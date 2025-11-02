#include "LinuxFace/web/streamBroadcaster.h"

#include <drogon/DrClassMap.h>

#include "LinuxFace/common.h"
#include "LinuxFace/web/videoStreamController.h"

namespace linuxface
{
namespace web
{

StreamBroadcaster::StreamBroadcaster(const Config& config) : config_(config)
{
}

StreamBroadcaster::~StreamBroadcaster()
{
    stop();
}

bool StreamBroadcaster::start()
{
    if (running_)
    {
        return true;
    }

    if (!config_.enabled)
    {
        common::logInfo("StreamBroadcaster: Broadcasting disabled in config");
        return false;
    }

    running_ = true;

    workerThread_ = std::thread(&StreamBroadcaster::workerThread, this);

    common::logInfo("StreamBroadcaster: Started (JPEG quality: %d, max queue: %u)", config_.jpegQuality,
                    config_.maxQueueSize);
    return true;
}

void StreamBroadcaster::stop()
{
    if (!running_)
    {
        return;
    }

    common::logInfo("StreamBroadcaster: Stopping...");

    // Signal worker thread to stop by setting running to false
    running_ = false;
    queueCV_.notify_all();

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }

    // Clear queue
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!frameQueue_.empty())
        {
            frameQueue_.pop();
        }
    }

    encoder_.reset();

    common::logInfo("StreamBroadcaster: Stopped");
}

void StreamBroadcaster::submitFrame(const std::unique_ptr<Image>& frame)
{
    if (!running_ || !frame)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.framesSubmitted++;
    }

    std::unique_lock<std::mutex> lock(queueMutex_);

    // Drop frame if queue is full (backpressure)
    if (frameQueue_.size() >= config_.maxQueueSize)
    {
        {
            std::lock_guard<std::mutex> statsLock(statsMutex_);
            stats_.framesDropped++;
        }
        return;
    }

    // Deep copy frame for async processing
    frameQueue_.push(frame->deepCopy());
    lock.unlock();
    queueCV_.notify_one();
}

void StreamBroadcaster::workerThread()
{
    common::logInfo("StreamBroadcaster: Worker thread started");

    while (running_)
    {
        std::unique_ptr<Image> frame;

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCV_.wait(lock, [this]() { return !frameQueue_.empty() || !running_; });

            if (!running_)
            {
                break;
            }

            if (!frameQueue_.empty())
            {
                frame = std::move(frameQueue_.front());
                frameQueue_.pop();
            }
        }

        if (frame)
        {
            encodeAndBroadcast(frame);
        }
    }

    common::logInfo("StreamBroadcaster: Worker thread stopped");
}

void StreamBroadcaster::updateEncoder(unsigned int width, unsigned int height, unsigned char pixelSizeBytes)
{
    if (encoder_ && lastEncoderWidth_ == width && lastEncoderHeight_ == height && lastPixelSizeBytes_ == pixelSizeBytes)
    {
        return; // Encoder already configured for this resolution and format
    }

    // Determine pixel format based on the image's pixel size
    TJPF pixelFormat = (pixelSizeBytes == 4) ? TJPF_RGBA : TJPF_RGB;

    ConfigBuilder encoderConfig;
    encoderConfig.imageFormat(ImageFormat::JPEG)
        .pixelFormat(pixelFormat)
        .width(width)
        .height(height)
        .quality(config_.jpegQuality)
        .chrominanceSubsampling(TJSAMP_420);

    encoder_ = CodecFactory::create<Encoder>(encoderConfig);

    if (!encoder_)
    {
        common::logError("StreamBroadcaster: Failed to create encoder for %ux%u", width, height);
        return;
    }

    lastEncoderWidth_ = width;
    lastEncoderHeight_ = height;
    lastPixelSizeBytes_ = pixelSizeBytes;

    common::logInfo("StreamBroadcaster: Encoder updated to %ux%u, pixel format: %s", 
                    width, height, (pixelSizeBytes == 4) ? "RGBA" : "RGB");
}

bool StreamBroadcaster::encodeAndBroadcast(const std::unique_ptr<Image>& frame)
{
    if (!frame)
    {
        return false;
    }

    // Update encoder if resolution or format changed
    updateEncoder(frame->info.width, frame->info.height, frame->info.pixelSizeBytes);

    if (!encoder_)
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.encodingErrors++;
        return false;
    }

    // Encode to JPEG
    Image encodedImage(encoder_->encodeSizeInBytes());
    unsigned long compressedSize = 0;

    if (!encoder_->encode(*frame, encodedImage, compressedSize))
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.encodingErrors++;
        common::logError("StreamBroadcaster: Failed to encode frame");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.framesEncoded++;
    }

    // Broadcast to WebSocket clients
    auto controller = drogon::DrClassMap::getSingleInstance<videoStreamController>();
    if (!controller)
    {
        common::logError("StreamBroadcaster: Failed to get videoStreamController");
        return false;
    }

    std::vector<uint8_t> jpegData(encodedImage.data(), encodedImage.data() + compressedSize);
    controller->sendProcessedFrame(jpegData);

    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.framesBroadcast++;
    }

    return true;
}

StreamBroadcaster::Stats StreamBroadcaster::getStats() const
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void StreamBroadcaster::setJpegQuality(int quality)
{
    if (quality < 1 || quality > 100)
    {
        common::logError("StreamBroadcaster: Invalid quality value %d, must be 1-100", quality);
        return;
    }

    config_.jpegQuality = quality;
    common::logInfo("StreamBroadcaster: JPEG quality updated to %d", quality);

    // Encoder will be recreated on next frame with new quality
    encoder_.reset();
}

} // namespace web
} // namespace linuxface
