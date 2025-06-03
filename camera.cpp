#include "camera.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>

#include "common.h"
#include "config.hpp"

using namespace funnyface;

// Definition of the static member
std::atomic<bool> CameraManager::keepRunning_{true};

CameraManager::CameraManager() : profiler_(Profiler::getInstance())
{
}

CameraManager::~CameraManager()
{
    if (processingThread_.joinable())
    {
        processingThread_.join();
    }

    close(outputDevice_.fd);
}

void CameraManager::logFormat(v4l2_format vid_format)
{
    common::log_info("v4l2_format struct:");
    common::log_info("vid_format->type                =%u", vid_format.type);
    common::log_info("vid_format->fmt.pix.width       =%u", vid_format.fmt.pix.width);
    common::log_info("vid_format->fmt.pix.height      =%u", vid_format.fmt.pix.height);
    common::log_info("vid_format->fmt.pix.pixelformat =%u", vid_format.fmt.pix.pixelformat);
    common::log_info("vid_format->fmt.pix.sizeimage   =%u", vid_format.fmt.pix.sizeimage);
    common::log_info("vid_format->fmt.pix.field       =%u", vid_format.fmt.pix.field);
    common::log_info("vid_format->fmt.pix.bytesperline=%u", vid_format.fmt.pix.bytesperline);
    common::log_info("vid_format->fmt.pix.colorspace  =%u", vid_format.fmt.pix.colorspace);
}

bool CameraManager::getCameraCapabilities(const CapturingDevice& device, CameraCapabilities& outCaps)
{
    struct v4l2_capability cap;
    if (ioctl(device.fd, VIDIOC_QUERYCAP, &cap) == -1)
    {
        common::errno_log("CameraManager::getCameraCapabilities - VIDIOC_QUERYCAP");
        close(device.fd);
        return false;
    }

    // Check if we have the required capabilities
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        common::errno_log(
            "CameraManager::getCameraCapabilities - The device does not handle single-planar video capture.");
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING))
    {
        common::errno_log("CameraManager::getCameraCapabilities - The device does not handle streaming.");
        return false;
    }

    outCaps.driver = reinterpret_cast<char*>(cap.driver);
    outCaps.card = reinterpret_cast<char*>(cap.card);
    outCaps.bus_info = reinterpret_cast<char*>(cap.bus_info);

    struct v4l2_fmtdesc fmtdesc;
    CLEAR(fmtdesc);
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    outCaps.formats.clear();
    for (fmtdesc.index = 0; ioctl(device.fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0; ++fmtdesc.index)
    {
        Format fmt;
        fmt.description = reinterpret_cast<char*>(fmtdesc.description);
        fmt.pixelformat = fmtdesc.pixelformat;

        struct v4l2_frmsizeenum frmsize;
        CLEAR(frmsize);
        frmsize.pixel_format = fmtdesc.pixelformat;

        for (frmsize.index = 0; ioctl(device.fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0; ++frmsize.index)
        {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE)
            {
                FrameSize size;
                size.width = frmsize.discrete.width;
                size.height = frmsize.discrete.height;
                fmt.sizes.push_back(size);
            }
        }

        outCaps.formats.push_back(fmt);
    }

    return true;
}

bool CameraManager::getCapabilities(int fd, v4l2_capability& cap)
{
    CLEAR(cap);

    // Retrieve device capabilities
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
    {
        common::errno_log("CameraManager::getCapabilities - VIDIOC_QUERYCAP");
        return false;
    }
    return true;
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

    // Signal to stop recording
    keepRunning_ = false;

    // Stop recording through InputDeviceContext
    inputDeviceContext_.stopRecording();

    // Reset running flag
    keepRunning_ = true;

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
