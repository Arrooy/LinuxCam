#include "LinuxFace/web/wsInputDevice.h"

#include <utility>

#include "LinuxFace/codecFactory.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

wsInputDevice::wsInputDevice(const WebServerConfig& config)
    : Webcam("WebSocket Input", "websocket://" + config.host + ":" + std::to_string(config.port),
             WebcamType::VIRTUAL_INPUT, 0, 0)
    , webServerConfig_(config)
{
    setCurrentlySelected(true);
}

wsInputDevice::~wsInputDevice()
{
    stop();
}

bool wsInputDevice::setupDevice()
{
    ConfigBuilder configBuilder;
    configBuilder.imageFormat(ImageFormat::JPEG).pixelFormat(TJPF_RGB);
    // Decoder doesnt know its size yet. JPEG doesn't need it.
    decoder_ = CodecFactory::create<Decoder>(configBuilder);

    if (!decoder_)
    {
        common::logError("wsInputDevice::setupDevice - Failed to create decoder");
        return false;
    }

    ready_ = true;
    common::logInfo("wsInputDevice::setupDevice - Ready to receive frames");
    return true;
}

bool wsInputDevice::start()
{
    if (!ready_)
    {
        common::logError("wsInputDevice::start - Websocket device not ready");
        return false;
    }

    if (running_.load())
    {
        common::logInfo("wsInputDevice::start - Already running");
        return true;
    }

    common::logInfo("wsInputDevice::start - Starting frame processing");
    running_ = true;

    // Start frame processing thread
    processingThread_ = std::thread(&wsInputDevice::processFrameQueue, this);

    return true;
}

bool wsInputDevice::stop()
{
    if (!running_.load())
    {
        return true;
    }

    common::logInfo("wsInputDevice::stop - Stopping");
    running_ = false;

    // Wake up processing thread
    queueCondition_.notify_all();

    if (processingThread_.joinable())
    {
        processingThread_.join();
    }

    return true;
}

bool wsInputDevice::isRunning()
{
    return running_.load();
}

bool wsInputDevice::getImage(std::unique_ptr<Image>& outImage)
{
    Profiler::ScopedProfilerSpan span("WebSocket Input", "Get new frame");
    // Get the most recent frame from queue, discarding older ones
    std::lock_guard<std::mutex> lock(imageMutex_);
    if (latestImage_)
    {
        outImage = latestImage_->deepCopy();
        return true;
    }

    return false;
}

// Drogon produces images and pushes them here
void wsInputDevice::pushFrame(const std::vector<uint8_t>& jpegData)
{
    std::lock_guard<std::mutex> lock(queueMutex_);

    // Drop oldest frame if queue is full
    if (frameQueue_.size() >= MAX_QUEUE_SIZE)
    {
        frameQueue_.pop();
        common::logWarn("wsInputDevice::pushFrame - Frame queue full, dropping oldest frame");
    }

    frameQueue_.push(jpegData);
    queueCondition_.notify_one();
}

void wsInputDevice::processFrameQueue()
{
    common::logInfo("wsInputDevice::processFrameQueue - Started");

    bool readImageHeader = true;
    Image imageTmp;
    ImageMetadata wsInputInfo;

    while (running_.load())
    {
        std::unique_lock<std::mutex> lock(queueMutex_);

        // Wait for frames or stop signal
        queueCondition_.wait(lock, [this] { return !frameQueue_.empty() || !running_.load(); });

        if (!running_.load())
        {
            break;
        }

        if (frameQueue_.empty())
        {
            continue;
        }

        // Get next frame
        std::vector<uint8_t> jpegData = std::move(frameQueue_.front());
        frameQueue_.pop();
        lock.unlock();

        // Decode JPEG frame
        Profiler::ScopedProfilerSpan span("WebSocket Input", "Decode JPEG frame");

        // Create source image from JPEG data (wraps the buffer, doesn't copy)
        Image srcImage(const_cast<unsigned char*>(jpegData.data()), jpegData.size(), false);
        srcImage.info.TJPixelFormat = TJPF_RGB;
        srcImage.info.format = ImageFormat::JPEG;

        // Decode header on first frame or after decode failure to get dimensions
        if (readImageHeader)
        {
            unsigned long rawNeededSize = 0;

            if (!decoder_->decodeHeader(srcImage, rawNeededSize))
            {
                common::logError("wsInputDevice::processFrameQueue - Failed to decode JPEG header");
                continue;
            }

            // Allocate decode buffer if needed or resize if dimensions changed
            if (imageTmp.size() != rawNeededSize)
            {
                imageTmp.resize(rawNeededSize);
                common::logInfo("wsInputDevice - Allocated decode buffer: %dx%d (%lu bytes)", srcImage.info.width,
                                srcImage.info.height, rawNeededSize);
            }

            wsInputInfo = srcImage.info;
            readImageHeader = false;
        }

        // Decode frame into pre-allocated buffer
        if (!decoder_->decode(srcImage, imageTmp))
        {
            common::logError("wsInputDevice::processFrameQueue - Failed to decode JPEG frame");
            readImageHeader = true; // Force header re-read on next frame
            continue;
        }

        // Restore metadata after decode
        imageTmp.info = wsInputInfo;

        // Store as latest image
        {
            std::lock_guard<std::mutex> imageLock(imageMutex_);
            if (!latestImage_)
            {
                latestImage_ = std::make_unique<Image>(imageTmp.size());
            }
            latestImage_->copyFrom(imageTmp);
        }
    }

    common::logInfo("wsInputDevice::processFrameQueue - Stopped");
}
