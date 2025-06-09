#include "FunnyFace/inputWebcam.h"

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include "FunnyFace/codecFactory.h"
#include "FunnyFace/common.h"
#include "FunnyFace/profiler.h"
using namespace funnyface;

InputWebcam::InputWebcam(const std::string& name, const std::string& devicePath, const unsigned int width,
                         const unsigned int height, const unsigned int bufferCount)
    : Webcam(name, devicePath, WebcamType::PhysicalInput, width, height)
{
    buffer_count_ = bufferCount;
}

bool InputWebcam::setupDevice()
{
    // Open the webcam device
    if (!Webcam::open())
    {
        return false;
    }

    if (!Webcam::updateDeviceCapabilities())
    {
        common::errno_log("InputWebcam::setupDevice - Failed to get device capabilities");
        return false;
    }

    // Configure camera format
    if (!Webcam::configureDeviceFormat())
    {
        common::log_error("InputWebcam::setupDevice - Failed to configure camera format");
        return false;
    }

    // Here we know what format we are decoding, we can create the decoder.
    // TODO: FIXME: Create the decoder based on the format we are using.
    ConfigBuilder configBuilder;
    configBuilder.imageFormat(selectedFormat_->format)
        .pixelFormat(TJPF_RGB)
        .width(selectedFormat_->sizes[selectedFormat_->selectedFrameSize].width)
        .height(selectedFormat_->sizes[selectedFormat_->selectedFrameSize].height);

    decoder_ = CodecFactory::create<Decoder>(configBuilder);
    if (decoder_ == nullptr)
    {
        common::log_error("InputWebcam::setupDevice - Failed to create decoder");
        return false;
    }
    // Configure buffers_
    if (!configureBuffers())
    {
        common::log_error("InputWebcam::setupDevice - Failed to configure buffers_");
        return false;
    }

    ready_ = true;
    common::log_info("InputWebcam::setupDevice - Successfully set up device %s", device_path_.c_str());
    return true;
}

bool InputWebcam::start()
{
    if (!ready_)
    {
        common::log_error("InputWebcam::start - Device not ready");
        return false;
    }

    if (!startRecording())
    {
        return false;
    }

    return true;
}


bool InputWebcam::stop()
{
    if (!ready_)
    {
        common::log_error("InputWebcam::stop - Device not ready");
        return false;
    }

    stopRecording();
    if(!queueAllBuffersAgain())
    {
        common::log_error("InputWebcam::stop - Failed to queue all buffers again");
        return false;
    }
    return true;
}

bool InputWebcam::configureBuffers()
{
    struct v4l2_buffer buf;

    CLEAR(bufrequest_);
    bufrequest_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufrequest_.memory = V4L2_MEMORY_MMAP;
    bufrequest_.count = buffer_count_;

    if (ioctl(fd_, VIDIOC_REQBUFS, &bufrequest_) < 0)
    {
        common::errno_log("InputWebcam::configureBuffers - VIDIOC_REQBUFS");
        return false;
    }

    if (bufrequest_.count != buffer_count_)
    {
        common::errno_log("InputWebcam::configureBuffers - Not enough buffer memory");
        return false;
    }

    buffers_ = (Buffer*) calloc(bufrequest_.count, sizeof(*buffers_));
    if (!buffers_)
    {
        common::errno_log("InputWebcam::configureBuffers - Out of memory when creating buffers_");
        return false;
    }

    // Allocate and configure buffers_
    for (unsigned int i = 0; i < bufrequest_.count; i++)
    {
        CLEAR(buf);
        buf.type = bufrequest_.type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0)
        {
            common::errno_log("InputWebcam::configureBuffers - VIDIOC_QUERYBUF");
            cleanupBuffers();
            return false;
        }

        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);

        if (buffers_[i].start == MAP_FAILED)
        {
            common::errno_log("InputWebcam::configureBuffers - MMAP Failed");
            cleanupBuffers();
            return false;
        }

        memset(buffers_[i].start, 0, buffers_[i].length);

        if (ioctl(fd_, VIDIOC_QBUF, &buf) == -1)
        {
            common::errno_log("InputWebcam::configureBuffers - VIDIOC_QBUF");
            cleanupBuffers();
            return false;
        }
    }

    return true;
}

bool InputWebcam::startStreaming()
{
    if (fd_ >= 0)
    {
        if (ioctl(fd_, VIDIOC_STREAMON, &bufrequest_.type) < 0)
        {
            common::errno_log("InputWebcam::startStreaming - VIDIOC_STREAMON");
            cleanup();
            return false;
        }
    }
    else
    {
        common::log_error("InputWebcam::startStreaming - fd_ < 0");
        return false;
    }
    return true;
}


bool InputWebcam::stopStreaming()
{
    if (fd_ >= 0)
    {
        if (ioctl(fd_, VIDIOC_STREAMOFF, &bufrequest_.type) < 0)
        {
            common::errno_log("InputWebcam::stopStreaming - VIDIOC_STREAMOFF");
            return false;
        }
    }
    else
    {
        common::log_error("InputWebcam::stopStreaming - fd_ < 0");
        return false;
    }
    return true;
}


bool InputWebcam::startRecording()
{
    if (isRecording_.load())
    {
        common::log_info("InputWebcam::startRecording - already recording");
        return false;
    }

    if (!startStreaming())
    {
        common::log_error("InputWebcam::setupDevice - Failed to start streaming");
        cleanup();
        return false;
    }

    isRecording_ = true;
    recordThread_ = std::thread(&InputWebcam::imageAcquisitionLoop, this);
    return true;
}

void InputWebcam::stopRecording()
{
    if(!stopStreaming())
    {
        common::log_error("InputWebcam::stopRecording - Failed to stop streaming");
        return;
    }

    isRecording_ = false;

    if (recordThread_.joinable())
    {
        recordThread_.join();
    }
    common::log_info("InputWebcam::stopRecording - Stopped recording");
}

bool InputWebcam::isRunning()
{
    return isRecording_;
}

bool InputWebcam::getImage(Image*& outImage)
{
    std::lock_guard<std::mutex> lock(imageMutex_);
    if (image_.getBeingUsed())
    {
        outImage = &image_;
        return true;
    }
    return false;
}


void InputWebcam::imageAcquisitionLoop()
{
    struct v4l2_buffer buf;
    unsigned int totalDiscarded{0u};
    unsigned int totalDiscardedHeader{0u};
    unsigned int decodingFailureCount{0u};
    unsigned int totalTimeouts{0u};
    const unsigned int warmupDiscardCount{2u};
    
    bool readImageHeader{true};
    image_.setBeingUsed(false);

    while (isRecording_.load())
    {
        fd_set fds;
        struct timeval tv;
        int r;

        FD_ZERO(&fds);
        FD_SET(fd_, &fds);

        tv.tv_sec = 2;
        tv.tv_usec = 0;
        Profiler::getInstance().start(name_.c_str(), "Waiting for OS camera frame");
        r = select(fd_ + 1, &fds, nullptr, nullptr, &tv);

        if (r == -1)
        {
            if (EINTR == errno)
            {
                continue;
            }
            common::errno_log("InputWebcam::imageAcquisitionLoop - Select failed");
            break;
        }

        if (r == 0)
        {
            common::log_warn("InputWebcam::imageAcquisitionLoop - Select timeout");
            totalTimeouts++;
            if (totalTimeouts > 10)
            {
                common::log_warn("InputWebcam::imageAcquisitionLoop - Select timeout reached, exiting loop");
                break;
            }
            continue;
        }

        if (isRecording_ == false)
        {
            common::log_info("InputWebcam::imageAcquisitionLoop - Recording stopped, exiting loop");
            break;
        }

        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0)
        {
            if (errno == EAGAIN || errno == EIO)
            {
                continue;
            }
            else
            {
                common::errno_log("InputWebcam::imageAcquisitionLoop - VIDIOC_DQBUF");
                break;
            }
        }

        if (buf.index >= bufrequest_.count)
        {
            common::errno_log("InputWebcam::imageAcquisitionLoop - INVALID INDEX in BUFF. Aborting.");
            break;
        }

        Profiler::getInstance().stop(name_.c_str(), "Waiting for OS camera frame");

        if (totalDiscarded < warmupDiscardCount)
        {
            totalDiscarded++;
            if (!requeueFrame(buf))
            {
                break;
            }
            continue;
        }

        Profiler::getInstance().start(name_.c_str(), "Input image decoding");

        Image srcImage(static_cast<unsigned char*>(buffers_[buf.index].start), buf.bytesused, false);
        srcImage.info.TJPixelFormat = TJPF_RGB;

        if (readImageHeader)
        {
            unsigned long raw_needed_size;
            bool valid_image = decoder_->decodeHeader(srcImage, raw_needed_size);
            if (!valid_image)
            {
                common::log_info(
                    "InputWebcam::imageAcquisitionLoop - Invalid input image. Discarded. Total discarded: %d",
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
                common::log_warn("InputWebcam::imageAcquisitionLoop - Reallocating raw image buffer to %lu - %s",
                                 image_.size(), common::format_size(image_.size()));
            }

            cameraInputInfo = srcImage.info;
            readImageHeader = false;
        }

        if (image_.getBeingUsed())
        {
            common::log_warn("InputWebcam::imageAcquisitionLoop - Image buffer is being used, skipping frame");
            if (!requeueFrame(buf))
            {
                break;
            }
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(imageMutex_);

            if (!decoder_->decode(srcImage, image_))
            {
                readImageHeader = true;
                image_.setBeingUsed(false);
                if (!requeueFrame(buf))
                {
                    break;
                }
                if (decodingFailureCount > 10)
                {
                    common::log_error("InputWebcam::imageAcquisitionLoop - Failed to decode image after %d attempts",
                                      decodingFailureCount);
                    break;
                }
                else
                {
                    decodingFailureCount++;
                    common::log_error("InputWebcam::imageAcquisitionLoop - Decoding failed %d times",
                                      decodingFailureCount);
                    continue;
                }
            }

            decodingFailureCount = 0;
            image_.info = cameraInputInfo;
        }
        Profiler::getInstance().stop(name_.c_str(), "Input image decoding");

        // Others can process the image

        if (!requeueFrame(buf))
        {
            break;
        }
    }

    image_.setBeingUsed(false);
    common::log_warn("InputWebcam::imageAcquisitionLoop thread dead for device: %s", device_path_.c_str());
}

InputWebcam::~InputWebcam()
{
    cleanup();
}

void InputWebcam::cleanup()
{
    stopRecording();
    cleanupBuffers();

    if (fd_ >= 0)
    {
        common::log_info("Closing fd: %d", fd_);
        close(fd_);
        fd_ = -1;
    }

    if (decoder_ != nullptr)
    {
        decoder_.reset();
    }
}


void InputWebcam::cleanupBuffers()
{
    if (buffers_)
    {
        for (unsigned int i = 0; i < bufrequest_.count; i++)
        {
            if (buffers_[i].start != MAP_FAILED)
            {
                if (-1 == munmap(buffers_[i].start, buffers_[i].length))
                {
                    common::errno_log("InputWebcam::cleanupBuffers - munmap failed");
                }
            }
        }
        free(buffers_);
        buffers_ = nullptr;
    }
}

bool InputWebcam::queueAllBuffersAgain()
{
    struct v4l2_buffer buf;

    // Allocate and configure buffers_
    for (unsigned int i = 0; i < bufrequest_.count; i++)
    {
        CLEAR(buf);
        buf.type = bufrequest_.type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if(!requeueFrame(buf))
        {
            common::errno_log("InputWebcam::queueAllBuffersAgain - VIDIOC_QBUF");
            return false;
        }
    }
    return true;
}

bool InputWebcam::requeueFrame(struct v4l2_buffer& buf)
{
    if (ioctl(fd_, VIDIOC_QBUF, &buf) == -1)
    {
        common::errno_log("InputWebcam::requeueFrame - VIDIOC_QBUF");
        return false;
    }
    return true;
}

bool InputWebcam::reconfigureFormat(int formatIndex, int sizeIndex)
{
    common::log_info("InputWebcam::reconfigureFormat - Reconfiguring device %s with format %d, size %d", name_.c_str(),
                     formatIndex, sizeIndex);

    // Stop current operation
    bool wasRunning = isRunning();
    if (wasRunning)
    {
        if (!stop())
        {
            common::log_error("InputWebcam::reconfigureFormat - Failed to stop device");
            return false;
        }
    }

    // Validate indices
    if (formatIndex < 0 || formatIndex >= static_cast<int>(capabilities_.formats.size()) || sizeIndex < 0
        || sizeIndex >= static_cast<int>(capabilities_.formats[formatIndex].sizes.size()))
    {
        common::log_error("InputWebcam::reconfigureFormat - Invalid format/size indices");
        if (wasRunning)
        {
            start(); // Try to restart with old config
        }
        return false;
    }

    // Update desired format and size
    const auto& selectedFormat = capabilities_.formats[formatIndex];
    const auto& selectedSize = selectedFormat.sizes[sizeIndex];

    desiredWidth_ = selectedSize.width;
    desiredHeight_ = selectedSize.height;

    // Cleanup current setup
    cleanup();

    // Set new selected format
    selectedFormat_ = std::make_unique<Format>(selectedFormat);
    selectedFormat_->selectedFrameSize = sizeIndex;

    // Reinitialize device
    if (!setupDevice())
    {
        common::log_error("InputWebcam::reconfigureFormat - Failed to setup device with new configuration");
        return false;
    }

    // Restart if it was running before
    if (wasRunning)
    {
        if (!start())
        {
            common::log_error("InputWebcam::reconfigureFormat - Failed to restart device");
            return false;
        }
    }

    common::log_info("InputWebcam::reconfigureFormat - Successfully reconfigured device to %ux%u", desiredWidth_,
                     desiredHeight_);
    return true;
}
