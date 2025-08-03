#include "LinuxFace/v4l2loopbackWritter.h"

#include "LinuxFace/profiler.h"

using namespace linuxface;


V4L2LoopbackWriter::V4L2LoopbackWriter(const std::string& name, const std::string& devicePath, const unsigned int width,
                                       const unsigned int height, const TJSAMP subsample)
    : Webcam(name, devicePath, WebcamType::VirtualOutput, width, height), streaming_(false)
{
    chrominance_subsampling_ = subsample;
    quality_ = 100;
}

V4L2LoopbackWriter::~V4L2LoopbackWriter()
{
    cleanup();
}

bool V4L2LoopbackWriter::setupDevice()
{
    // 1. Open device
    fd_ = ::open(device_path_.c_str(), O_RDWR);
    if (fd_ < 0)
    {
        common::errno_log("V4L2LoopbackWriter::setupDevice - Open fd failed.");
        return false;
    }

    // 2. Set format
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = getDesiredWidth();
    fmt.fmt.pix.height = getDesiredHeight();
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
        common::errno_log("V4L2LoopbackWriter::setupDevice - VIDIOC_S_FMT");
        cleanup();
        return false;
    }

    // Store the selected format in capabilities
    Format selFmt;
    selFmt.description = "V4L2 custom device";
    selFmt.format = ImageFormat::JPEG;
    selFmt.pixelformat = V4L2_PIX_FMT_MJPEG;
    selFmt.selectedFrameSize = 0;
    selFmt.sizes.push_back(FrameSize{getDesiredWidth(), getDesiredHeight()});
    selectedFormat_ = std::make_unique<Format>(selFmt);

    capabilities_.formats.push_back(selFmt);

    // 3. Request buffers_
    struct v4l2_requestbuffers reqbuf = {0};
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 1; // Single buffer for V4L2 loopback - sufficient for virtual devices

    if (ioctl(fd_, VIDIOC_REQBUFS, &reqbuf) < 0)
    {
        common::errno_log("V4L2LoopbackWriter::setupDevice - VIDIOC_REQBUFS");
        cleanup();
        return false;
    }

    // 4. Map buffers_
    buffers_.resize(reqbuf.count);
    for (unsigned int i = 0; i < reqbuf.count; i++)
    {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0)
        {
            common::errno_log("V4L2LoopbackWriter::setupDevice - VIDIOC_QUERYBUF");
            cleanup();
            return false;
        }

        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);

        if (buffers_[i].start == MAP_FAILED)
        {
            common::errno_log("V4L2LoopbackWriter::setupDevice - mmap");
            cleanup();
            return false;
        }
    }

    // Create encoder
    auto pixelFormat = TJPF_RGB;

    ConfigBuilder configBuilder;
    configBuilder.imageFormat(selectedFormat_->format)
        .pixelFormat(pixelFormat)
        .width(selectedFormat_->sizes[selectedFormat_->selectedFrameSize].width)
        .height(selectedFormat_->sizes[selectedFormat_->selectedFrameSize].height)
        .quality(100)
        .chrominance_subsampling(chrominance_subsampling_);

    encoder_ = CodecFactory::create<Encoder>(configBuilder);
    if (encoder_ == nullptr)
    {
        common::log_error("V4L2LoopbackWriter::setupDevice - Failed to create encoder");
        return false;
    }

    common::log_info("V4L2LoopbackWriter::setupDevice - V4L2 streaming_ initialized successfully");
    return true;
}

bool V4L2LoopbackWriter::start()
{
    if (fd_ >= 0)
    {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0)
        {
            common::errno_log("V4L2LoopbackWriter::start - VIDIOC_STREAMON");
            cleanup();
            return false;
        }
        streaming_ = true;
    }
    return true;
}

bool V4L2LoopbackWriter::stop()
{
    if (fd_ >= 0)
    {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        if (ioctl(fd_, VIDIOC_STREAMOFF, &type) < 0)
        {
            common::errno_log("V4L2LoopbackWriter::stop - VIDIOC_STREAMOFF");
            cleanup();
            return false;
        }

        if (!queueAllBuffersAgain(buffers_.size(), type))
        {
            common::log_error("V4L2LoopbackWriter::stop - Failed to queue all buffers again");
            return false;
        }
    }
    streaming_ = false;
    return true;
}

bool V4L2LoopbackWriter::writeFrame(Image& image)
{
    if (!streaming_)
    {
        common::log_error("Device not initialized for streaming_");
        return false;
    }

    Profiler::getInstance().start(name_.c_str(), "Encode and write output image");

    // Check if we need to scale the image
    std::unique_ptr<Image> scaledImage = nullptr;
    unsigned long desiredWidth = getDesiredWidth();
    unsigned long desiredHeight = getDesiredHeight();
    if (image.info.width != desiredWidth || image.info.height != desiredHeight)
    {
        scaledImage = image.scale(desiredWidth, desiredHeight);
        if (!scaledImage)
        {
            common::log_error("V4L2LoopbackWriter::writeFrame - Failed to scale image");
            Profiler::getInstance().stop(name_.c_str(), "Encode and write output image");
            return false;
        }
    }

    // Use scaled image if available, otherwise use original
    Image& imageToEncode = scaledImage ? *scaledImage : image;

    // 1. Dequeue a buffer (get an available buffer)
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0)
    {
        common::errno_log("V4L2LoopbackWriter::writeFrame - VIDIOC_DQBUF");
        return false;
    }

    const size_t buf_len = buffers_[buf.index].length;

    // Wrap v4l2 buffer to Image so encoder can encode there the image.
    Image v4l2Image = Image((unsigned char*) buffers_[buf.index].start, buf_len, false);

    // Encode image and store it into v4l2 buffer
    unsigned long actualCompressedSize{0u};
    if (!encoder_->encode(imageToEncode, v4l2Image, actualCompressedSize))
    {
        common::log_error("Failed to encode image");
        // Re-queue buffer
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0)
        {
            common::errno_log("V4L2LoopbackWriter::writeFrame - VIDIOC_QBUF");
            return false;
        }
        return false;
    }

    if (actualCompressedSize > buf_len)
    {
        common::log_error("Compressed image too large: %lu > %lu", actualCompressedSize, buf_len);
        // Queue the buffer back (send it to the device)
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0)
        {
            common::errno_log("V4L2LoopbackWriter::writeFrame - VIDIOC_QBUF");
            return false;
        }
        return false;
    }

    buf.bytesused = actualCompressedSize;

    // Queue the buffer back (send it to the device)
    if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0)
    {
        common::errno_log("V4L2LoopbackWriter::writeFrame - VIDIOC_QBUF");
        return false;
    }

    Profiler::getInstance().stop(name_.c_str(), "Encode and write output image");

    return true;
}


void V4L2LoopbackWriter::cleanup()
{
    // 1. Stop streaming_
    if (streaming_)
    {
        if (fd_ >= 0)
        {
            enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            ioctl(fd_, VIDIOC_STREAMOFF, &type);
        }
        streaming_ = false;
    }

    // 2. Unmap buffers_
    for (auto& buffer : buffers_)
    {
        if (buffer.start != MAP_FAILED)
        {
            munmap(buffer.start, buffer.length);
        }
    }
    buffers_.clear();

    // 3. Release buffers_
    if (fd_ >= 0)
    {
        struct v4l2_requestbuffers reqbuf = {0};
        reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        reqbuf.memory = V4L2_MEMORY_MMAP;
        reqbuf.count = 0;
        ioctl(fd_, VIDIOC_REQBUFS, &reqbuf);
    }

    // 4. Close device
    if (fd_ >= 0)
    {
        close(fd_);
        fd_ = -1;
    }

    if (encoder_ != nullptr)
    {
        encoder_.reset();
    }

    common::log_info("v4l2LoopbackWritter - Cleanup finished!");
}

bool V4L2LoopbackWriter::reconfigure(TJSAMP subsampling, int quality)
{
    // Stop current operation
    bool wasStreaming = streaming_;
    if (wasStreaming)
    {
        if (!stop())
        {
            common::log_error("V4L2LoopbackWriter::reconfigure - Failed to stop streaming");
            return false;
        }
    }

    // Update device variables
    chrominance_subsampling_ = subsampling;
    quality_ = quality;

    // Recreate encoder with new subsampling
    if (encoder_)
    {
        encoder_.reset();
    }

    ConfigBuilder configBuilder;
    configBuilder.imageFormat(selectedFormat_->format)
        .pixelFormat(TJPF_RGB)
        .width(selectedFormat_->sizes[selectedFormat_->selectedFrameSize].width)
        .height(selectedFormat_->sizes[selectedFormat_->selectedFrameSize].height)
        .quality(quality)
        .chrominance_subsampling(chrominance_subsampling_);

    encoder_ = CodecFactory::create<Encoder>(configBuilder);
    if (encoder_ == nullptr)
    {
        common::log_error("V4L2LoopbackWriter::reconfigure - Failed to create encoder");
        return false;
    }

    // Restart streaming if it was active before
    if (wasStreaming)
    {
        if (!start())
        {
            common::log_error("V4L2LoopbackWriter::reconfigure - Failed to restart streaming");
            return false;
        }
    }

    common::log_info("V4L2LoopbackWriter::reconfigure - Successfully reconfigured subsampling");
    return true;
}
