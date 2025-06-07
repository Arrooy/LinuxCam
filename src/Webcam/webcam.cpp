#include "FunnyFace/webcam.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include "FunnyFace/common.h"

using namespace funnyface;

Webcam::Webcam(const std::string& name, const std::string& devicePath, const WebcamType type, const unsigned int width,
               const unsigned int height)
    : name_(name), device_path_(devicePath), fd_(-1), desiredWidth_(width), desiredHeight_(height), type_(type)
{
}

bool Webcam::open()
{
    if (device_path_.empty())
    {
        common::log_error("Webcam::open - No device path specified");
        return false;
    }

    if (fd_ >= 0)
    {
        common::log_error("Webcam::open - Device already open");
        return false;
    }

    common::log_info("Webcam::open - Opening device %s, path: %s", name_.c_str(), device_path_.c_str());

    auto flags = O_RDWR;
    if (type_ == WebcamType::VirtualOutput)
    {
        flags = O_WRONLY;
    }

    if ((fd_ = ::open(device_path_.c_str(), flags)) < 0)
    {
        common::errno_log("Webcam::open - Failed to open device");
        return false;
    }

    return true;
}


bool Webcam::updateDeviceCapabilities()
{
    struct v4l2_capability cap;

    CLEAR(cap);

    if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) == -1)
    {
        common::errno_log("Webcam::updateDeviceCapabilities - VIDIOC_QUERYCAP");
        return false;
    }

    // Check if we have the required capabilities
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        common::errno_log("Webcam::updateDeviceCapabilities - The device does not handle single-planar video capture.");
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING))
    {
        common::errno_log("Webcam::updateDeviceCapabilities - The device does not handle streaming.");
        return false;
    }

    capabilities_.driver = reinterpret_cast<char*>(cap.driver);
    capabilities_.card = reinterpret_cast<char*>(cap.card);
    capabilities_.bus_info = reinterpret_cast<char*>(cap.bus_info);

    struct v4l2_fmtdesc fmtdesc;
    CLEAR(fmtdesc);
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    capabilities_.formats.clear();
    for (fmtdesc.index = 0; ioctl(fd_, VIDIOC_ENUM_FMT, &fmtdesc) == 0; ++fmtdesc.index)
    {
        Format fmt;
        fmt.description = std::string(reinterpret_cast<char*>(fmtdesc.description));
        common::log_info("Webcam::updateDeviceCapabilities - Camera supports format: %s", fmt.description.c_str());
        // TODO: Update format_ with description.
        fmt.format = ImageFormat::JPEG;
        // TODO: conversion from fourcc to generic
        fmt.pixelformat = fmtdesc.pixelformat;
        struct v4l2_frmsizeenum frmsize;
        CLEAR(frmsize);
        frmsize.pixel_format = fmtdesc.pixelformat;

        for (frmsize.index = 0; ioctl(fd_, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0; ++frmsize.index)
        {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE)
            {
                FrameSize size;
                size.width = frmsize.discrete.width;
                size.height = frmsize.discrete.height;
                fmt.sizes.push_back(size);
            }
        }

        capabilities_.formats.push_back(fmt);
    }


    if (capabilities_.formats.empty())
    {
        common::log_error("Webcam::UpdateDeviceCapabilities - This should never happen with an input device.");
        return false;
    }

    // Select the format that best adapts to user requirements.
    for (auto& fmt : capabilities_.formats)
    {
        unsigned int index{0u};
        for (const auto& size : fmt.sizes)
        {
            if (size.width == desiredWidth_ && size.height == desiredHeight_)
            {
                common::log_info("Webcam: Selected format is %s with frame size of %dx%d", fmt.description.c_str(),
                                 size.width, size.height);
                fmt.selectedFrameSize = index;
                selectedFormat_ = std::make_unique<Format>(fmt);
                return true;
            }
            index++;
        }
    }
    // If no format is found, select the first one.
    capabilities_.formats[0].selectedFrameSize = 0u;
    auto elected_size = capabilities_.formats[0].sizes[0];
    common::log_error("Webcam: No format found for %dx%d, selecting first one of %dx%d", desiredWidth_, desiredHeight_,
                      elected_size.width, elected_size.height);
    selectedFormat_ = std::make_unique<Format>(capabilities_.formats[0]);


    return true;
}


bool Webcam::configureDeviceFormat()
{
    struct v4l2_format format;

    // Set the video format with retry mechanism
    for (int i = 0; i < 2; i++)
    {
        CLEAR(format);
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.pixelformat = selectedFormat_->pixelformat;
        FrameSize frameSize = selectedFormat_->sizes[selectedFormat_->selectedFrameSize];
        format.fmt.pix.width = frameSize.width;
        format.fmt.pix.height = frameSize.height;
        format.fmt.pix.field = V4L2_FIELD_NONE;

        if (ioctl(fd_, VIDIOC_S_FMT, &format) < 0)
        {
            if (errno == EBUSY)
            {
                common::log_warn("Webcam::configureDeviceFormat - Device is busy, trying again later.");
            }
            else
            {
                common::errno_log("Webcam::configureDeviceFormat - Error configuring device format");
                return false;
            }
        }
        else
        {
            break;
        }
    }

    return true;
}
