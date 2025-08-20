#include "LinuxFace/inputWebcam.h"

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include "LinuxFace/codecFactory.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"
using namespace linuxface;

InputWebcam::InputWebcam(const std::string& name, const std::string& device_path, const unsigned int width,
                         const unsigned int height, const unsigned int buffer_count)
    : Webcam(name, devicePath, WebcamType::PhysicalInput, width, height), buffer_count_(buffer_count)
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
    // TODO(arroyo): FIXME: Create the decoder based on the format we are using.
    ConfigBuilder config_builder;
    config_builder.imageFormat(selected_format->format)
        .pixelFormat(TJPF_RGB)
        .width(selected_format->sizes[selected_format->selectedFrameSize].width)
        .height(selected_format->sizes[selected_format->selectedFrameSize].height);

    decoder_ = CodecFactory::create<Decoder>(config_builder);
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
    common::logInfo("InputWebcam::setupDevice - Successfully set up device %s", device_path.c_str());
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
    if (!queueAllBuffersAgain(bufrequest_.count, bufrequest_.type))
    {
        common::logError("InputWebcam::stop - Failed to queue all buffers again");
        return false;
    }
    return true;
}

bool InputWebcam::configureBuffers()
{
    struct v4l2_buffer buf
    {
    };

    CLEAR(bufrequest_);
    bufrequest_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufrequest_.memory = V4L2_MEMORY_MMAP;
    bufrequest_.count = buffer_count_;

    if (ioctl(fd, VIDIOC_REQBUFS, &bufrequest_) < 0)
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
        common::errnoLog("InputWebcam::configureBuffers - Out of memory when creating "
                         "buffers_");
        return false;
    }

    // Allocate and configure buffers_
    for (unsigned int i = 0; i < bufrequest_.count; i++)
    {
        CLEAR(buf);
        buf.type = bufrequest_.type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            common::errnoLog("InputWebcam::configureBuffers - VIDIOC_QUERYBUF");
            cleanupBuffers();
            return false;
        }

        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);

        if (buffers_[i].start == MAP_FAILED)
        {
            common::errnoLog("InputWebcam::configureBuffers - MMAP Failed");
            cleanupBuffers();
            return false;
        }

        memset(buffers_[i].start, 0, buffers_[i].length);

        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1)
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
        if (ioctl(fd, VIDIOC_STREAMON, &bufrequest_.type) < 0)
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

        if (ioctl(fd, VIDIOC_STREAMOFF, &bufrequest_.type) < 0)
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
    struct v4l2_buffer buf
    {
    };
    const unsigned int total_discarded_header{0u};
    unsigned int decoding_failure_count{0u};
    unsigned int total_timeouts{0u};

    unsigned int total_discarded{0u};
    const unsigned int max_discard_count{buffer_count_ * 3};

    unsigned int skip_counter{0u};
    const unsigned int amount_of_skip_frames = buffer_count_; // std::max(2u, buffer_count_ / 2u);

    bool read_image_header{true};

    // Image state
    Image image_tmp;
    ImageMetadata camera_input_info;

    while (isRecording_.load())
    {
        fd_set fds;
        struct timeval tv
        {
        };
        const int r = 0;

        FD_ZERO(&fds);
        FD_SET(fds, &fds);

        tv.tv_sec = 2;
        tv.tv_usec = 0;
        Profiler::getInstance().start(name_, "Waiting for OS camera frame");
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
            total_timeouts++;
            if (total_timeouts > 5)
            {
                common::logError("InputWebcam::imageAcquisitionLoop - Select timeout "
                                 "reached. Please unplug and plug "
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

        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0)
        {
            if (errno == EAGAIN || errno == EIO)
            {
                continue;
            }

            common::errno_log("InputWebcam::imageAcquisitionLoop - VIDIOC_DQBUF");
            break;
        }

        if (buf.index >= bufrequest_.count)
        {
            common::errno_log("InputWebcam::imageAcquisitionLoop - INVALID INDEX in BUFF. Aborting.");
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
            total_discarded++;
            if (total_discarded > max_discard_count)
            {
                common::logError("InputWebcam::imageAcquisitionLoop - Too many discarded frames (%d), exiting loop",
                                  total_discarded);
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
        if (skip_counter < amount_of_skip_frames)
        {
            common::logInfo("InputWebcam::imageAcquisitionLoop - Skipping frame %d", buf.index);
            if (!requeueFrame(buf))
            {
                common::logError("InputWebcam::imageAcquisitionLoop - Failed to requeue frame %d", buf.index);
                break;
            }
            skip_counter++;
            continue;
        }

        Profiler::getInstance().stop(name_.c_str(), "Waiting for OS camera frame");

        Profiler::getInstance().start(name_.c_str(), "Input image decoding");

        Image src_image(static_cast<unsigned char*>(buffers_[buf.index].start), buf.bytesused, false);
        src_image.info.TJPixelFormat = TJPF_RGB;


        if (read_image_header)
        {
            unsigned long raw_needed_size = 0;
            const bool valid_image = decoder_->decodeHeader(src_image, raw_needed_size);
            if (!valid_image)
            {
                common::logInfo(
                    "InputWebcam::imageAcquisitionLoop - Invalid input image. Discarded. Total discarded: %d",
                    ++total_discarded_header);
                if (!requeueFrame(buf))
                {
                    break;
                }
                continue;
            }

            if (image_tmp.size() != raw_needed_size)
            {
                image_tmp.resize(raw_needed_size);
                common::logWarn("InputWebcam::imageAcquisitionLoop - Reallocating raw image buffer to %lu - %s",
                                 imageTmp.size(), common::format_size(imageTmp.size()));
            }

            camera_input_info = src_image.info;
            read_image_header = false;
        }


        if (!decoder_->decode(src_image, image_tmp))
        {
            read_image_header = true;
            if (!requeueFrame(buf))
            {
                break;
            }
            if (decoding_failure_count > 10)
            {
                common::logError("InputWebcam::imageAcquisitionLoop - Failed to decode image after %d attempts",
                                  decoding_failure_count);
                break;
            }

            decoding_failure_count++;
            common::logError("InputWebcam::imageAcquisitionLoop - Decoding failed %d times", decoding_failure_count);
            continue;
        }

        decoding_failure_count = 0;
        image_tmp.info = camera_input_info; // TODO(arroyo): FIXME: Here we are resetting the
                                         // decoder state. We should use the decoder state
        {
            std::lock_guard<std::mutex> lock(imageMutex_);
            if (!latestImage_ || latestImage_->size() != image_tmp.size())
            {
                latestImage_ = std::make_unique<Image>(image_tmp.size());
            }
            // Copy image to latest image
            latestImage_->copyFrom(image_tmp);
        }

        Profiler::getInstance().stop(name_.c_str(), "Input image decoding");

        // Others can process the image

        if (!requeueFrame(buf))
        {
            break;
        }
    }

    common::logWarn("InputWebcam::imageAcquisitionLoop thread dead for device: %s", device_path_.c_str());
}


bool InputWebcam::GetImage(std::unique_ptr<Image>& out_image)
{
    // Get the most recent frame from queue, discarding older ones
    std::unique_ptr<Image> latest_frame;
    if (latestImage_)
    {
        std::lock_guard<std::mutex> lock(imageMutex_);
        out_image = latestImage_->deepCopy();
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
                    common::errno_log("InputWebcam::cleanupBuffers - munmap failed");
                }
            }
        }
        free(buffers_);
        buffers_ = nullptr;
    }
}

bool InputWebcam::reconfigureFormat(int format_index, int size_index, int fps_index)
{
    const auto& new_format = capabilities_.formats[formatIndex].sizes[sizeIndex];
    common::logInfo("InputWebcam::reconfigureFormat - Reconfiguring device %s with format Index %d, size index %d and "
                     "fps index %d that is %dx%d with %d fps",
                     name_.c_str(), formatIndex, sizeIndex, fpsIndex, new_format.width, new_format.height,
                     new_format.getFps(new_format.selectedFPS));

    // Stop current operation
    const bool was_running = isRunning();
    if (was_running)
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
        if (was_running)
        {
            start(); // Try to restart with old config
        }
        return false;
    }

    Format selected_format = capabilities_.formats[formatIndex];

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
    if (was_running)
    {
        if (!start())
        {
            common::logError("InputWebcam::reconfigureFormat - Failed to restart device");
            return false;
        }
    }
    return true;
}
