#include "camera.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include <cstdlib>

#include "common.h"
#include "dlibDetectors.h"

using namespace funnyface;

// Definition of the static member
std::atomic<bool> CameraManager::keepRunning_{true};

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

    CLEAR(cap);

    // Retrieve device capabilities
    if (ioctl(input_fd_, VIDIOC_QUERYCAP, &cap) < 0)
    {
        common::errno_log("CameraManager::configureInputCamera - VIDIOC_QUERYCAP");
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

    // Define video format
    CLEAR(format);
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;

    if (ioctl(input_fd_, VIDIOC_S_FMT, &format) < 0)
    {
        common::errno_log("CameraManager::configureInputCamera - VIDIOC_S_FMT");
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
            cleanup_buffers(i);
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
            cleanup_buffers(i);
            return false;
        }

        // Delete the garbage frame, make all black
        memset(buffers_[i].start, 0, buffers_[i].length);

        // Put the buffer in the incoming queue.
        if (-1 == ioctl(input_fd_, VIDIOC_QBUF, &buf))
        {
            common::errno_log("CameraManager::configureInputBuffers - VIDIOC_QBUF");
            cleanup_buffers(i);
            return false;
        }
    }

    // Register the Ctrl-c signal to stop the program with no memory leaks.
    signal(SIGINT, CameraManager::intHandler);

    // Activate streaming
    if (ioctl(input_fd_, VIDIOC_STREAMON, &bufrequest_.type) < 0)
    {
        common::errno_log("CameraManager::configureInputBuffers - VIDIOC_STREAMON Cannot activate streaming.");
        cleanup_buffers(bufrequest_.count);
        return false;
    }
    return true;
}

void CameraManager::cleanup_buffers(unsigned int index)
{
    // Free all buffers.
    for (unsigned int i = 0; i < index; i++)
    {
        if (-1 == munmap(buffers_[i].start, buffers_[i].length))
        {
            common::errno_log("CameraManager::cleanup_buffers - munmap failed");
        }
    }
    free(buffers_);
    buffers_ = nullptr;
}

bool CameraManager::configureFaceDetector()
{
    faceDetector_ptr_ = std::make_shared<DlibFaceDetector>();
    return true;
}

bool CameraManager::update(std::function<void(Image&, std::shared_ptr<FaceDetector>)> paint)
{
    struct v4l2_buffer buf;
    keepRunning_ = true;

    Image raw_image;
    raw_image.data = nullptr;
    raw_image.size = 0L;
    bool success{true};
    unsigned int total_discarded{0u};

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

        r = select(input_fd_ + 1, &fds, nullptr, nullptr, &tv);

        if (r == -1)
        {
            if (EINTR == errno)
            {
                continue;
            }
            common::errno_log("CameraManager::update - Select failed");
            success = false;
            break;
        }

        if (r == 0)
        {
            common::errno_log("CameraManager::update - Select timeout in main loop");
            success = false;
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
                common::errno_log("CameraManager::update - VIDIOC_QBUF");
                success = false;
                break;
            }
        }

        if (buf.index >= bufrequest_.count)
        {
            common::errno_log("CameraManager::update - INVALID INDEX in BUFF. Aborting.");
            success = false;
            break;
        }

        const auto& start_decoding = std::chrono::high_resolution_clock::now();
        // Convert to Image and get the header info
        Image srcImage;
        srcImage.data = static_cast<unsigned char*>(buffers_[buf.index].start);
        srcImage.size = buf.bytesused;
        srcImage.info.TJPixelFormat = TJPF_RGB;

        unsigned long raw_needed_size;
        bool valid_image = jpegManager_->decodeJPEGHeader(srcImage, raw_needed_size);
        if (!valid_image)
        {
            success = false;
            common::log_info("Invalid input image. Discarted. Total discarded: %d", ++total_discarded);
            continue;
        }

        total_discarded = 0;

        if (raw_needed_size != raw_image.size || raw_image.data == nullptr)
        {
            raw_image.data = static_cast<unsigned char*>(malloc(sizeof(unsigned char) * raw_needed_size));
            raw_image.size = raw_needed_size;
            common::log_info("Reallocating raw image buffer to %d - %s", raw_needed_size,
                             common::format_size(raw_needed_size));
        }

        // Decode the image
        if (!jpegManager_->decodeImage(srcImage, &raw_image.data))
        {
            success = false;
            break;
        }

        raw_image.info = srcImage.info;

        const auto& end_decoding = std::chrono::high_resolution_clock::now();

        const auto& start_paint = std::chrono::high_resolution_clock::now();
        // Process the image
        paint(raw_image, faceDetector_ptr_);
        const auto& end_paint = std::chrono::high_resolution_clock::now();


        const auto& start_encoding = std::chrono::high_resolution_clock::now();
        // Encode and send to output
        if (!jpegManager_->encodeAndWriteToOutput(raw_image))
        {
            success = false;
            break;
        }
        const auto& end_encoding = std::chrono::high_resolution_clock::now();

        std::string decoding_time = common::format_duration(start_decoding, end_decoding);
        std::string paint_time = common::format_duration(start_paint, end_paint);
        std::string encoding_time = common::format_duration(start_encoding, end_encoding);

        common::log_info("Decoding %s - Paint duration: %s - Encoding %s", decoding_time.c_str(), paint_time.c_str(),
                         encoding_time.c_str());


        // Add the frame to the queue
        if (ioctl(input_fd_, VIDIOC_QBUF, &buf) == -1)
        {
            common::errno_log("CameraManager::update - VIDIOC_QBUF");
            success = false;
            break;
        }
    }

    // Deactivate streaming
    if (ioctl(input_fd_, VIDIOC_STREAMOFF, &bufrequest_.type) < 0)
    {
        common::errno_log("CameraManager::update - VIDIOC_STREAMOFF");
        success = false;
    }

    free(raw_image.data);
    cleanup_buffers(bufrequest_.count);
    close(input_fd_);
    close(output_fd_);
    return success;
}

bool CameraManager::configureVirtualOuputCamera(const char* out_device, unsigned int width, unsigned int height)
{
    struct v4l2_format format;

    // Get the file descriptor
    if ((output_fd_ = open(out_device, O_WRONLY)) < 0)
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

    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG; // Si video to video
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

CameraManager::CameraManager()
{
}
