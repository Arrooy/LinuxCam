#include "FunnyFace/cameraManager.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <set>

#include "FunnyFace/common.h"
#include "FunnyFace/profiler.h"
using namespace funnyface;

CameraManager::CameraManager()
{
}

CameraManager::~CameraManager()
{
    shutdown();
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

bool CameraManager::updateInput(std::unique_ptr<Image>& outputImage)
{
    for (auto& input : inWebcam_)
    {
        if (input->isRunning())
        {
            Image* inImage = nullptr;

            if (!input->getImage(inImage))
            {
                // Input device doesn't have an image for us yet.
                continue;
            }
            else if (inImage == nullptr)
            {
                common::log_error("CameraManager::updateInput - Input image is null");
                return false;
            }
            else if (inImage->info.width == 0 || inImage->info.height == 0)
            {
                common::log_error("CameraManager::updateInput - Input image invalid size size: %d x %d",
                                  inImage->info.width, inImage->info.height);
                inImage->setBeingUsed(false);
                continue;
            }
            // inImage->scaleByPercentage(35.0f);
            // Valid image, copy it to output image
            if (!outputImage)
            {
                outputImage = inImage->deepCopy();
            }
            else
            {
                outputImage->paste(*inImage);
            }
            inImage->setBeingUsed(false);

            if (outputImage == nullptr)
            {
                common::log_error("CameraManager::updateInput - Failed to create outputImage");
                continue;
            }
        }

        break;
    }

    return outputImage != nullptr;
}

bool CameraManager::updateOutput(std::unique_ptr<Image>& image)
{
    Profiler::getInstance().start("1", "Encode and write all output images");

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

    // Release the image
    image->setBeingUsed(false);
    Profiler::getInstance().stop("1", "Encode and write all output images");
    return success;
}

bool CameraManager::addCamera(std::shared_ptr<Webcam> camera)
{
    if (auto input = std::dynamic_pointer_cast<InputWebcam>(camera))
    {
        return addCameraImpl(inWebcam_, std::move(input));
    }
    else if (auto output = std::dynamic_pointer_cast<V4L2LoopbackWriter>(camera))
    {
        return addCameraImpl(outWebcam_, std::move(output));
    }

    common::log_error("CameraManager::addCamera - Unknown webcam type");
    return false;
}


bool CameraManager::removeCamera(std::shared_ptr<Webcam> camera)
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

bool CameraManager::updateCamera(std::shared_ptr<Webcam> camera)
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

std::vector<std::string> CameraManager::discoverAvailableInputDevices()
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
            common::log_info("CameraManager::discoverAvailableInputDevices - Found unmanaged usable device: %s",
                             devicePath.c_str());
        }
    }

    return availableDevices;
}

bool CameraManager::isDeviceUsable(const std::string& devicePath)
{
    int fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        return false;
    }

    struct v4l2_capability cap;
    bool isUsable = false;

    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
    {
        // Check if it's an input device (has capture capability)
        if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) && (cap.capabilities & V4L2_CAP_STREAMING))
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
