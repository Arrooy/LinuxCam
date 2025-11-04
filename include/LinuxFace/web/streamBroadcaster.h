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
class StreamBroadcaster
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

    /**
     * Submit a frame for encoding and broadcast
     * Non-blocking - drops frame if queue is full
     */
    void submitFrame(const std::unique_ptr<Image>& frame);

    /**
     * Start the background encoding/broadcast thread
     */
    bool start();

    /**
     * Stop the background thread and clear queue
     */
    void stop();

    /**
     * Check if broadcaster is running
     */
    bool isRunning() const { return running_; }

    /**
     * Update JPEG quality dynamically
     */
    void setJpegQuality(int quality);

    /**
     * Get statistics for monitoring
     */
    struct Stats
    {
        uint64_t framesSubmitted;
        uint64_t framesDropped;
        uint64_t framesEncoded;
        uint64_t framesBroadcast;
        uint64_t encodingErrors;

        Stats()
            : framesSubmitted(0)
            , framesDropped(0)
            , framesEncoded(0)
            , framesBroadcast(0)
            , encodingErrors(0)
        {}
    };
    Stats getStats() const;

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
    Stats stats_;
};

} // namespace web
} // namespace linuxface
