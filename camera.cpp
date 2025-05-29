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

    close(input_fd_);
    close(output_fd_);
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

bool CameraManager::configureInputCamera(const char* in_device, unsigned int width, unsigned int height)
{
    struct v4l2_capability cap;
    struct v4l2_format format;

    // Get the file descriptor of the video device
    if ((input_fd_ = open(in_device, O_RDWR)) < 0)
    {
        common::errno_log("CameraManager::configureInputCamera - Failed to open device");
        return false;
    }

    if (!getCapabilities(input_fd_, cap))
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
        format.fmt.pix.width = width;
        format.fmt.pix.height = height;

        if (ioctl(input_fd_, VIDIOC_S_FMT, &format) < 0)
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

    if (ioctl(input_fd_, VIDIOC_REQBUFS, &bufrequest_) < 0)
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

        if (ioctl(input_fd_, VIDIOC_QUERYBUF, &buf) < 0)
        {
            common::errno_log("CameraManager::configureInputBuffers - VIDIOC_QUERYBUF");
            cleanupBuffers(i);
            return false;
        }

        buffers_[i].length = buf.length; // Remember for munmap()

        buffers_[i].start = mmap(nullptr,
                                 buf.length, // Get buffer info fills this variable
                                 PROT_READ | PROT_WRITE, MAP_SHARED, input_fd_,
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
        if (-1 == ioctl(input_fd_, VIDIOC_QBUF, &buf))
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
    if (ioctl(input_fd_, VIDIOC_STREAMON, &bufrequest_.type) < 0)
    {
        common::errno_log("CameraManager::configureInputBuffers - VIDIOC_STREAMON Cannot activate streaming.");
        cleanupBuffers(bufrequest_.count);
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

bool CameraManager::record()
{
    recordThread_ = std::thread(
        [this]()
        {
            struct v4l2_buffer buf;

            Image raw_image;
            unsigned int totalDiscarded{0u};
            unsigned int totalDiscardedHeader{0u};
            const unsigned int warmupDiscardCount{2u};

            bool readJPEGHeader{true};
            unsigned int decodingFailureCount{0u};

            while (keepRunning_)
            {
                fd_set fds;
                struct timeval tv;
                int r;

                FD_ZERO(&fds);
                FD_SET(input_fd_, &fds);

                // Timeout.
                tv.tv_sec = 2;
                tv.tv_usec = 0;
                profiler_.start("Waiting for OS camera frame");
                r = select(input_fd_ + 1, &fds, nullptr, nullptr, &tv);

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
                if (ioctl(input_fd_, VIDIOC_DQBUF, &buf) < 0)
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
                    if (!requeueFrame(input_fd_, buf))
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
                        if (!requeueFrame(input_fd_, buf))
                        {
                            break;
                        }
                        continue;
                    }

                    if (raw_image.size() != raw_needed_size)
                    {
                        // This JPEG is diferent from the previous ones. Requires a decoding buffer of different size.
                        raw_image.resize(raw_needed_size);
                        common::log_warn("CameraManager::record - Reallocating raw image buffer to %d - %s",
                                         raw_image.size(), common::format_size(raw_image.size()));
                    }

                    // Disable header reading. Decoding failures can trigger a new header read.
                    readJPEGHeader = false;
                }

                // Decode the image immediately
                if (!jpegManager_->decodeImage(srcImage, raw_image))
                {
                    readJPEGHeader = true;
                    if (!requeueFrame(input_fd_, buf))
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

                // Update raw_image info after successful decode
                raw_image.info = srcImage.info;

                // TODO: FIXME: Here we are cloning all the data!
                imageQueue_.push(raw_image.clone());
                profiler_.stop("Input image decoding");

                if (!requeueFrame(input_fd_, buf))
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

bool CameraManager::stopInputStreaming()
{
    // Deactivate streaming
    if (ioctl(input_fd_, VIDIOC_STREAMOFF, &bufrequest_.type) < 0)
    {
        common::errno_log("CameraManager::record - VIDIOC_STREAMOFF");
        return false;
    }
    return true;
}

bool CameraManager::update(std::function<void(Image&)> paint)
{
    bool success{true};

    // while (keepRunning_)
    // {
    // Get the next image
    Image processed_image;
    bool still_alive = imageQueue_.wait_and_pop(processed_image);
    if (!still_alive)
    {
        // break;
        return false;
    }

    profiler_.start("Processing time");
    // Process the image
    paint(processed_image);
    profiler_.stop("Processing time");


    profiler_.start("Encode and write output image");
    // Encode and send to output
    if (!jpegManager_->encodeAndWriteToOutput(processed_image))
    {
        success = false;
        return false;
        // break;
    }
    profiler_.stop("Encode and write output image");

    // }

    return success;
}

bool CameraManager::configureVirtualOuputCamera(const char* out_device, unsigned int width, unsigned int height)
{
    struct v4l2_format format;

    // Get the file descriptor
    if ((output_fd_ = open(out_device, O_RDWR)) < 0)
    {
        common::errno_log("Error open video out!");
        return false;
    }

    CLEAR(format);

    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    // Get information from output device
    if (ioctl(output_fd_, VIDIOC_G_FMT, &format) < 0)
    {
        common::errno_log("VIDIOC_G_FMT");
        return false;
    }

    // Configure the device
    logFormat(format);

    format.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG; // Si video to video
    // format.fmt.pix.colorspace = V4L2_PIX_FMT_ARGB32;
    // My camera is able to do: 1920x1080 1280x720 640x480 640x360 320x240
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.field = V4L2_FIELD_NONE;

    // Change the device configuration with the new parameters
    if (ioctl(output_fd_, VIDIOC_S_FMT, &format) < 0)
    {
        common::errno_log("VIDIOC_S_FMT");
        return false;
    }

    return true;
}

bool CameraManager::initialize()
{
    if (!configureVirtualOuputCamera("/dev/video8", CAMERA_WIDTH, CAMERA_HEIGHT))
    {
        common::log_error("CameraManager::initialize - Cannot proceed. Aborting.");
        return false;
    }

    if (!configureInputCamera("/dev/video0", CAMERA_WIDTH, CAMERA_HEIGHT))
    {
        common::log_error("CameraManager::initialize - Cannot proceed. Aborting.");
        // Here we should reconfigure the input device?
        return false;
    }

    if (!configureInputBuffers(2))
    {
        common::log_error("CameraManager::initialize - Cannot proceed. Aborting.");
        // Here we should reconfigure the input device?
        return false;
    }

    jpegManager_ = std::make_shared<JPEGManager>(output_fd_, CAMERA_WIDTH, CAMERA_HEIGHT, TJSAMP::TJSAMP_444);

    // Start streaming thread.
    if (!record())
    {
        common::log_error("CameraManager::initialize - Unable to record. Aborting.");
        return false;
    }

    // processingThread_ = std::thread(
    //     [this]()
    //     {

    //     });
    return true;
}
