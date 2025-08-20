#include "LinuxFace/v4l2loopbackWritter.h"

#include "LinuxFace/profiler.h"

using namespace linuxface;

V4L2LoopbackWriter::V4L2LoopbackWriter(const std::string& name, const std::string& device_path, const unsigned int width,
                                       const unsigned int height, const TJSAMP subsample)
    : Webcam(name, devicePath, WebcamType::VirtualOutput, width, height), chrominance_subsampling_(subsample)

{
}

V4L2LoopbackWriter::~V4L2LoopbackWriter()
{
    cleanup();
}

bool V4L2LoopbackWriter::setupDevice()
{
    // 1. Open device
    fd = ::open(device_path.c_str(), O_RDWR);
    if (fd < 0)
    {
        common::errnoLog("V4L2LoopbackWriter::setupDevice - Open fd failed.");
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

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        common::errnoLog("V4L2LoopbackWriter::setupDevice - VIDIOC_S_FMT");
        cleanup();
        return false;
    }

    // Store the selected format in capabilities
    Format sel_fmt;
    sel_fmt.description = "V4L2 custom device";
    sel_fmt.format = ImageFormat::JPEG;
    sel_fmt.pixelformat = V4L2_PIX_FMT_MJPEG;
    sel_fmt.selectedFrameSize = 0;
    sel_fmt.sizes.push_back(FrameSize{getDesiredWidth(), getDesiredHeight()});
    selected_format = std::make_unique<Format>(sel_fmt);

    capabilities.formats.push_back(sel_fmt);

    // 3. Request buffers_
    struct v4l2_requestbuffers reqbuf = {0};
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 1; // Single buffer for V4L2 loopback - sufficient for virtual devices

    if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0)
    {
        common::errnoLog("V4L2LoopbackWriter::setupDevice - VIDIOC_REQBUFS");
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

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            common::errnoLog("V4L2LoopbackWriter::setupDevice - VIDIOC_QUERYBUF");
            cleanup();
            return false;
        }

        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);

        if (buffers_[i].start == MAP_FAILED)
        {
            common::errnoLog("V4L2LoopbackWriter::setupDevice - mmap");
            cleanup();
            return false;
        }
    }

    // Create encoder
    auto pixel_format = TJPF_RGB;

    ConfigBuilder config_builder;
    config_builder.imageFormat(selected_format->format)
        .pixelFormat(pixel_format)
        .width(selected_format->sizes[selected_format->selectedFrameSize].width)
        .height(selected_format->sizes[selected_format->selectedFrameSize].height)
        .quality(100)
        .chrominanceSubsampling(chrominance_subsampling_);

    encoder_ = CodecFactory::create<Encoder>(config_builder);
    return !(encoder_ == nullptr);
}

bool V4L2LoopbackWriter::start()
{
    if (fd_ >= 0)
    {
        enum const v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0)
        {
            common::errnoLog("V4L2LoopbackWriter::start - VIDIOC_STREAMON");
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
        enum const v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0)
        {
            common::errnoLog("V4L2LoopbackWriter::stop - VIDIOC_STREAMOFF");
            cleanup();
            return false;
        }

        if (!queueAllBuffersAgain(buffers_.size(), type))
        {
            common::logError("V4L2LoopbackWriter::stop - Failed to queue all buffers again");
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
        common::logError("Device not initialized for streaming_");
        return false;
    }

    Profiler::getInstance().start(name, "Encode and write output image");

    // Check if we need to scale the image
    std::unique_ptr<Image> scaled_image = nullptr;
    const unsigned long desired_width = getDesiredWidth();
    const unsigned long desired_height = getDesiredHeight();
    if (image.info.width != desired_width || image.info.height != desired_height)
    {
        scaled_image = image.scale(desired_width, desired_height);
        if (!scaledImage)
        {
            common::logError("V4L2LoopbackWriter::writeFrame - Failed to scale image");
            Profiler::getInstance().stop(name, "Encode and write output image");
            return false;
        }
    }

    // Use scaled image if available, otherwise use original
    const Image& image_to_encode = scaledImage ? *scaledImage : image;

    // 1. Dequeue a buffer (get an available buffer)
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0)
    {
        common::errnoLog("V4L2LoopbackWriter::writeFrame - VIDIOC_DQBUF");
        return false;
    }

    const size_t buf_len = buffers_[buf.index].length;

    // Wrap v4l2 buffer to Image so encoder can encode there the image.
    Image v4l2_image = Image(static_cast<unsigned char*>(buffers_[buf.index].start), buf_len, false);

    // Encode image and store it into v4l2 buffer
    unsigned long actual_compressed_size{0u};
    if (!encoder_->encode(image_to_encode, v4l2_image, actual_compressed_size))
    {
        common::logError("Failed to encode image");
        // Re-queue buffer
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0)
        {
            common::errnoLog("V4L2LoopbackWriter::writeFrame - VIDIOC_QBUF");
            return false;
        }
        return false;
    }

    if (actual_compressed_size > buf_len)
    {
        common::logError("Compressed image too large: %lu > %lu", actualCompressedSize, buf_len);
        // Queue the buffer back (send it to the device)
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0)
        {
            common::errno_log("V4L2LoopbackWriter::writeFrame - VIDIOC_QBUF");
            return false;
        }
        return false;
    }

    buf.bytesused = actual_compressed_size;

    // Queue the buffer back (send it to the device)
    return !ioctl(fd_, VIDIOC_QBUF, &buf) < 0;
}


void V4L2LoopbackWriter::cleanup()
{
    // 1. Stop streaming_
    if (streaming_)
    {
        if (fd_ >= 0)
        {
            enum const v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
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

    common::logInfo("v4l2LoopbackWritter - Cleanup finished!");
}

bool V4L2LoopbackWriter::reconfigure(TJSAMP subsampling, int quality)
{
    // Stop current operation
    const bool was_streaming = streaming_;
    if (was_streaming)
    {
        if (!stop())
        {
            common::logError("V4L2LoopbackWriter::reconfigure - Failed to stop streaming");
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

    const ConfigBuilder config_builder;
    configBuilder.imageFormat(selectedFormat_->format)
        .pixelFormat(TJPF_RGB)
        .width(selectedFormat_->sizes[selectedFormat_->selectedFrameSize].width)
        .height(selectedFormat_->sizes[selectedFormat_->selectedFrameSize].height)
        .quality(quality)
        .chrominance_subsampling(chrominance_subsampling_);

    encoder_ = CodecFactory::create<Encoder>(config_builder);
    if (encoder_ == nullptr)
    {
        common::logError("V4L2LoopbackWriter::reconfigure - Failed to create encoder");
        return false;
    }

    // Restart streaming if it was active before
    if (was_streaming)
    {
        if (!start())
        {
            common::logError("V4L2LoopbackWriter::reconfigure - Failed to restart streaming");
            return false;
        }
    }

    common::logInfo("V4L2LoopbackWriter::reconfigure - Successfully reconfigured subsampling");
    return true;
}
