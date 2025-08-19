#include "LinuxFace/cameraManager.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <set>
#include <sys/ioctl.h>
#include <unistd.h>

#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"
#include "LinuxFace/UI/layerManager.h"
using linuxface::CameraManager;
using linuxface::Webcam;
using linuxface::Image;

CameraManager::CameraManager() : inWebcam_(), outWebcam_(), layerManager_(nullptr) {}

CameraManager::~CameraManager()
{
    shutdown();
}

void CameraManager::setLayerManager(std::shared_ptr<LayerManager> layerManager)
{
    layerManager_ = layerManager;
}

void CameraManager::shutdown()
{
    for (const auto& camera : inWebcam_)
    {
        camera->stop();
    }
    inWebcam_.clear();

    for (const auto& camera : outWebcam_)
    {
        camera->stop();
    }
    outWebcam_.clear();
}

bool CameraManager::updateInput()
{
    if (!layerManager_)
    {
        common::log_error("CameraManager::updateInput - LayerManager not set");
        return false;
    }

    for (auto& input : inWebcam_)
    {
        if (input->isRunning())
        {
            // Skip if the input is not currently selected
            if (!input->isCurrentlySelected())
            {
                continue;
            }

            std::unique_ptr<Image> newFrame;

            if (!input->getImage(newFrame))
            {
                continue;
            }
            else if (newFrame == nullptr)
            {
                common::log_error("CameraManager::updateInput - Input image is null");
                continue;
            }
            else if (newFrame->info.width == 0 || newFrame->info.height == 0)
            {
                common::log_error("CameraManager::updateInput - Input image invalid size: %d x %d",
                                  newFrame->info.width, newFrame->info.height);
                continue;
            }

            updateCameraLayer(input, std::move(newFrame));
        }
    }

    return true;
}

void CameraManager::updateCameraLayer(std::shared_ptr<InputWebcam> camera, std::unique_ptr<Image> newFrame)
{
    // Find or create layer for this camera
    Layer* existingLayer = layerManager_->getLayerByCameraDevicePath(camera->getDevicePath());
    
    if (existingLayer != nullptr)
    {
        // Update existing layer
        if (existingLayer->resizeScale != 1.0f)
        {
            // Apply resize if scale is set
            int targetWidth = static_cast<int>(newFrame->info.width * existingLayer->resizeScale);
            int targetHeight = static_cast<int>(newFrame->info.height * existingLayer->resizeScale);
            
            if (targetWidth > 0 && targetHeight > 0)
            {
                newFrame->scaleToInPlace(static_cast<size_t>(targetWidth), static_cast<size_t>(targetHeight));
            }
        }
        
        existingLayer->img = std::shared_ptr<Image>(newFrame.release());
        existingLayer->dirty = true;
    }
    else
    {
        // Create new layer for this camera
        Layer newLayer;
        newLayer.id = Layer::next_id++;
        newLayer.type = LayerType::Image;
        newLayer.name = camera->getName();
        newLayer.cameraDevicePath = camera->getDevicePath();
        newLayer.selected = false;
        newLayer.resizeScale = 1.0f;
        newLayer.img = std::shared_ptr<Image>(newFrame.release());
        newLayer.dirty = true;
        newLayer.x = 0.0f;
        newLayer.y = 0.0f;
        
        if (newLayer.img)
        {
            // Assign next available layer number
            int nextLayerNumber = 0;
            for (const auto& layer : layerManager_->getLayers())
            {
                nextLayerNumber = std::max(nextLayerNumber, layer.getLayerNumber() + 1);
            }
            newLayer.img->info.layer = nextLayerNumber;
        }
        
        layerManager_->addLayer(newLayer);
    }
}

bool CameraManager::updateOutput(std::unique_ptr<Image>& image /*image*/)
{
    Profiler::getInstance().start("CameraManager", "Encode and write all output images");

    if (!image)
    {
        common::log_error("No image to encode and write to output");
        return false;
    }

    bool success = true;
    for (auto& output : outWebcam_)
    {
        if (output->isRunning())
        {
            if (!output->writeFrame(*image))
            {
                common::log_error("Failed to write frame to output device %s", output->getDevicePath().c_str());
                success = false;
            }
        }
        break;
    }

    Profiler::getInstance().stop("CameraManager", "Encode and write all output images");
    return success;
}

bool CameraManager::addCamera(std::shared_ptr<Webcam> camera /*camera*/)
{
    if (auto input = std::dynamic_pointer_cast<InputWebcam>(camera))
    {
        return addCameraImpl(inWebcam_, std::move(input));
    }
    else if (auto output = std::dynamic_pointer_cast<V4L2LoopbackWriter>(camera))
    {
        bool result = addCameraImpl(outWebcam_, output);
        if (result && layerManager_)
        {
            createOutputCameraOverlay(output);
        }
        return result;
    }

    common::log_error("CameraManager::addCamera - Unknown webcam type");
    return false;
}


bool CameraManager::removeCamera(std::shared_ptr<Webcam> camera /*camera*/)
{
    const std::string& devicePath = camera->getDevicePath();

    if (auto input = std::dynamic_pointer_cast<InputWebcam>(camera))
    {
        return removeCameraImpl(inWebcam_, devicePath);
    }
    else if (auto output = std::dynamic_pointer_cast<V4L2LoopbackWriter>(camera))
    {
        return removeCameraImpl(outWebcam_, devicePath);
    }

    common::log_error("CameraManager::removeCamera unknown webcam type");
    return false;
}

bool CameraManager::updateCamera(std::shared_ptr<Webcam> camera /*camera*/)
{
    if (auto input = std::dynamic_pointer_cast<InputWebcam>(camera))
    {
        return updateCameraImpl(inWebcam_, std::move(input));
    }
    else if (auto output = std::dynamic_pointer_cast<V4L2LoopbackWriter>(camera))
    {
        return updateCameraImpl(outWebcam_, std::move(output));
    }

    common::log_error("CameraManager::updateCamera unknown webcam type");
    return false;
}

std::vector<std::shared_ptr<Webcam>> CameraManager::getWebcams() const
{
    std::vector<std::shared_ptr<Webcam>> result;
    result.reserve(inWebcam_.size() + outWebcam_.size()); // Optional, improves performance
    result.insert(result.end(), inWebcam_.begin(), inWebcam_.end());
    result.insert(result.end(), outWebcam_.begin(), outWebcam_.end());
    return result;
}

std::vector<std::string> CameraManager::discoverAvailableVideoDevices()
{
    std::vector<std::string> availableDevices;

    // Get list of already managed device paths
    std::set<std::string> managedDevicePaths;
    for (const auto& camera : inWebcam_)
    {
        managedDevicePaths.insert(camera->getDevicePath());
    }
    for (const auto& camera : outWebcam_)
    {
        managedDevicePaths.insert(camera->getDevicePath());
    }

    // Scan /dev for video devices
    for (int i = 0; i < 64; ++i) // Check video0 through video63
    {
        std::string devicePath = "/dev/video" + std::to_string(i);

        // Skip if device is already managed
        if (managedDevicePaths.find(devicePath) != managedDevicePaths.end())
        {
            continue;
        }

        if (common::file_exists(devicePath) && isDeviceUsable(devicePath))
        {
            availableDevices.push_back(devicePath);
            common::log_info("CameraManager::discoverAvailableVideoDevices - Found unmanaged usable device: %s",
                             devicePath.c_str());
        }
    }

    return availableDevices;
}

bool CameraManager::isDeviceUsable(const std::string& devicePath /*devicePath*/)
{
    int fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        return false;
    }

        struct v4l2_capability cap{}; // initialize struct
    bool isUsable = false;

    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
    {
        // Check if it's an input device (has capture capability)
            if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0u && (cap.capabilities & V4L2_CAP_STREAMING) != 0u)
        {
            // Make sure it's not a v4l2loopback output device
            std::string driver = reinterpret_cast<char*>(cap.driver);
            if (driver != "v4l2 loopback")
            {
                isUsable = true;
            }
        }
    }

    close(fd);
    return isUsable;
}
