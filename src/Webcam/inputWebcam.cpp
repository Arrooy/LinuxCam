#include "LinuxFace/inputWebcam.h"

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include "LinuxFace/codecFactory.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"
using namespace linuxface;

InputWebcam::InputWebcam(const std::string& name, const std::string& devicePath, const unsigned int width,
                         const unsigned int height, const unsigned int bufferCount)
    : Webcam(name, devicePath, WebcamType::PHYSICAL_INPUT, width, height), buffer_count_(bufferCount)
{
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
        return false;
    }

    // Configure camera format
    if (!Webcam::configureDeviceFormat())
    {
        return false;
    }

    // Here we know what format we are decoding, we can create the decoder.
    ConfigBuilder configBuilder;
    configBuilder.imageFormat(selectedFormat_->format)
        .pixelFormat(TJPF_RGB)
        .width(selectedFormat_->sizes[selectedFormat_->selectedFrameSize].width)
        .height(selectedFormat_->sizes[selectedFormat_->selectedFrameSize].height);

    decoder_ = CodecFactory::create<Decoder>(configBuilder);
    if (decoder_ == nullptr)
    {
        common::logError("InputWebcam::setupDevice - Failed to create decoder");
        return false;
    }
    // Configure buffers_
    if (!configureBuffers())
    {
        common::logError("InputWebcam::setupDevice - Failed to configure buffers_");
        return false;
    }

    ready_ = true;
    common::logInfo("InputWebcam::setupDevice - Successfully set up device %s", device_path_.c_str());
    return true;
}

bool InputWebcam::start()
{
    if (!ready_)
    {
        common::logError("InputWebcam::start - Device not ready");
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
        common::logError("InputWebcam::stop - Device not ready");
        return false;
    }

    stopRecording();
    return queueAllBuffersAgain(bufrequest_.count, bufrequest_.type);
}

bool InputWebcam::configureBuffers()
{
    struct v4l2_buffer buf{};

    CLEAR(bufrequest_);
    bufrequest_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufrequest_.memory = V4L2_MEMORY_MMAP;
    bufrequest_.count = buffer_count_;

    if (ioctl(fd_, VIDIOC_REQBUFS, &bufrequest_) < 0)
    {
        common::errnoLog("InputWebcam::configureBuffers - VIDIOC_REQBUFS");
        return false;
    }

    if (bufrequest_.count != buffer_count_)
    {
        common::errnoLog("InputWebcam::configureBuffers - Not enough buffer memory");
        return false;
    }

    buffers_ = static_cast<Buffer*>(calloc(bufrequest_.count, sizeof(*buffers_)));
    if (buffers_ == nullptr)
    {
        common::errnoLog("InputWebcam::configureBuffers - Out of memory when creating buffers_");
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
            common::errnoLog("InputWebcam::configureBuffers - VIDIOC_QUERYBUF");
            cleanupBuffers();
            return false;
        }

        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);

        if (buffers_[i].start == MAP_FAILED)
        {
            common::errnoLog("InputWebcam::configureBuffers - MMAP Failed");
            cleanupBuffers();
            return false;
        }

        memset(buffers_[i].start, 0, buffers_[i].length);

        if (ioctl(fd_, VIDIOC_QBUF, &buf) == -1)
        {
            common::errnoLog("InputWebcam::configureBuffers - VIDIOC_QBUF");
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
            common::errnoLog("InputWebcam::startStreaming - VIDIOC_STREAMON");
            cleanup();
            return false;
        }
    }
    else
    {
        common::logError("InputWebcam::startStreaming - fd_ < 0");
        return false;
    }
    return true;
}


bool InputWebcam::stopStreaming()
{
    if (fd_ >= 0)
    {
        if (!isRecording_.load())
        {
            return true;
        }

        if (ioctl(fd_, VIDIOC_STREAMOFF, &bufrequest_.type) < 0)
        {
            common::errnoLog("InputWebcam::stopStreaming - VIDIOC_STREAMOFF");
            return false;
        }
    }
    else
    {
        common::logError("InputWebcam::stopStreaming - Unknown file descriptor");
        return false;
    }
    return true;
}


bool InputWebcam::startRecording()
{
    if (isRecording_.load())
    {
        common::logInfo("InputWebcam::startRecording - already recording");
        return true;
    }

    if (!startStreaming())
    {
        common::logError("InputWebcam::setupDevice - Failed to start streaming");
        cleanup();
        return false;
    }

    isRecording_ = true;
    recordThread_ = std::thread(&InputWebcam::imageAcquisitionLoop, this);
    return true;
}

void InputWebcam::stopRecording()
{
    if (!stopStreaming())
    {
        common::logError("InputWebcam::stopRecording - Failed to stop streaming");
        return;
    }

    isRecording_ = false;

    if (recordThread_.joinable())
    {
        recordThread_.join();
    }
}

bool InputWebcam::isRunning()
{
    return isRecording_;
}

void InputWebcam::imageAcquisitionLoop()
{
    struct v4l2_buffer buf{};
    unsigned int totalDiscardedHeader{0u};
    unsigned int decodingFailureCount{0u};
    unsigned int totalTimeouts{0u};

    unsigned int totalDiscarded{0u};
    const unsigned int maxDiscardCount{buffer_count_ * 3};

    unsigned int skipCounter{0u};
    const unsigned int amountOfSkipFrames = buffer_count_; // std::max(2u, buffer_count_ / 2u);

    bool readImageHeader{true};

    // Image state
    Image imageTmp;
    ImageMetadata cameraInputInfo;

    while (isRecording_.load())
    {
        fd_set fds;
        struct timeval tv{};
        int r = 0;

        FD_ZERO(&fds);
        FD_SET(fd_, &fds);

        tv.tv_sec = 2;
        tv.tv_usec = 0;
        Profiler::ScopedProfilerSpan span_wait(name_, "Waiting for OS camera frame");
        r = select(fd_ + 1, &fds, nullptr, nullptr, &tv);

        if (r == -1)
        {
            if (EINTR == errno)
            {
                continue;
            }
            common::errnoLog("InputWebcam::imageAcquisitionLoop - Select failed");
            break;
        }

        if (r == 0)
        {
            common::logWarn("InputWebcam::imageAcquisitionLoop - Select timeout");
            totalTimeouts++;
            if (totalTimeouts > 5)
            {
                common::logError("InputWebcam::imageAcquisitionLoop - Select timeout reached. Please unplug and plug "
                                 "again the camera.");
                break;
            }
            continue;
        }

        if (!isRecording_)
        {
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

            common::errnoLog("InputWebcam::imageAcquisitionLoop - VIDIOC_DQBUF");
            break;
        }

        if (buf.index >= bufrequest_.count)
        {
            common::errnoLog("InputWebcam::imageAcquisitionLoop - INVALID INDEX in BUFF. Aborting.");
            break;
        }

        // Check if buffer is in error state
        if ((buf.flags & V4L2_BUF_FLAG_ERROR) != 0u)
        {
            if (!requeueFrame(buf))
            {
                common::logError("InputWebcam::imageAcquisitionLoop - Failed to requeue frame %d", buf.index);
                break;
            }
            totalDiscarded++;
            if (totalDiscarded > maxDiscardCount)
            {
                common::logError("InputWebcam::imageAcquisitionLoop - Too many discarded frames (%d), exiting loop",
                                 totalDiscarded);
                break;
            }
            continue;
        }

        if (buf.bytesused == 0)
        {
            common::logInfo("InputWebcam::imageAcquisitionLoop - Empty frame received, requeuing");
            if (!requeueFrame(buf))
            {
                break;
            }
            continue;
        }

        // Skip inital frames to get rid of initial camera warmup frames
        if (skipCounter < amountOfSkipFrames)
        {
            common::logInfo("InputWebcam::imageAcquisitionLoop - Skipping frame %d", buf.index);
            if (!requeueFrame(buf))
            {
                common::logError("InputWebcam::imageAcquisitionLoop - Failed to requeue frame %d", buf.index);
                break;
            }
            skipCounter++;
            continue;
        }


        Profiler::ScopedProfilerSpan span_decode(name_, "Input image decoding");

        Image srcImage(static_cast<unsigned char*>(buffers_[buf.index].start), buf.bytesused, false);
        srcImage.info.TJPixelFormat = TJPF_RGB;


        if (readImageHeader)
        {
            unsigned long rawNeededSize = 0;
            const bool validImage = decoder_->decodeHeader(srcImage, rawNeededSize);
            if (!validImage)
            {
                common::logInfo(
                    "InputWebcam::imageAcquisitionLoop - Invalid input image. Discarded. Total discarded: %d",
                    ++totalDiscardedHeader);
                if (!requeueFrame(buf))
                {
                    break;
                }
                continue;
            }

            if (imageTmp.size() != rawNeededSize)
            {
                imageTmp.resize(rawNeededSize);
                common::logWarn("InputWebcam::imageAcquisitionLoop - Reallocating raw image buffer to %lu - %s",
                                imageTmp.size(), common::formatSize(imageTmp.size()));
            }

            cameraInputInfo = srcImage.info;
            readImageHeader = false;
        }


        if (!decoder_->decode(srcImage, imageTmp))
        {
            readImageHeader = true;
            if (!requeueFrame(buf))
            {
                break;
            }
            if (decodingFailureCount > 10)
            {
                common::logError("InputWebcam::imageAcquisitionLoop - Failed to decode image after %d attempts",
                                 decodingFailureCount);
                break;
            }

            decodingFailureCount++;
            common::logError("InputWebcam::imageAcquisitionLoop - Decoding failed %d times", decodingFailureCount);
            continue;
        }

        decodingFailureCount = 0;
        imageTmp.info = cameraInputInfo; // TODO(arroyo): FIXME: Here we are resetting the
                                         // decoder state. We should use the decoder state
        {
            const std::lock_guard<std::mutex> lock(imageMutex_);
            if (!latestImage_)
            {
                latestImage_ = std::make_unique<Image>(imageTmp.size());
            }
            // Copy image to latest image
            latestImage_->copyFrom(imageTmp);
        }


        // Others can process the image

        if (!requeueFrame(buf))
        {
            break;
        }
    }

    common::logWarn("InputWebcam::imageAcquisitionLoop thread dead for device: %s", device_path_.c_str());
}

bool InputWebcam::getImage(std::unique_ptr<Image>& outImage)
{
    Profiler::ScopedProfilerSpan span(name_, "Get new camera frame");
    // Get the most recent frame from queue, discarding older ones
    if (latestImage_)
    {
        const std::lock_guard<std::mutex> lock(imageMutex_);
        outImage = latestImage_->deepCopy();
        return true;
    }

    return false;
}

InputWebcam::~InputWebcam()
{
    cleanup();
}

void InputWebcam::cleanup()
{
    if (ready_)
    {
        stopRecording();
    }

    cleanupBuffers();

    if (fd_ >= 0)
    {
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
    if (buffers_ != nullptr)
    {
        for (unsigned int i = 0; i < bufrequest_.count; i++)
        {
            if (buffers_[i].start != MAP_FAILED)
            {
                if (-1 == munmap(buffers_[i].start, buffers_[i].length))
                {
                    common::errnoLog("InputWebcam::cleanupBuffers - munmap failed");
                }
            }
        }
        free(buffers_);
        buffers_ = nullptr;
    }
}

bool InputWebcam::reconfigureFormat(int formatIndex, int sizeIndex, int fpsIndex)
{
    const auto& newFormat = capabilities_.formats[formatIndex].sizes[sizeIndex];
    common::logInfo("InputWebcam::reconfigureFormat - Reconfiguring device %s with format Index %d, size index %d and "
                    "fps index %d that is %dx%d with %d fps",
                    name_.c_str(), formatIndex, sizeIndex, fpsIndex, newFormat.width, newFormat.height,
                    newFormat.getFps(newFormat.selectedFPS));

    // Stop current operation
    const bool wasRunning = isRunning();
    if (wasRunning)
    {
        if (!stop())
        {
            common::logError("InputWebcam::reconfigureFormat - Failed to stop device");
            return false;
        }
    }

    // Validate indices provided
    if (formatIndex < 0 || formatIndex >= static_cast<int>(capabilities_.formats.size()) || sizeIndex < 0
        || sizeIndex >= static_cast<int>(capabilities_.formats[formatIndex].sizes.size()))
    {
        common::logError("InputWebcam::reconfigureFormat - Invalid format/size indices");
        if (wasRunning)
        {
            start(); // Try to restart with old config
        }
        return false;
    }

    const Format selectedFormat = capabilities_.formats[formatIndex];

    // Cleanup current setup
    cleanup();

    // Set new selected format
    selectedFormat_ = std::make_unique<Format>(selectedFormat);
    selectedFormat_->selectedFrameSize = sizeIndex;
    selectedFormat_->sizes[sizeIndex].selectedFPS = fpsIndex;

    // Reinitialize device with new selected format
    if (!setupDevice())
    {
        common::logError("InputWebcam::reconfigureFormat - Failed to setup device with new configuration");
        return false;
    }

    // Restart if it was running before
    if (wasRunning)
    {
        if (!start())
        {
            common::logError("InputWebcam::reconfigureFormat - Failed to restart device");
            return false;
        }
    }
    return true;
}
