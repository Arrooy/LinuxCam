#include "FunnyFace/webcam.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <cmath>

#include "FunnyFace/common.h"

using namespace funnyface;

Webcam::Webcam(const std::string& name, const std::string& devicePath, const WebcamType type, const unsigned int width,
               const unsigned int height)
    : name_(name), device_path_(devicePath), fd_(-1), type_(type)
{
    std::vector<FrameSize> frameSizes;
    frameSizes.push_back(FrameSize{width, height, 0, {0u}});
    Format fmt{"Constructor", ImageFormat::UNKNOWN, 0, 0, frameSizes};
    selectedFormat_ = std::make_unique<Format>(fmt);
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


// TODO: FIXME: When selecting a format, select highest framerate allways
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

    if (name_.empty())
    {
        name_ = capabilities_.card + " - " + capabilities_.driver;
    }

    struct v4l2_fmtdesc fmtdesc;
    CLEAR(fmtdesc);
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    capabilities_.formats.clear();

    // Iterate over all formats supported by the device.
    for (fmtdesc.index = 0; ioctl(fd_, VIDIOC_ENUM_FMT, &fmtdesc) == 0; ++fmtdesc.index)
    {
        Format fmt;
        fmt.description = std::string(reinterpret_cast<char*>(fmtdesc.description));
        fmt.sizes.clear();

        if (fmtdesc.pixelformat == V4L2_PIX_FMT_JPEG || fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG)
        {
            fmt.format = ImageFormat::JPEG;
            fmt.pixelformat = fmtdesc.pixelformat;
            common::log_info("Webcam::updateDeviceCapabilities - Camera supports MJPEG format (%s)",
                             fmt.description.c_str());
        }
        else if (fmtdesc.pixelformat == V4L2_PIX_FMT_SGBRG8)
        {
            fmt.format = ImageFormat::SGBRG8;
            fmt.pixelformat = fmtdesc.pixelformat;
            common::log_info("Webcam::updateDeviceCapabilities - Camera supports Bayer format (%s)",
                             fmt.description.c_str());
        }
        else if (fmtdesc.pixelformat == V4L2_PIX_FMT_Z16)
        {
            fmt.format = ImageFormat::DEPTH_Z16;
            fmt.pixelformat = fmtdesc.pixelformat;
            common::log_info("Webcam::updateDeviceCapabilities - Camera supports Z16 format (%s)",
                             fmt.description.c_str());
        }
        else if (fmtdesc.pixelformat == V4L2_PIX_FMT_UYVY)
        {
            fmt.format = ImageFormat::UYUV422;
            fmt.pixelformat = fmtdesc.pixelformat;
            common::log_info("Webcam::updateDeviceCapabilities - Camera supports UYUV422 format (%s)",
                             fmt.description.c_str());
        }
        else if (fmtdesc.pixelformat == V4L2_PIX_FMT_YUYV)
        {
            fmt.format = ImageFormat::YUYV422;
            fmt.pixelformat = fmtdesc.pixelformat;
            common::log_info("Webcam::updateDeviceCapabilities - Camera supports YUYV422 format (%s)",
                             fmt.description.c_str());
        }

        else
        {
            common::log_warn("Webcam::updateDeviceCapabilities - Camera supports unknown format: %s",
                             fmt.description.c_str());
            continue; // Skip unsupported formats
        }

        // Query available frame sizes for the current format
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
                size.fps.clear();

                struct v4l2_frmivalenum frmival;
                CLEAR(frmival);
                frmival.pixel_format = fmtdesc.pixelformat;
                frmival.width = frmsize.discrete.width;
                frmival.height = frmsize.discrete.height;

                for (frmival.index = 0; ioctl(fd_, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) == 0; ++frmival.index)
                {
                    if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE)
                    {
                        int fps = static_cast<int>(static_cast<double>(frmival.discrete.denominator)
                                                   / static_cast<double>(frmival.discrete.numerator));

                        if (std::find(size.fps.begin(), size.fps.end(), fps) == size.fps.end())
                        {
                            size.fps.push_back(fps); // Only insert if not already present
                        }
                    }
                }

                if (std::find(fmt.sizes.begin(), fmt.sizes.end(), size) == fmt.sizes.end())
                {
                    fmt.sizes.push_back(size); // No duplicate width/height/fps
                }
            }
        }

        capabilities_.formats.push_back(fmt);
    }


    if (capabilities_.formats.empty())
    {
        common::log_warn("Webcam::UpdateDeviceCapabilities - This input device does not have any capabilities for "
                         "V4L2_BUF_TYPE_VIDEO_CAPTURE.");
        return false;
    }

    // Select the format that best adapts to user requirements.
    selectBestFormat();
    return true;
}

void Webcam::selectBestFormat()
{
    Format* bestFormat = nullptr;
    unsigned int bestIndex = 0;
    unsigned int bestFpsIndex = 0;

    double bestDistance = std::numeric_limits<double>::max();

    for (auto& fmt : capabilities_.formats)
    {
        if (fmt.sizes.empty())
        {
            // Skip formats without frame sizes.
            continue;
        }

        if (selectedFormat_ && selectedFormat_->format != ImageFormat::UNKNOWN)
        {
            // Skip formats that don't match the desired format.
            if (selectedFormat_->format != fmt.format)
            {
                continue;
            }
        }

        auto [targetIndex, targetFpsIndex, targetDistance] = findBestFrameSize(fmt);

        if (targetDistance < bestDistance)
        {
            bestDistance = targetDistance;
            bestFormat = &fmt;
            bestIndex = targetIndex;
            bestFpsIndex = targetFpsIndex;
        }
    }

    if (bestFormat != nullptr)
    {
        auto& selectedSize = bestFormat->sizes[bestIndex];
        bestFormat->selectedFrameSize = bestIndex;
        selectedSize.selectedFPS = bestFpsIndex;
        common::log_info("Webcam: Found bestFpsIndex %d, selected FPS %d", bestFpsIndex,
                         selectedSize.fps[selectedSize.selectedFPS]);

        common::log_info("Webcam: Selected format is %s with frame size of %dx%d (%d FPS). And with pixel format %u",
                         bestFormat->description.c_str(), selectedSize.width, selectedSize.height,
                         selectedSize.fps[selectedSize.selectedFPS], bestFormat->pixelformat);
        selectedFormat_ = std::make_unique<Format>(*bestFormat);
    }
    else
    {
        // Fallback: select the first format if nothing else works
        auto& electedSize = capabilities_.formats[0].sizes[0];
        capabilities_.formats[0].selectedFrameSize = 0u;
        electedSize.selectedFPS = 0u;
        common::log_error("Webcam: No suitable format found, selecting first one of %dx%d and %d FPS",
                          electedSize.width, electedSize.height, electedSize.fps[electedSize.selectedFPS]);
        selectedFormat_ = std::make_unique<Format>(capabilities_.formats[0]);
    }
}

std::tuple<unsigned int, unsigned int, double> Webcam::findBestFrameSize(const Format& fmt) const
{
    if (!selectedFormat_ || selectedFormat_->sizes[selectedFormat_->selectedFrameSize].width == 0
        || selectedFormat_->sizes[selectedFormat_->selectedFrameSize].height == 0)
    {
        // No selected format, select the central size
        common::log_warn("Webcam::findBestFrameSize - No selected format, selecting central size");
        return {fmt.sizes.size() / 2, 0, 0.0}; // Central size, highest priority
    }

    unsigned int bestIndex = 0;
    unsigned int bestFpsIndex = 0;

    double bestDistance = std::numeric_limits<double>::max();

    for (unsigned int i = 0; i < fmt.sizes.size(); ++i)
    {
        const auto& size = fmt.sizes[i];

        const auto& selectedSize = selectedFormat_->sizes[selectedFormat_->selectedFrameSize];
        unsigned int desiredWidth_ = selectedSize.width;
        unsigned int desiredHeight_ = selectedSize.height;

        // Check that the desired FPS appears in the format (ignore for 0fps)
        unsigned int desiredFPS = selectedSize.fps[selectedSize.selectedFPS];
        if (desiredFPS != 0 && std::find(size.fps.begin(), size.fps.end(), desiredFPS) == size.fps.end())
        {
            // Skip this format
            continue;
        }

        double distance = calculateDistance(size.width, size.height, desiredWidth_, desiredHeight_);

        if (distance <= bestDistance)
        {
            bestDistance = distance;
            bestIndex = i;
            // compute the index of the max value inside vector (Allways select best fps)
            bestFpsIndex = std::distance(size.fps.begin(), std::max_element(size.fps.begin(), size.fps.end()));
        }
    }

    return {bestIndex, bestFpsIndex, bestDistance};
}

double
Webcam::calculateDistance(unsigned int width1, unsigned int height1, unsigned int width2, unsigned int height2) const
{
    double dx = static_cast<double>(width1) - static_cast<double>(width2);
    double dy = static_cast<double>(height1) - static_cast<double>(height2);
    return std::sqrt(dx * dx + dy * dy);
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
        common::log_info("WebCam::configureDeviceFormat - %s - Trying to set format: pixfmt=%d, width=%d, height=%d",
                         name_.c_str(), format.fmt.pix.pixelformat, format.fmt.pix.width, format.fmt.pix.height);

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

bool Webcam::queueAllBuffersAgain(int numBuffers, int bufferType)
{
    struct v4l2_buffer buf;

    if (fd_ <= 0)
    {
        common::log_info("Webcam::queueAllBuffersAgain - Device not open. Cannot queue buffers.");
        return true;
    }

    // Queue all available buffers
    for (int i = 0; i < numBuffers; i++)
    {
        CLEAR(buf);
        buf.type = bufferType;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        // Query buffer status first to check its state
        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) == -1)
        {
            common::log_error("Webcam::queueAllBuffersAgain - VIDIOC_QUERYBUF failed for buffer %d", i);
            common::errno_log("Webcam::queueAllBuffersAgain - VIDIOC_QUERYBUF failed");
            continue;
        }

        // Check buffer flags to determine if it can be queued
        if (buf.flags & V4L2_BUF_FLAG_QUEUED)
        {
            continue;
        }

        if (!(buf.flags & V4L2_BUF_FLAG_MAPPED))
        {
            common::log_error("Webcam::queueAllBuffersAgain - Buffer %d is not mapped, cannot queue", i);
            return false;
        }

        // Reset buffer parameters for queueing
        buf.type = bufferType;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.bytesused = 0;

        if (!requeueFrame(buf))
        {
            return false; // If requeuing fails, return false immediately
        }
    }

    return true;
}

bool Webcam::requeueFrame(struct v4l2_buffer& buf)
{
    if (fd_ <= 0)
    {
        common::log_error("Webcam::requeueFrame - Device not open. Skipping requeue for buffer %d", buf.index);
        return true;
    }

    // Check if buffer is already queued
    if (buf.flags & V4L2_BUF_FLAG_QUEUED)
    {
        return true;
    }

    // Ensure buffer is mapped before queueing
    if (!(buf.flags & V4L2_BUF_FLAG_MAPPED))
    {
        common::log_error("Webcam::requeueFrame - Buffer %d is not mapped, cannot queue", buf.index);
        return false;
    }

    if (ioctl(fd_, VIDIOC_QBUF, &buf) == -1)
    {
        switch (errno)
        {
            case EINVAL:
                common::log_error(
                    "Webcam::requeueFrame - %s - Invalid argument for buffer %d (buffer may already be queued "
                    "or parameters invalid)",
                    name_.c_str(), buf.index);
                break;
            case ENOMEM:
                common::log_error("Webcam::requeueFrame - %s - Not enough memory for buffer %d", name_.c_str(),
                                  buf.index);
                break;
            case EIO:
                common::log_error("Webcam::requeueFrame - %s - I/O error for buffer %d", name_.c_str(), buf.index);
                break;
            default:
                common::log_error("Webcam::requeueFrame - %s - Unknown error %d for buffer %d (flags: 0x%x)",
                                  name_.c_str(), errno, buf.index, buf.flags);
                common::errno_log("Webcam::requeueFrame - VIDIOC_QBUF");
                break;
        }
        return false;
    }

    return true;
}
