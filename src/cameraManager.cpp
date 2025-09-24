#include "LinuxFace/cameraManager.h"

#include <cmath>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <set>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>

#include "LinuxFace/UI/layerManager.h"
#include "LinuxFace/common.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/profiler.h"

using linuxface::CameraManager;
using linuxface::Image;
using linuxface::Pixel;
using linuxface::Webcam;

CameraManager::~CameraManager()
{
    shutdown();
}

void CameraManager::setLayerManager(std::shared_ptr<LayerManager> layerManager)
{
    layerManager_ = std::move(layerManager);

    // Create output preview layer when layer manager is set
    createOutputPreviewLayer();

    // Update preview visibility based on current output devices
    updatePreviewVisibility();
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
        common::logWarn("CameraManager::updateInput - LayerManager not set; nothing to do");
        return false;
    }

    bool anyUpdated = false;

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

            if (newFrame == nullptr)
            {
                common::logError("CameraManager::updateInput - Input image is null");
                continue;
            }

            if (newFrame->info.width == 0 || newFrame->info.height == 0)
            {
                common::logError("CameraManager::updateInput - Input image invalid size: %d x %d", newFrame->info.width,
                                 newFrame->info.height);
                continue;
            }

            updateCameraLayer(input, std::move(newFrame));
            anyUpdated = true;
        }
    }

    return anyUpdated;
}

void CameraManager::updateCameraLayer(const std::shared_ptr<InputWebcam>& camera, std::unique_ptr<Image> newFrame)
{
    // Find or create layer for this camera
    Layer* existingLayer = layerManager_->getLayerByCameraDevicePath(camera->getDevicePath());

    if (existingLayer != nullptr)
    {
        // Update existing layer
        if (existingLayer->resizeScale != 1.0f)
        {
            // Apply resize if scale is set
            const int targetWidth = static_cast<int>(newFrame->info.width * existingLayer->resizeScale);
            const int targetHeight = static_cast<int>(newFrame->info.height * existingLayer->resizeScale);

            if (targetWidth > 0 && targetHeight > 0)
            {
                newFrame->scaleToInPlace(static_cast<size_t>(targetWidth), static_cast<size_t>(targetHeight));
            }
        }

        existingLayer->img = std::shared_ptr<Image>(newFrame.release());
        existingLayer->dirty = true;
        // Preserve existing layer position - don't reset x,y coordinates
    }
    else
    {
        // Create new layer for this camera
        Layer newLayer;
        newLayer.id = Layer::getNextId();
        newLayer.type = LayerType::IMAGE;
        newLayer.name = camera->getName();
        newLayer.cameraDevicePath = camera->getDevicePath();
        newLayer.selected = false;
        newLayer.resizeScale = 1.0f;
        newLayer.img = std::shared_ptr<Image>(newFrame.release());
        newLayer.dirty = true;

        if (newLayer.img)
        {
            newLayer.img->info.layer = newLayer.id;
            newLayer.img->info.filename = newLayer.name;
        }

        layerManager_->addLayer(newLayer);

        common::logInfo("CameraManager::updateCameraLayer - Created layer '%s' for device: %s", newLayer.name.c_str(),
                        newLayer.cameraDevicePath.c_str());
    }
}

bool CameraManager::updateOutput(std::unique_ptr<Image>& image)
{
    Profiler::getInstance().start("CameraManager", "Encode and write all output images");

    if (!image)
    {
        common::logError("No image to encode and write to output");
        return false;
    }

    bool success = true;
    std::unique_ptr<Image> processedOutputImage{nullptr};

    for (auto& output : outWebcam_)
    {
        if (output->isRunning())
        {
            // Pre-process image once for all outputs with the same dimensions
            const unsigned long desiredWidth = output->getDesiredWidth();
            const unsigned long desiredHeight = output->getDesiredHeight();

            // Check if we can reuse the already processed image
            if (!processedOutputImage || processedOutputImage->info.width != desiredWidth
                || processedOutputImage->info.height != desiredHeight)
            {
                // Create a copy of the original image for processing
                processedOutputImage = image->deepCopy();

                // Scale if needed
                processedOutputImage->scaleToInPlace(desiredWidth, desiredHeight, ScalingAlgorithm::BICUBIC);

                // Convert RGBA to RGB for JPEG encoder compatibility
                if (!processedOutputImage->convertToRGBInplace())
                {
                    common::logError("CameraManager::updateOutput - Failed to convert RGBA to RGB");
                    success = false;
                    continue;
                }
            }

            // Use the pre-processed image
            std::unique_ptr<Image> outputImageCopy = processedOutputImage->deepCopy();
            if (!output->writeFrame(*outputImageCopy))
            {
                common::logError("Failed to write frame to output device %s", output->getDevicePath().c_str());
                success = false;
            }
        }
    }

    Profiler::getInstance().stop("CameraManager", "Encode and write all output images");
    Profiler::getInstance().start("CameraManager", "Update output preview layer");

    // Update preview layer with the processed image if available, otherwise use original
    if (processedOutputImage)
    {
        updateOutputPreviewLayer(*processedOutputImage);
    }
    else
    {
        updateOutputPreviewLayer(*image);
    }
    Profiler::getInstance().stop("CameraManager", "Update output preview layer");

    return success;
}

bool CameraManager::addCamera(const std::shared_ptr<Webcam>& camera)
{
    if (auto input = std::dynamic_pointer_cast<InputWebcam>(camera))
    {
        return addCameraImpl(inWebcam_, std::move(input));
    }
    if (auto output = std::dynamic_pointer_cast<V4L2LoopbackWriter>(camera))
    {
        return addCameraImpl(outWebcam_, output);
    }

    common::logError("CameraManager::addCamera - Unknown webcam type");
    return false;
}


bool CameraManager::removeCamera(const std::shared_ptr<Webcam>& camera)
{
    const std::string& devicePath = camera->getDevicePath();

    if (auto input = std::dynamic_pointer_cast<InputWebcam>(camera))
    {
        return removeCameraImpl(inWebcam_, devicePath);
    }

    if (auto output = std::dynamic_pointer_cast<V4L2LoopbackWriter>(camera))
    {
        return removeCameraImpl(outWebcam_, devicePath);
    }

    common::logError("CameraManager::removeCamera unknown webcam type");
    return false;
}

bool CameraManager::updateCamera(const std::shared_ptr<Webcam>& camera)
{
    if (auto input = std::dynamic_pointer_cast<InputWebcam>(camera))
    {
        return updateCameraImpl(inWebcam_, std::move(input));
    }
    if (auto output = std::dynamic_pointer_cast<V4L2LoopbackWriter>(camera))
    {
        return updateCameraImpl(outWebcam_, std::move(output));
    }

    common::logError("CameraManager::updateCamera unknown webcam type");
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
        const std::string devicePath = "/dev/video" + std::to_string(i);

        // Skip if device is already managed
        if (managedDevicePaths.find(devicePath) != managedDevicePaths.end())
        {
            continue;
        }

        if (common::fileExists(devicePath) && isDeviceUsable(devicePath))
        {
            availableDevices.push_back(devicePath);
            common::logInfo("CameraManager::discoverAvailableVideoDevices - Found unmanaged usable device: %s",
                            devicePath.c_str());
        }
    }

    return availableDevices;
}

bool CameraManager::isDeviceUsable(const std::string& devicePath)
{
    const int fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
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
            const std::string driver = reinterpret_cast<char*>(cap.driver);
            if (driver != "v4l2 loopback")
            {
                isUsable = true;
            }
        }
    }

    close(fd);
    return isUsable;
}

void CameraManager::updatePreviewVisibility()
{
    if (!layerManager_)
    {
        return;
    }

    Layer* previewLayer = layerManager_->getLayerByName("Output Preview");

    // Always show/create preview layer
    if (previewLayer == nullptr)
    {
        createOutputPreviewLayer();
    }
    // Preview layer exists and should be visible (no action needed)
    common::logInfo("CameraManager::updatePreviewVisibility - Ensuring output preview is always visible");
}

void CameraManager::createOutputPreviewLayer()
{
    if (!layerManager_)
    {
        return;
    }

    // Check if preview already exists
    if (layerManager_->getLayerByName("Output Preview") != nullptr)
    {
        return; // Already exists
    }
    common::logInfo("CameraManager::createOutputPreviewLayer - Creating output preview layer");

    // Determine preview dimensions based on output devices
    unsigned int previewWidth = 640;  // Default width
    unsigned int previewHeight = 480; // Default height

    // Use dimensions from the first output device if available
    if (!outWebcam_.empty() && outWebcam_[0])
    {
        previewWidth = outWebcam_[0]->getDesiredWidth();
        previewHeight = outWebcam_[0]->getDesiredHeight();
        common::logInfo("CameraManager::createOutputPreviewLayer - Using output device dimensions: %dx%d", previewWidth,
                        previewHeight);
    }
    else
    {
        common::logInfo("CameraManager::createOutputPreviewLayer - Using default dimensions: %dx%d", previewWidth,
                        previewHeight);
    }

    // Create preview image with determined dimensions
    auto previewImage = std::make_shared<Image>(Pixel(0, 0, 0, 0), previewWidth, previewHeight);

    // Create preview layer as a regular IMAGE layer
    Layer previewLayer;
    previewLayer.id = Layer::getNextId();
    previewLayer.type = LayerType::IMAGE;
    previewLayer.name = "Output Preview";
    previewLayer.cameraDevicePath = ""; // Not associated with a camera device
    previewLayer.selected = false;
    previewLayer.resizeScale = 1.0f;
    previewLayer.img = previewImage;
    previewLayer.dirty = true;
    previewLayer.locked = false;

    // Set initial position
    previewLayer.x = 0;
    previewLayer.y = 0;

    if (previewLayer.img)
    {
        previewLayer.img->info.layer = previewLayer.id;
        previewLayer.img->info.filename = "Output Preview";
    }

    layerManager_->addLayer(previewLayer);
    common::logInfo("CameraManager::createOutputPreviewLayer - Created output preview layer (%dx%d)",
                    previewLayer.img->info.width, previewLayer.img->info.height);
}

void CameraManager::updateOutputPreviewLayer(const Image& compositeImage)
{
    if (!layerManager_)
    {
        return;
    }

    // Find the output preview layer
    Layer* previewLayer = layerManager_->getLayerByName("Output Preview");
    if (previewLayer == nullptr || previewLayer->type != LayerType::IMAGE || !previewLayer->img)
    {
        // Create the preview layer if it doesn't exist or is invalid
        createOutputPreviewLayer();
        previewLayer = layerManager_->getLayerByName("Output Preview");

        if (previewLayer == nullptr || previewLayer->type != LayerType::IMAGE || !previewLayer->img)
        {
            common::logWarn("CameraManager::updateOutputPreviewLayer - Failed to create valid preview layer");
            return;
        }
    }

    // Update the preview layer with the composite image
    previewLayer->img = std::shared_ptr<Image>(compositeImage.deepCopy());
    previewLayer->dirty = true;
    // Preserve the layer name
    if (previewLayer->img)
    {
        previewLayer->img->info.layer = previewLayer->id;
        previewLayer->img->info.filename = "Output Preview";
    }
}
