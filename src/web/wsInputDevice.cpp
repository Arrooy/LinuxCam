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
    // Consume the most recent frame
    std::lock_guard<std::mutex> lock(imageMutex_);
    if (latestImage_)
    {
        outImage = std::move(latestImage_);
        latestImage_.reset(); // Clear so next call returns false until new frame arrives
        return true;
    }

    return false;
}

// WebSocket path: receives JPEG frames
void wsInputDevice::pushFrame(const std::vector<uint8_t>& jpegData)
{
    std::lock_guard<std::mutex> lock(queueMutex_);

    // For real-time video, drop ALL queued frames and keep only the latest
    if (frameQueue_.size() >= MAX_QUEUE_SIZE)
    {
        size_t droppedCount = 0;
        while (!frameQueue_.empty())
        {
            frameQueue_.pop();
            droppedCount++;
        }
        common::logWarn("wsInputDevice::pushFrame - Frame queue full, dropped %d old frames to maintain real-time",
                        droppedCount);
    }

    frameQueue_.push(jpegData);
    queueCondition_.notify_one();
}

// WebRTC path: receives decoded RGB frames with move semantics (zero-copy)
void wsInputDevice::pushRGBFrame(Image&& rgbImage)
{
    if (!running_.load())
    {
        common::logWarn("wsInputDevice::pushRGBFrame - Device not running, frame dropped");
        return;
    }

    // Validate image format
    if (rgbImage.info.format != ImageFormat::RAW && rgbImage.info.format != ImageFormat::RGB)
    {
        common::logError("wsInputDevice::pushRGBFrame - Invalid image format: %s (expected RAW or RGB)",
                         fromImageFormatToString(rgbImage.info.format).c_str());
        return;
    }

    if (rgbImage.info.pixelSizeBytes != 3)
    {
        common::logError("wsInputDevice::pushRGBFrame - Invalid pixel size: %d (expected 3 for RGB)",
                         rgbImage.info.pixelSizeBytes);
        return;
    }

    // Store RGB image with move semantics (zero-copy)
    storeRGBImage(std::move(rgbImage));
}

void wsInputDevice::signalResolutionChange()
{
    std::lock_guard<std::mutex> lock(queueMutex_);

    // Clear all queued frames with old resolution
    while (!frameQueue_.empty())
    {
        frameQueue_.pop();
    }

    // Force header re-read on next frame
    needsHeaderRead_.store(true);

    common::logInfo("wsInputDevice::signalResolutionChange - Queue cleared, decoder will reset on next frame");
}

// Move-only RGB image storage (zero-copy for both WebSocket and WebRTC paths)
void wsInputDevice::storeRGBImage(Image&& rgbImage)
{
    Profiler::ScopedProfilerSpan span("WebSocket Input", "Store RGB image (move)");

    std::lock_guard<std::mutex> imageLock(imageMutex_);

    // Move image directly (zero-copy transfer)
    latestImage_ = std::make_unique<Image>(std::move(rgbImage));

    common::logDebugVerbose("wsInputDevice::storeRGBImage - Moved RGB buffer: %dx%d (%zu bytes)",
                            latestImage_->info.width, latestImage_->info.height, latestImage_->size());
}

void wsInputDevice::processFrameQueue()
{
    common::logInfo("wsInputDevice::processFrameQueue - Started");

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

        // Check if we need to read header (first frame or after resolution change)
        bool readImageHeader = needsHeaderRead_.load();

        // Decode header on first frame or after resolution change
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
            needsHeaderRead_.store(false);
        }

        // Decode frame into pre-allocated buffer
        if (!decoder_->decode(srcImage, imageTmp))
        {
            common::logError("wsInputDevice::processFrameQueue - Failed to decode JPEG frame");
            needsHeaderRead_.store(true); // Force header re-read on next frame
            continue;
        }

        // Restore metadata after decode
        imageTmp.info = wsInputInfo;

        // Move image to storage (zero-copy, buffer will be reallocated on next iteration if needed)
        storeRGBImage(std::move(imageTmp));
    }

    common::logInfo("wsInputDevice::processFrameQueue - Stopped");
}
