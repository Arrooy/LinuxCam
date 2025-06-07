#if 0
#include "FunnyFace/InputDeviceContext.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "FunnyFace/common.h"

using namespace funnyface;

InputDeviceContext::InputDeviceContext()
{
    image_.setBeingUsed(false);
}

InputDeviceContext::~InputDeviceContext()
{
    cleanup();
}

bool InputDeviceContext::setupDevice(const CapturingDevice& device)
{
    // Validate device parameters
    if (device.device_path.empty() || device.width == 0 || device.height == 0 || device.buffer_count == 0)
    {
        common::log_error("InputDeviceContext::setupDevice - Invalid device parameters");
        return false;
    }

    // Set up the device configuration
    device_ = device;
    device_.fd = -1;

    // Open the camera
    if (!openCamera())
    {
        common::log_error("InputDeviceContext::setupDevice - Failed to open camera");
        return false;
    }

    // Configure camera format
    if (!configureCameraFormat())
    {
        common::log_error("InputDeviceContext::setupDevice - Failed to configure camera format");
        return false;
    }

    // Configure buffers
    if (!configureBuffers())
    {
        common::log_error("InputDeviceContext::setupDevice - Failed to configure buffers");
        return false;
    }

    if (!startStreaming())
    {
        common::log_error("InputDeviceContext::setupDevice - Failed to start streaming");
        cleanup();
        return false;
    }

    common::log_info("InputDeviceContext::setupDevice - Successfully set up device %s", device_.device_path.c_str());
    return true;
}

bool InputDeviceContext::reconfigureDevice(const CapturingDevice& device)
{
    // Stop current recording
    stopRecording();

    // Close and cleanup current configuration
    if (device_.fd >= 0)
    {
        close(device_.fd);
        device_.fd = -1;
    }
    cleanupBuffers();

    // Update settings
    device_ = device;
    device_.fd = -1;

    if (!setupDevice(device_))
    {
        common::log_error("InputDeviceContext::reconfigureDevice - Failed to reconfigure device %s",
                          device_.device_path.c_str());
        return false;
    }
    common::log_info("InputDeviceContext::reconfigure - Successfully reconfigured device %s",
                     device_.device_path.c_str());
    return true;
}

bool InputDeviceContext::openCamera()
{
    common::log_info("InputDeviceContext::openCamera - Opening camera %s, path: %s", device_.name.c_str(),
                     device_.device_path.c_str());

    if ((device_.fd = open(device_.device_path.c_str(), O_RDWR)) < 0)
    {
        common::errno_log("InputDeviceContext::openCamera - Failed to open device");
        return false;
    }

    if (!getCameraCapabilities())
    {
        common::errno_log("InputDeviceContext::openCamera - Failed to get camera capabilities");
        return false;
    }

    return true;
}

bool InputDeviceContext::configureCameraFormat()
{
    struct v4l2_format format;

    // Set the video format with retry mechanism
    for (int i = 0; i < 2; i++)
    {
        CLEAR(format);
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        format.fmt.pix.width = device_.width;
        format.fmt.pix.height = device_.height;

        if (ioctl(device_.fd, VIDIOC_S_FMT, &format) < 0)
        {
            if (errno == EBUSY)
            {
                common::log_warn(
                    "InputDeviceContext::configureCameraFormat - Device is busy, trying to stop stream and retry.");
                if (!stopStreaming())
                {
                    common::errno_log("InputDeviceContext::configureCameraFormat - Error stopping streaming");
                    return false;
                }
            }
            else
            {
                common::errno_log("InputDeviceContext::configureCameraFormat - Error configuring camera format");
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

bool InputDeviceContext::configureBuffers()
{
    struct v4l2_buffer buf;

    CLEAR(bufrequest);
    bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufrequest.memory = V4L2_MEMORY_MMAP;
    bufrequest.count = device_.buffer_count;

    if (ioctl(device_.fd, VIDIOC_REQBUFS, &bufrequest) < 0)
    {
        common::errno_log("InputDeviceContext::configureBuffers - VIDIOC_REQBUFS");
        return false;
    }

    if (bufrequest.count != device_.buffer_count)
    {
        common::errno_log("InputDeviceContext::configureBuffers - Not enough buffer memory");
        return false;
    }

    buffers = (Buffer*) calloc(bufrequest.count, sizeof(*buffers));
    if (!buffers)
    {
        common::errno_log("InputDeviceContext::configureBuffers - Out of memory when creating buffers");
        return false;
    }

    // Allocate and configure buffers
    for (unsigned int i = 0; i < bufrequest.count; i++)
    {
        CLEAR(buf);
        buf.type = bufrequest.type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(device_.fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            common::errno_log("InputDeviceContext::configureBuffers - VIDIOC_QUERYBUF");
            cleanupBuffers();
            return false;
        }

        buffers[i].length = buf.length;
        buffers[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, device_.fd, buf.m.offset);

        if (buffers[i].start == MAP_FAILED)
        {
            common::errno_log("InputDeviceContext::configureBuffers - MMAP Failed");
            cleanupBuffers();
            return false;
        }

        memset(buffers[i].start, 0, buffers[i].length);

        if (ioctl(device_.fd, VIDIOC_QBUF, &buf) == -1)
        {
            common::errno_log("InputDeviceContext::configureBuffers - VIDIOC_QBUF");
            cleanupBuffers();
            return false;
        }
    }

    return true;
}

bool InputDeviceContext::startStreaming()
{
    if (ioctl(device_.fd, VIDIOC_STREAMON, &bufrequest.type) < 0)
    {
        common::errno_log("InputDeviceContext::startStreaming - VIDIOC_STREAMON");
        cleanup();
        return false;
    }

    return true;
}

bool InputDeviceContext::stopStreaming()
{
    if (device_.fd >= 0)
    {
        if (ioctl(device_.fd, VIDIOC_STREAMOFF, &bufrequest.type) < 0)
        {
            common::errno_log("InputDeviceContext::stopStreaming - VIDIOC_STREAMOFF");
            return false;
        }
    }
    return true;
}

void InputDeviceContext::cleanup()
{
    stopStreaming();
    stopRecording();
    cleanupBuffers();

    if (device_.fd >= 0)
    {
        close(device_.fd);
        device_.fd = -1;
    }
}

bool InputDeviceContext::startRecording(std::shared_ptr<JPEGManager> jpegManager, Profiler& profiler)
{
    if (isRecording.load())
    {
        return false; // Already recording
    }

    isRecording = true;
    recordThread = std::thread(&InputDeviceContext::recordingLoop, this, jpegManager, std::ref(profiler));
    return true;
}

void InputDeviceContext::stopRecording()
{
    isRecording = false;

    if (recordThread.joinable())
    {
        recordThread.join();
    }
}

Image* InputDeviceContext::getCurrentImage()
{
    return &image_;
}

void InputDeviceContext::logFormat(v4l2_format vid_format)
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


bool InputDeviceContext::getCameraCapabilities()
{
    struct v4l2_capability cap;
    CLEAR(cap);
    if (ioctl(device_.fd, VIDIOC_QUERYCAP, &cap) == -1)
    {
        common::errno_log("InputDeviceContext::getCameraCapabilities - VIDIOC_QUERYCAP");
        return false;
    }

    // Check if we have the required capabilities
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        common::errno_log(
            "InputDeviceContext::getCameraCapabilities - The device does not handle single-planar video capture.");
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING))
    {
        common::errno_log("InputDeviceContext::getCameraCapabilities - The device does not handle streaming.");
        return false;
    }

    device_.caps.driver = reinterpret_cast<char*>(cap.driver);
    device_.caps.card = reinterpret_cast<char*>(cap.card);
    device_.caps.bus_info = reinterpret_cast<char*>(cap.bus_info);

    struct v4l2_fmtdesc fmtdesc;
    CLEAR(fmtdesc);
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    device_.caps.formats.clear();
    for (fmtdesc.index = 0; ioctl(device_.fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0; ++fmtdesc.index)
    {
        Format fmt;
        fmt.description = reinterpret_cast<char*>(fmtdesc.description);
        fmt.pixelformat = fmtdesc.pixelformat;

        struct v4l2_frmsizeenum frmsize;
        CLEAR(frmsize);
        frmsize.pixel_format = fmtdesc.pixelformat;

        for (frmsize.index = 0; ioctl(device_.fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0; ++frmsize.index)
        {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE)
            {
                FrameSize size;
                size.width = frmsize.discrete.width;
                size.height = frmsize.discrete.height;
                fmt.sizes.push_back(size);
            }
        }

        device_.caps.formats.push_back(fmt);
    }

    return true;
}

void InputDeviceContext::recordingLoop(std::shared_ptr<JPEGManager> jpegManager, Profiler& profiler)
{
    struct v4l2_buffer buf;
    unsigned int totalDiscarded{0u};
    unsigned int totalDiscardedHeader{0u};
    const unsigned int warmupDiscardCount{2u};

    image_.setBeingUsed(false);

    while (isRecording.load())
    {
        fd_set fds;
        struct timeval tv;
        int r;

        FD_ZERO(&fds);
        FD_SET(device_.fd, &fds);

        tv.tv_sec = 2;
        tv.tv_usec = 0;
        profiler.start("1", "Waiting for OS camera frame");
        r = select(device_.fd + 1, &fds, nullptr, nullptr, &tv);

        if (r == -1)
        {
            if (EINTR == errno)
            {
                continue;
            }
            common::errno_log("InputDeviceContext::recordingLoop - Select failed");
            break;
        }

        if (r == 0)
        {
            common::errno_log("InputDeviceContext::recordingLoop - Select timeout");
            break;
        }

        if (isRecording == false)
        {
            common::log_info("InputDeviceContext::recordingLoop - Recording stopped, exiting loop");
            break;
        }

        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(device_.fd, VIDIOC_DQBUF, &buf) < 0)
        {
            if (errno == EAGAIN || errno == EIO)
            {
                continue;
            }
            else
            {
                common::errno_log("InputDeviceContext::recordingLoop - VIDIOC_DQBUF");
                break;
            }
        }

        if (buf.index >= bufrequest.count)
        {
            common::errno_log("InputDeviceContext::recordingLoop - INVALID INDEX in BUFF. Aborting.");
            break;
        }

        profiler.stop("1", "Waiting for OS camera frame");

        if (totalDiscarded < warmupDiscardCount)
        {
            totalDiscarded++;
            if (!requeueFrame(buf))
            {
                break;
            }
            continue;
        }

        profiler.start("1", "Input image decoding");

        Image srcImage(static_cast<unsigned char*>(buffers[buf.index].start), buf.bytesused, false);
        srcImage.info.TJPixelFormat = TJPF_RGB;

        if (readJPEGHeader)
        {
            unsigned long raw_needed_size;
            bool valid_image = jpegManager->decodeJPEGHeader(srcImage, raw_needed_size);
            if (!valid_image)
            {
                common::log_info(
                    "InputDeviceContext::recordingLoop - Invalid input image. Discarded. Total discarded: %d",
                    ++totalDiscardedHeader);
                if (!requeueFrame(buf))
                {
                    break;
                }
                continue;
            }

            if (image_.size() != raw_needed_size)
            {
                image_.resize(raw_needed_size);
                common::log_warn("InputDeviceContext::recordingLoop - Reallocating raw image buffer to %lu - %s",
                                 image_.size(), common::format_size(image_.size()));
            }

            cameraInputInfo = srcImage.info;
            readJPEGHeader = false;
        }

        if (image_.getBeingUsed())
        {
            if (!requeueFrame(buf))
            {
                break;
            }
            continue;
        }

        if (!jpegManager->decodeImage(srcImage, image_))
        {
            readJPEGHeader = true;
            image_.setBeingUsed(false);
            if (!requeueFrame(buf))
            {
                break;
            }
            if (decodingFailureCount > 10)
            {
                common::log_error("InputDeviceContext::recordingLoop - Failed to decode image after %d attempts",
                                  decodingFailureCount);
                break;
            }
            else
            {
                decodingFailureCount++;
                common::log_error("InputDeviceContext::recordingLoop - Decoding failed %d times", decodingFailureCount);
                continue;
            }
        }

        decodingFailureCount = 0;
        image_.info = cameraInputInfo;

        profiler.stop("1", "Input image decoding");

        // Process the image
        // TODO:
        if (!requeueFrame(buf))
        {
            break;
        }
    }

    common::log_warn("InputDeviceContext::recordingLoop thread dead for device: %s", device_.device_path.c_str());
    image_.setBeingUsed(true);
    stopStreaming();
}

bool InputDeviceContext::requeueFrame(struct v4l2_buffer& buf)
{
    if (ioctl(device_.fd, VIDIOC_QBUF, &buf) == -1)
    {
        common::errno_log("InputDeviceContext::requeueFrame - VIDIOC_QBUF");
        return false;
    }
    return true;
}

void InputDeviceContext::cleanupBuffers()
{
    if (buffers)
    {
        for (unsigned int i = 0; i < bufrequest.count; i++)
        {
            if (buffers[i].start != MAP_FAILED)
            {
                if (-1 == munmap(buffers[i].start, buffers[i].length))
                {
                    common::errno_log("InputDeviceContext::cleanupBuffers - munmap failed");
                }
            }
        }
        free(buffers);
        buffers = nullptr;
    }
}
#endif
