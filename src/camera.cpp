#include "FunnyFace/camera.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>

#include "FunnyFace/common.h"
#include "config.hpp"

using namespace funnyface;

// Definition of the static member
std::atomic<bool> CameraManager::keepRunning_{true};

CameraManager::CameraManager() : profiler_(Profiler::getInstance())
{
}

CameraManager::~CameraManager()
{
    close(outputDevice_.fd);
}

void CameraManager::configureInputDevice(const char* input_device, unsigned int width, unsigned int height,
                                         unsigned int buffer_count)
{
    CapturingDevice device = inputDeviceContext_.getDevice();
    device.name = std::string("Unknown");
    device.device_path = std::string(input_device);
    device.width = width;
    device.height = height;
    device.buffer_count = buffer_count;
    device.fd = -1;
}

void CameraManager::configureOutputDevice(const char* out_device, unsigned int width, unsigned int height)
{
    outputDevice_.name = std::string("Unkown");
    outputDevice_.device_path = out_device;
    outputDevice_.width = width;
    outputDevice_.height = height;
    outputDevice_.fd = -1; // Will be set in configureOutputCamera
}

bool CameraManager::initialize()
{
    if (inputDeviceContext_.getDevice().device_path.empty())
    {
        common::log_error("CameraManager::initialize - Input device not configured. Aborting.");
        return false;
    }

    if (outputDevice_.device_path.empty())
    {
        common::log_error("CameraManager::initialize - Output device not configured. Aborting.");
        return false;
    }

    if (!configureVirtualOuputCamera())
    {
        common::log_error("CameraManager::initialize - Cannot proceed. Aborting.");
        return false;
    }

    if (!inputDeviceContext_.setupDevice(inputDeviceContext_.getDevice()))
    {
        common::log_error("CameraManager::initialize - Cannot proceed. Aborting.");
        return false;
    }

    jpegManager_ = std::make_shared<JPEGManager>(outputDevice_.fd, outputDevice_.width, outputDevice_.height,
                                                 outputDevice_.subsampling);

    // Start streaming thread.
    if (!record())
    {
        common::log_error("CameraManager::initialize - Unable to record. Aborting.");
        return false;
    }

    return true;
}

bool CameraManager::configureVirtualOuputCamera()
{
    struct v4l2_format format;

    common::log_info("CameraManager::configureVirtualOuputCamera - Configuring output device %s",
                     outputDevice_.device_path.c_str());
    // Get the file descriptor
    if ((outputDevice_.fd = open(outputDevice_.device_path.c_str(), O_RDWR)) < 0)
    {
        common::errno_log("Error open video out!");
        return false;
    }

    CLEAR(format);

    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    // Get information from output device
    if (ioctl(outputDevice_.fd, VIDIOC_G_FMT, &format) < 0)
    {
        common::errno_log("VIDIOC_G_FMT");
        return false;
    }

    // Configure the device
    logFormat(format);

    format.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG; // Si video to video
    // format.fmt.pix.colorspace = V4L2_PIX_FMT_ARGB32;
    // My camera is able to do: 1920x1080 1280x720 640x480 640x360 320x240
    format.fmt.pix.width = outputDevice_.width;
    format.fmt.pix.height = outputDevice_.height;
    format.fmt.pix.field = V4L2_FIELD_NONE;

    // Change the device configuration with the new parameters
    if (ioctl(outputDevice_.fd, VIDIOC_S_FMT, &format) < 0)
    {
        common::errno_log("VIDIOC_S_FMT");
        return false;
    }

    return true;
}

bool CameraManager::record()
{
    return inputDeviceContext_.startRecording(jpegManager_, profiler_);
}

bool CameraManager::update(std::function<void(Image&)> paint)
{
    bool success{true};

    if(!inputDeviceContext_.isImageReady())
    {
        return success; // No image ready, don't block
    }

    // Get the next image from InputDeviceContext
    Image* currentImage = inputDeviceContext_.getCurrentImage();


    profiler_.start("1", "Processing time");
    // Process the image
    paint(*currentImage);
    profiler_.stop("1", "Processing time");

    profiler_.start("1", "Encode and write output image");

    // Encode and send to output
    if (!jpegManager_->encodeAndWriteToOutput(*currentImage))
    {
        success = false;
    }

    // Release the image
    currentImage->beingUsed_.store(false);
    profiler_.stop("1", "Encode and write output image");
    return success;
}

void CameraManager::reconfigureInputCamera()
{
    common::log_info("CameraManager::reconfigureInputCamera - Starting camera reconfiguration");

    // Reconfigure input camera with new settings
    if (!inputDeviceContext_.reconfigureDevice(inputDeviceContext_.getDevice()))
    {
        common::log_error("CameraManager::reconfigureInputCamera - Failed to reconfigure input device");
        return;
    }

    // Restart recording thread
    if (!record())
    {
        common::log_error("CameraManager::reconfigureInputCamera - Failed to restart recording");
        return;
    }

    common::log_info("CameraManager::reconfigureInputCamera - Camera reconfiguration completed successfully");
}

void CameraManager::reconfigureOutputCamera()
{
    common::log_info("CameraManager::reconfigureOutputCamera - Starting output camera reconfiguration");

    // Close current output device
    if (outputDevice_.fd >= 0)
    {
        close(outputDevice_.fd);
        outputDevice_.fd = -1;
    }

    // Reconfigure output camera with new settings
    if (!configureVirtualOuputCamera())
    {
        common::log_error("CameraManager::reconfigureOutputCamera - Failed to configure output camera");
        return;
    }

    // Update JPEG manager with new settings
    if (jpegManager_)
    {
        jpegManager_.reset();
        jpegManager_ = std::make_shared<JPEGManager>(outputDevice_.fd, outputDevice_.width, outputDevice_.height,
                                                     outputDevice_.subsampling);
    }

    common::log_info("CameraManager::reconfigureOutputCamera - Output camera reconfiguration completed successfully");
}
