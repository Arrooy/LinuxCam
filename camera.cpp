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
using namespace funnyface;

// Dlib and this size, we get 20ms paint. 2ms Compress/Decompress.
#define CAMERA_WIDTH 640L
#define CAMERA_HEIGHT 480L

// Definition of the static member
std::atomic<bool> CameraManager::keepRunning_{true};

CameraManager::CameraManager() : profiler_(Profiler::getInstance())
{
}

CameraManager::~CameraManager()
{
    if (recordThread_.joinable())
    {
        recordThread_.join();
    }

    if (processingThread_.joinable())
    {
        processingThread_.join();
    }

    close(inputDevice_.fd);
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

void CameraManager::logSupportedResolutions(int fd, const char* device_name)
{
    struct v4l2_fmtdesc fmtdesc;
    struct v4l2_frmsizeenum frmsize;

    common::log_info("Supported resolutions for device %s:", device_name);

    // Enumerate pixel formats
    CLEAR(fmtdesc);
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;

    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0)
    {
        common::log_info("  Format: %.4s (%s)", (char*) &fmtdesc.pixelformat, fmtdesc.description);

        // Enumerate frame sizes for this format
        CLEAR(frmsize);
        frmsize.pixel_format = fmtdesc.pixelformat;
        frmsize.index = 0;

        while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0)
        {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE)
            {
                common::log_info("    %ux%u", frmsize.discrete.width, frmsize.discrete.height);
            }
            else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE)
            {
                common::log_info("    %ux%u to %ux%u (step %ux%u)", frmsize.stepwise.min_width,
                                 frmsize.stepwise.min_height, frmsize.stepwise.max_width, frmsize.stepwise.max_height,
                                 frmsize.stepwise.step_width, frmsize.stepwise.step_height);
            }
            frmsize.index++;
        }

        fmtdesc.index++;
    }
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

void CameraManager::cleanupBuffers(unsigned int index)
{
    // Free all buffers.
    for (unsigned int i = 0; i < index; i++)
    {
        if (-1 == munmap(buffers_[i].start, buffers_[i].length))
        {
            common::errno_log("CameraManager::cleanupBuffers - munmap failed");
        }
    }
    free(buffers_);
    buffers_ = nullptr;
}

bool CameraManager::requeueFrame(int fd, v4l2_buffer& buf)
{
    if (ioctl(fd, VIDIOC_QBUF, &buf) == -1)
    {
        common::errno_log("CameraManager::requeueFrame - VIDIOC_QBUF");
        return false;
    }
    return true;
}

bool CameraManager::stopInputStreaming()
{
    // Deactivate streaming
    if (ioctl(inputDevice_.fd, VIDIOC_STREAMOFF, &bufrequest_.type) < 0)
    {
        common::errno_log("CameraManager::record - VIDIOC_STREAMOFF");
        return false;
    }
    return true;
}

void CameraManager::configureInputDevice(const char* input_device, unsigned int width, unsigned int height,
                                         unsigned int buffer_count)
{
    inputDevice_.device_path = input_device;
    inputDevice_.width = width;
    inputDevice_.height = height;
    inputDevice_.buffer_count = buffer_count;
    inputDevice_.fd = -1; // Will be set in configureInputCamera
}
void CameraManager::configureOutputDevice(const char* out_device, unsigned int width, unsigned int height)
{
    outputDevice_.device_path = out_device;
    outputDevice_.width = width;
    outputDevice_.height = height;
    outputDevice_.fd = -1; // Will be set in configureOutputCamera
}

bool CameraManager::initialize()
{
    if (inputDevice_.device_path == nullptr)
    {
        common::log_error("CameraManager::initialize - Input device not configured. Aborting.");
        return false;
    }

    if (outputDevice_.device_path == nullptr)
    {
        common::log_error("CameraManager::initialize - Output device not configured. Aborting.");
        return false;
    }

    if (!configureVirtualOuputCamera())
    {
        common::log_error("CameraManager::initialize - Cannot proceed. Aborting.");
        return false;
    }

    if (!configureInputCamera())
    {
        common::log_error("CameraManager::initialize - Cannot proceed. Aborting.");
        // Here we should reconfigure the input device?
        return false;
    }

    if (!configureInputBuffers(inputDevice_.buffer_count))
    {
        common::log_error("CameraManager::initialize - Cannot proceed. Aborting.");
        // TODO:Here we should reconfigure the input device?
        return false;
    }

    jpegManager_ =
        std::make_shared<JPEGManager>(outputDevice_.fd, outputDevice_.width, outputDevice_.height, TJSAMP::TJSAMP_444);

    // Start streaming thread.
    if (!record())
    {
        common::log_error("CameraManager::initialize - Unable to record. Aborting.");
        return false;
    }

    return true;
}

bool CameraManager::configureInputCamera()
{
    struct v4l2_capability cap;
    struct v4l2_format format;

    // Get the file descriptor of the video device
    if ((inputDevice_.fd = open(inputDevice_.device_path, O_RDWR)) < 0)
    {
        common::errno_log("CameraManager::configureInputCamera - Failed to open device");
        return false;
    }

    if (!getCapabilities(inputDevice_.fd, cap))
    {
        return false;
    }

    // Check if we have the required capabilities
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        common::errno_log(
            "CameraManager::configureInputCamera - The device does not handle single-planar video capture.");
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING))
    {
        common::errno_log("CameraManager::configureInputCamera - The device does not handle streaming.");
        return false;
    }

    // Set the video format. Retry 1 times in case of failure
    for (int i = 0; i < 2; i++)
    {
        // Define video format
        CLEAR(format);
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        format.fmt.pix.width = inputDevice_.width;
        format.fmt.pix.height = inputDevice_.height;

        if (ioctl(inputDevice_.fd, VIDIOC_S_FMT, &format) < 0)
        {
            if (errno == EBUSY)
            {
                // Device busy, cloud be streaming... Try to stop the stream and try again
                common::log_warn(
                    "CameraManager::configureInputCamera - Device is busy, trying to stop the stream and try again.");
                bool stop_success = stopInputStreaming();
                if (!stop_success)
                {
                    common::errno_log("CameraManager::configureInputCamera - Error stopping streaming");
                    return false;
                }
            }
            else
            {
                common::errno_log("CameraManager::configureInputCamera - Error configuring input camera");
                return false;
            }
        }
    }

    logSupportedResolutions(inputDevice_.fd, inputDevice_.device_path);

    return true;
}

bool CameraManager::configureVirtualOuputCamera()
{
    struct v4l2_format format;

    common::log_info("CameraManager::configureVirtualOuputCamera - Configuring output device %s",
                     outputDevice_.device_path);
    // Get the file descriptor
    if ((outputDevice_.fd = open(outputDevice_.device_path, O_RDWR)) < 0)
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


bool CameraManager::configureInputBuffers(unsigned int buffer_count)
{
    struct v4l2_buffer buf;

    // Inform about the buffering system used
    // Note that DMA could be used, see ->
    // https://www.linuxtv.org/downloads/v4l-dvb-apis-new/userspace-api/v4l/dmabuf.html

    CLEAR(bufrequest_);
    bufrequest_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufrequest_.memory = V4L2_MEMORY_MMAP;
    bufrequest_.count = buffer_count;

    if (ioctl(inputDevice_.fd, VIDIOC_REQBUFS, &bufrequest_) < 0)
    {
        common::errno_log("CameraManager::configureInputBuffers - VIDIOC_REQBUFS");
        return false;
    }

    // Check if the SO provides the requested buffers
    if (bufrequest_.count != buffer_count)
    {
        common::errno_log("CameraManager::configureInputBuffers - Not enough buffer memory");
        return false;
    }

    buffers_ = (Buffer*) calloc(bufrequest_.count, sizeof(*buffers_));
    if (!buffers_)
    {
        common::errno_log("CameraManager::configureInputBuffers - Out of memory when creating buffers");
        return false;
    }

    // Allocate data for the buffers
    for (unsigned int i = 0; i < bufrequest_.count; i++)
    {
        // Get the buffer info
        CLEAR(buf);

        buf.type = bufrequest_.type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i; // Queueing buffer index i.

        if (ioctl(inputDevice_.fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            common::errno_log("CameraManager::configureInputBuffers - VIDIOC_QUERYBUF");
            cleanupBuffers(i);
            return false;
        }

        buffers_[i].length = buf.length; // Remember for munmap()

        buffers_[i].start = mmap(nullptr,
                                 buf.length, // Get buffer info fills this variable
                                 PROT_READ | PROT_WRITE, MAP_SHARED, inputDevice_.fd,
                                 buf.m.offset // Get buffer info fills this variable
        );

        if (buffers_[i].start == MAP_FAILED)
        {
            common::errno_log("CameraManager::configureInputBuffers - MMAP Failed");
            cleanupBuffers(i);
            return false;
        }

        // Delete the garbage frame, make all black
        memset(buffers_[i].start, 0, buffers_[i].length);

        // Put the buffer in the incoming queue.
        if (-1 == ioctl(inputDevice_.fd, VIDIOC_QBUF, &buf))
        {
            common::errno_log("CameraManager::configureInputBuffers - VIDIOC_QBUF");
            cleanupBuffers(i);
            return false;
        }
    }

    // Register the Ctrl-c signal to stop the program with no memory leaks.
    std::signal(SIGINT, CameraManager::intHandler);
    // std::signal(SIGSEGV, CameraManager::cleanupAndExit);

    // Activate streaming
    if (ioctl(inputDevice_.fd, VIDIOC_STREAMON, &bufrequest_.type) < 0)
    {
        common::errno_log("CameraManager::configureInputBuffers - VIDIOC_STREAMON Cannot activate streaming.");
        cleanupBuffers(bufrequest_.count);
        return false;
    }
    return true;
}

bool CameraManager::record()
{
    recordThread_ = std::thread(
        [this]()
        {
            struct v4l2_buffer buf;
            unsigned int totalDiscarded{0u};
            unsigned int totalDiscardedHeader{0u};
            const unsigned int warmupDiscardCount{2u};

            bool readJPEGHeader{true};
            unsigned int decodingFailureCount{0u};

            TJImageDescription cameraInputInfo;
            while (keepRunning_)
            {
                fd_set fds;
                struct timeval tv;
                int r;

                FD_ZERO(&fds);
                FD_SET(inputDevice_.fd, &fds);

                // Timeout.
                tv.tv_sec = 2;
                tv.tv_usec = 0;
                profiler_.start("Waiting for OS camera frame");
                r = select(inputDevice_.fd + 1, &fds, nullptr, nullptr, &tv);

                if (r == -1)
                {
                    if (EINTR == errno)
                    {
                        continue;
                    }
                    common::errno_log("CameraManager::record - Select failed");
                    break;
                }

                if (r == 0)
                {
                    common::errno_log("CameraManager::record - Select timeout in main loop");
                    break;
                }

                // Read frame
                CLEAR(buf);
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;

                // The buffer's waiting in the outgoing queue
                if (ioctl(inputDevice_.fd, VIDIOC_DQBUF, &buf) < 0)
                {
                    if (errno == EAGAIN || errno == EIO)
                    {
                        continue;
                    }
                    else
                    {
                        common::errno_log("CameraManager::record - VIDIOC_QBUF");
                        break;
                    }
                }

                if (buf.index >= bufrequest_.count)
                {
                    common::errno_log("CameraManager::record - INVALID INDEX in BUFF. Aborting.");
                    break;
                }

                profiler_.stop("Waiting for OS camera frame");

                // If we attached streaming mid frame, probably the fist buffer or two will be invalid.
                if (totalDiscarded < warmupDiscardCount)
                {
                    totalDiscarded++;
                    // re-queue the frame
                    if (!requeueFrame(inputDevice_.fd, buf))
                    {
                        break;
                    }
                    continue;
                }

                profiler_.start("Input image decoding");

                // Use V4L2 buffer directly with non-owning reference
                Image srcImage(static_cast<unsigned char*>(buffers_[buf.index].start), buf.bytesused, false);
                srcImage.info.TJPixelFormat = TJPF_RGB; // TODO: This depends on the input camera.

                if (readJPEGHeader)
                {
                    // Get JPEG metadata
                    unsigned long raw_needed_size;
                    bool valid_image = jpegManager_->decodeJPEGHeader(srcImage, raw_needed_size);
                    if (!valid_image)
                    {
                        common::log_info("CameraManager::record - Invalid input image. Discarted. Total discarded: %d",
                                         ++totalDiscardedHeader);
                        if (!requeueFrame(inputDevice_.fd, buf))
                        {
                            break;
                        }
                        continue;
                    }

                    if (currentImage_.size() != raw_needed_size)
                    {
                        // This JPEG is diferent from the previous ones. Requires a decoding buffer of different size.
                        currentImage_.resize(raw_needed_size);
                        common::log_warn("CameraManager::record - Reallocating raw image buffer to %d - %s",
                                         currentImage_.size(), common::format_size(currentImage_.size()));
                    }

                    // Store image info for later.
                    cameraInputInfo = srcImage.info;

                    // Disable header reading. Decoding failures can trigger a new header read.
                    readJPEGHeader = false;
                }

                // Check if we are already processing a buffer
                if (currentImage_.beingUsed_)
                {
                    // Skip this frame, we are already processing a previous one.
                    if (!requeueFrame(inputDevice_.fd, buf))
                    {
                        break;
                    }
                    continue;
                }

                // Decode the image immediately
                if (!jpegManager_->decodeImage(srcImage, currentImage_))
                {
                    readJPEGHeader = true;
                    currentImage_.beingUsed_ = false;
                    if (!requeueFrame(inputDevice_.fd, buf))
                    {
                        break;
                    }
                    if (decodingFailureCount > 10)
                    {
                        common::log_error("CameraManager::record - Failed to decode image after %d attemps",
                                          decodingFailureCount);
                        break;
                    }
                    else
                    {
                        decodingFailureCount++;
                        common::log_error("CameraManager::record - Decoding failed %d times", decodingFailureCount);
                        continue;
                    }
                }

                // Update currentImage_ info after successful decode
                currentImage_.info = cameraInputInfo;

                // Image will be consumed by another thread.
                profiler_.stop("Input image decoding");

                if (!requeueFrame(inputDevice_.fd, buf))
                {
                    break;
                }
            }
            common::log_warn("CameraManager::record thread dead.");

            cleanupBuffers(bufrequest_.count);
            stopInputStreaming();
        });

    return true;
}


bool CameraManager::update(std::function<void(Image&)> paint)
{
    bool success{true};

    // while (keepRunning_)
    // {
    // Get the next image

    profiler_.start("Processing time");
    // Process the image
    paint(currentImage_);
    profiler_.stop("Processing time");


    profiler_.start("Encode and write output image");
    // Encode and send to output
    if (!jpegManager_->encodeAndWriteToOutput(currentImage_))
    {
        success = false;
        return false;
        // break;
    }
    currentImage_.beingUsed_ = false;
    profiler_.stop("Encode and write output image");

    // }

    return success;
}
