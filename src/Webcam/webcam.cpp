#include "LinuxFace/webcam.h"

#include <algorithm>
#include <boost/range/algorithm/find.hpp>
#include <cmath>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <utility>

#include "LinuxFace/common.h"

using namespace linuxface;

Webcam::Webcam(std::string name, std::string device_path, const WebcamType type, const unsigned int width,
               const unsigned int height)
    : name(std::move(name)), device_path(std::move(device_path)), type(type)
{
    std::vector<FrameSize> frame_sizes;
    frame_sizes.push_back(FrameSize{width, height, 0, {0u}});
    Format fmt{"Constructor", ImageFormat::UNKNOWN, 0, 0, frame_sizes};
    selected_format = std::make_unique<Format>(fmt);
}

bool Webcam::open()
{
    if (device_path_.empty())
    {
        common::logError("Webcam::open - No device path specified");
        return false;
    }

    if (fd_ >= 0)
    {
        common::logError("Webcam::open - Device already open");
        return false;
    }

    common::logInfo("Webcam::open - Opening device %s, path: %s", name.c_str(), device_path.c_str());

    auto flags = O_RDWR;
    if (type_ == WebcamType::VirtualOutput)
    {
        flags = O_WRONLY;
    }

    if ((fd_ = ::open(device_path.c_str(), flags)) < 0)
    {
        common::errnoLog("Webcam::open - Failed to open device");
        return false;
    }

    return true;
}

// TODO(arroyo): FIXME: When selecting a format, select highest framerate
// allways
bool Webcam::updateDeviceCapabilities()
{
    struct v4l2_capability cap
    {
    };

    CLEAR(cap);

    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1)
    {
        common::errnoLog("Webcam::updateDeviceCapabilities - VIDIOC_QUERYCAP");
        return false;
    }

    // Check if we have the required capabilities
    if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0u)
    {
        common::errnoLog("Webcam::updateDeviceCapabilities - The device does not handle "
                         "single-planar video capture.");
        return false;
    }

    if ((cap.capabilities & V4L2_CAP_STREAMING) == 0u)
    {
        common::errnoLog("Webcam::updateDeviceCapabilities - The device does not handle "
                         "streaming.");
        return false;
    }

    capabilities.driver = reinterpret_cast<char*>(cap.driver);
    capabilities.card = reinterpret_cast<char*>(cap.card);
    capabilities.bus_info = reinterpret_cast<char*>(cap.bus_info);

    if (name_.empty())
    {
        name = capabilities.card + " - " + capabilities.driver;
    }

    struct v4l2_fmtdesc fmtdesc
    {
    };
    CLEAR(fmtdesc);
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    capabilities.formats.clear();

    // Iterate over all formats supported by the device.
    for (fmtdesc.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0; ++fmtdesc.index)
    {
        Format fmt;
        fmt.description = std::string(reinterpret_cast<char*>(fmtdesc.description));
        fmt.sizes.clear();

        if (fmtdesc.pixelformat == V4L2_PIX_FMT_JPEG || fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG)
        {
            fmt.format = ImageFormat::JPEG;
            fmt.pixelformat = fmtdesc.pixelformat;
            common::logInfo("Webcam::updateDeviceCapabilities - Camera supports MJPEG "
                            "format (%s)",
                            fmt.description.c_str());
        }
        else if (fmtdesc.pixelformat == V4L2_PIX_FMT_SGBRG8)
        {
            fmt.format = ImageFormat::SGBRG8;
            fmt.pixelformat = fmtdesc.pixelformat;
            common::logInfo("Webcam::updateDeviceCapabilities - Camera supports Bayer "
                            "format (%s)",
                            fmt.description.c_str());
        }
        else if (fmtdesc.pixelformat == V4L2_PIX_FMT_Z16)
        {
            fmt.format = ImageFormat::DEPTH_Z16;
            fmt.pixelformat = fmtdesc.pixelformat;
            common::logInfo("Webcam::updateDeviceCapabilities - Camera supports Z16 format "
                            "(%s)",
                            fmt.description.c_str());
        }
        else if (fmtdesc.pixelformat == V4L2_PIX_FMT_UYVY)
        {
            fmt.format = ImageFormat::UYUV422;
            fmt.pixelformat = fmtdesc.pixelformat;
            common::logInfo("Webcam::updateDeviceCapabilities - Camera supports UYUV422 "
                            "format (%s)",
                            fmt.description.c_str());
        }
        else if (fmtdesc.pixelformat == V4L2_PIX_FMT_YUYV)
        {
            fmt.format = ImageFormat::YUYV422;
            fmt.pixelformat = fmtdesc.pixelformat;
            common::logInfo("Webcam::updateDeviceCapabilities - Camera supports YUYV422 "
                            "format (%s)",
                            fmt.description.c_str());
        }

        else
        {
            // TODO(arroyo): Add support for 8-bit Greyscale
            // TODO(arroyo): Add support for Y/UV 4:2:0
            // TODO(arroyo): Some cameras provide metadata instead of video.
            // maybe we can support that also
            common::logWarn("Webcam::updateDeviceCapabilities - Camera supports unknown "
                            "format: %s",
                            fmt.description.c_str());
            continue; // Skip unsupported formats
        }

        // Query available frame sizes for the current format
        struct v4l2_frmsizeenum frmsize
        {
        };
        CLEAR(frmsize);
        frmsize.pixel_format = fmtdesc.pixelformat;

        for (frmsize.index = 0; ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0; ++frmsize.index)
        {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE)
            {
                FrameSize size;
                size.width = frmsize.discrete.width;
                size.height = frmsize.discrete.height;
                size.fps.clear();

                struct v4l2_frmivalenum frmival
                {
                };
                CLEAR(frmival);
                frmival.pixel_format = fmtdesc.pixelformat;
                frmival.width = frmsize.discrete.width;
                frmival.height = frmsize.discrete.height;

                for (frmival.index = 0; ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) == 0; ++frmival.index)
                {
                    if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE)
                    {
                        int fps = static_cast<int>(static_cast<double>(frmival.discrete.denominator)
                                                   / static_cast<double>(frmival.discrete.numerator));

                        if (boost::range::find(size.fps,, fps) == size.fps.end())
                        {
                            size.fps.push_back(fps); // Only insert if not already present
                        }
                    }
                }

                if (boost::range::find(fmt.sizes,, size) == fmt.sizes.end())
                {
                    fmt.sizes.push_back(size); // No duplicate width/height/fps
                }
            }
        }

        capabilities.formats.push_back(fmt);
    }

    if (capabilities_.formats.empty())
    {
        common::logWarn("Webcam::UpdateDeviceCapabilities - This input device does not "
                        "have any capabilities for "
                        "V4L2_BUF_TYPE_VIDEO_CAPTURE.");
        return false;
    }

    // Select the format that best adapts to user requirements.
    selectBestFormat();
    return true;
}

void Webcam::selectBestFormat()
{
    Format* best_format = nullptr;
    const unsigned int best_index = 0;
    const unsigned int best_fps_index = 0;

    const double best_distance = std::numeric_limits<double>::max();

    for (auto& fmt : capabilities_.formats)
    {
        if (fmt.sizes.empty())
        {
            // Skip formats without frame sizes.
            continue;
        }

        if (selectBestFormat && selectedFormat_->format != ImageFormat::UNKNOWN)
        {
            // Skip formats that don't match the desired format.
            if (selectBestFormat->format != fmt.format)
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

    if (best_format != nullptr)
    {
        auto& selected_size = best_format->sizes[best_index];
        best_format->selectedFrameSize = best_index;
        selected_size.selectedFPS = best_fps_index;

        common::logInfo("Webcam: Selected format is %s with frame size of %dx%d (%d FPS). And with pixel format %u",
                         best_format->description.c_str(), selected_size.width, selected_size.height,
                         selected_size.getFps(selected_size.selectedFPS), best_format->pixelformat);
        selectedFormat_ = std::make_unique<Format>(*bestFormat);
    }
    else
    {
        // Fallback: select the first format if nothing else works
        auto& elected_size = capabilities_.formats[0].sizes[0];
        capabilities_.formats[0].selectedFrameSize = 0u;
        elected_size.selectedFPS = 0u;
        common::logError("Webcam: No suitable format found, selecting first one of %dx%d and %d FPS",
                          electedSize.width, electedSize.height, electedSize.getFps(electedSize.selectedFPS));
        selectedFormat_ = std::make_unique<Format>(capabilities_.formats[0]);
    }
}

std::tuple<unsigned int, unsigned int, double> Webcam::findBestFrameSize(const Format& fmt)
{
    if (!selectedFormat_ || selectedFormat_->sizes[selectedFormat_->selectedFrameSize].width == 0
        || selectedFormat_->sizes[selectedFormat_->selectedFrameSize].height == 0)
    {
        // No selected format, select the central size
        common::logWarn("Webcam::findBestFrameSize - No selected format, selecting central size");
        return {fmt.sizes.size() / 2, 0, 0.0}; // Central size, highest priority
    }

    unsigned int best_index = 0;
    unsigned int best_fps_index = 0;

    double best_distance = std::numeric_limits<double>::max();

    for (unsigned int i = 0; i < fmt.sizes.size(); ++i)
    {
        const auto& size = fmt.sizes[i];

        const auto& selected_size = selectedFormat_->sizes[selectedFormat_->selectedFrameSize];
        unsigned int desired_width = selectedSize.width;
        unsigned int desired_height = selectedSize.height;
        unsigned int desired_fps = 0;

        // Check that the desired FPS appears in the format (ignore for 0fps)
        if (selected_size.fps.size() > selectedSize.selectedFPS)
        {
            desired_fps = selectedSize.getFps(selectedSize.selectedFPS);
            if (desired_fps != 0 && std::find(size.fps.begin(), size.fps.end(), desired_fps) == size.fps.end())
            {
                // Skip this format
                continue;
            }
        }
        const double distance = calculateDistance(size.width, size.height, desired_width, desired_height);

        if (distance <= best_distance)
        {
            best_distance = distance;
            best_index = i;
            // compute the index of the max value inside vector (Allways select best fps)
            common::logInfo("Found a good distance and fps desired is %d", selectedSize.selectedFPS);

            if (desired_fps == 0)
            {
                // Select the best fps
                best_fps_index = std::distance(size.fps.begin(), std::max_element(size.fps.begin(), size.fps.end()));
            }
            else
            {
                best_fps_index = selectedSize.selectedFPS;
            }
        }
    }

    return {best_index, best_fps_index, best_distance};
}

double Webcam::calculateDistance(unsigned int width1, unsigned int height1, unsigned int width2, unsigned int height2)
{
    const double dx = static_cast<double>(width1) - static_cast<double>(width2);
    const double dy = static_cast<double>(height1) - static_cast<double>(height2);
    return std::sqrt((dx * dx) + (dy * dy));
}

bool Webcam::configureDeviceFormat()
{
    struct v4l2_format format
    {
    };

    // Set the video format with retry mechanism
    for (int i = 0; i < 2; i++)
    {
        CLEAR(format);
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.pixelformat = selectedFormat_->pixelformat;
        FrameSize frame_size = selectedFormat_->sizes[selectedFormat_->selectedFrameSize];
        format.fmt.pix.width = frame_size.width;
        format.fmt.pix.height = frame_size.height;
        format.fmt.pix.field = V4L2_FIELD_NONE;
        common::logInfo("WebCam::configureDeviceFormat - %s - Trying to set format: pixfmt=%d, width=%d, height=%d",
                         name_.c_str(), format.fmt.pix.pixelformat, format.fmt.pix.width, format.fmt.pix.height);

        if (ioctl(fd_, VIDIOC_S_FMT, &format) < 0)
        {
            if (errno == EBUSY)
            {
                common::logWarn("Webcam::configureDeviceFormat - Device is busy, trying again later.");
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

    // Removed since it can messup the camera. not worthit.
    // Select FPS.
    // struct v4l2_streamparm streamparm;
    // CLEAR(streamparm);
    // streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    // if (ioctl(fd_, VIDIOC_G_PARM, &streamparm) == -1)
    // {
    //     common::errno_log("ConfigureDeviceFormat - VIDIOC_G_PARM");
    //     return false;
    // }

    // streamparm.parm.capture.timeperframe.numerator = 1;
    // const auto& selectedSize = selectedFormat_->sizes[selectedFormat_->selectedFrameSize];
    // streamparm.parm.capture.timeperframe.denominator = selectedSize.getFps(selectedSize.selectedFPS);
    // common::logError("Setting fps to %d", streamparm.parm.capture.timeperframe.denominator);
    // if (ioctl(fd_, VIDIOC_S_PARM, &streamparm) == -1)
    // {
    //     common::errno_log("ConfigureDeviceFormat - VIDIOC_S_PARM");
    //     return false;
    // }

    return true;
}

bool Webcam::queueAllBuffersAgain(int num_buffers, int buffer_type)
{
    struct v4l2_buffer buf
    {
    };

    if (fd_ <= 0)
    {
        common::logInfo("Webcam::queueAllBuffersAgain - Device not open. Cannot queue buffers.");
        return true;
    }

    // Queue all available buffers
    for (int i = 0; i < num_buffers; i++)
    {
        CLEAR(buf);
        buf.type = buffer_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        // Query buffer status first to check its state
        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) == -1)
        {
            common::logError("Webcam::queueAllBuffersAgain - VIDIOC_QUERYBUF failed for buffer %d", i);
            common::errno_log("Webcam::queueAllBuffersAgain - VIDIOC_QUERYBUF failed");
            continue;
        }

        // Check buffer flags to determine if it can be queued
        if ((buf.flags & V4L2_BUF_FLAG_QUEUED) != 0u)
        {
            continue;
        }

        if ((buf.flags & V4L2_BUF_FLAG_MAPPED) == 0u)
        {
            common::logError("Webcam::queueAllBuffersAgain - Buffer %d is not mapped, cannot queue", i);
            return false;
        }

        // Reset buffer parameters for queueing
        buf.type = buffer_type;
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
        common::logError("Webcam::requeueFrame - Device not open. Skipping requeue for buffer %d", buf.index);
        return true;
    }

    // Check if buffer is already queued
    if ((buf.flags & V4L2_BUF_FLAG_QUEUED) != 0u)
    {
        return true;
    }

    // Ensure buffer is mapped before queueing
    if ((buf.flags & V4L2_BUF_FLAG_MAPPED) == 0u)
    {
        common::logError("Webcam::requeueFrame - Buffer %d is not mapped, cannot queue", buf.index);
        return false;
    }

    if (ioctl(fd_, VIDIOC_QBUF, &buf) == -1)
    {
        switch (errno)
        {
            case EINVAL:
                common::logError(
                    "Webcam::requeueFrame - %s - Invalid argument for buffer %d (buffer may already be queued "
                    "or parameters invalid)",
                    name_.c_str(), buf.index);
                break;
            case ENOMEM:
                common::logError("Webcam::requeueFrame - %s - Not enough memory for buffer %d", name_.c_str(),
                                  buf.index);
                break;
            case EIO:
                common::logError("Webcam::requeueFrame - %s - I/O error for buffer %d", name_.c_str(), buf.index);
                break;
            default:
                common::logError("Webcam::requeueFrame - %s - Unknown error %d for buffer %d (flags: 0x%x)",
                                  name_.c_str(), errno, buf.index, buf.flags);
                common::errno_log("Webcam::requeueFrame - VIDIOC_QBUF");
                break;
        }
        return false;
    }

    return true;
}
