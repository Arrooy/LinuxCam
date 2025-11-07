#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/codecFactory.h"
#include "LinuxFace/web/IStreamTransport.h"

namespace linuxface
{
namespace web
{

// Forward declaration to avoid including Drogon in header
class videoStreamController;

/**
 * StreamBroadcaster - Manages encoding and broadcasting of processed frames to WebSocket clients
 * 
 * Responsibilities:
 * - Reuses JPEG encoder for performance
 * - Asynchronous encoding in background thread
 * - Rate limiting and frame dropping under load
 * - Decouples WebSocket broadcast from main render loop
 */
class StreamBroadcaster : public IStreamTransport
{
  public:
    struct Config
    {
        unsigned int maxQueueSize;      // Drop frames if queue exceeds this
        int jpegQuality;                // JPEG compression quality (1-100)
        bool enabled;                   // Enable/disable broadcasting

        Config() 
            : maxQueueSize(1)
            , jpegQuality(85)
            , enabled(true)
        {}
    };

    explicit StreamBroadcaster(const Config& config = Config{});
    ~StreamBroadcaster();

    // Disable copy/move
    StreamBroadcaster(const StreamBroadcaster&) = delete;
    StreamBroadcaster& operator=(const StreamBroadcaster&) = delete;

    // IStreamTransport interface implementation
    bool start() override;
    void stop() override;
    bool isRunning() const override { return running_; }
    void submitFrame(const std::unique_ptr<Image>& frame) override;
    bool hasActiveConnections() const override;
    const char* getName() const override { return "JPEG/WebSocket"; }
    IStreamTransport::Stats getStats() const override;

    /**
     * Update JPEG quality dynamically
     */
    void setJpegQuality(int quality);

  private:
    void workerThread();
    bool encodeAndBroadcast(const std::unique_ptr<Image>& frame);
    void updateEncoder(unsigned int width, unsigned int height, unsigned char pixelSizeBytes);

    Config config_;
    std::atomic<bool> running_{false};

    // Thread-safe queue for frames
    std::queue<std::unique_ptr<Image>> frameQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCV_;

    // Background worker thread
    std::thread workerThread_;

    // Reusable encoder - created once, reused across frames
    std::unique_ptr<Encoder> encoder_;
    unsigned int lastEncoderWidth_ = 0;
    unsigned int lastEncoderHeight_ = 0;
    unsigned char lastPixelSizeBytes_ = 0;

    // Statistics
    mutable std::mutex statsMutex_;
    IStreamTransport::Stats stats_;
};

} // namespace web
} // namespace linuxface
