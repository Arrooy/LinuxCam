#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <rtc/rtc.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include "LinuxFace/Image/image.h"
#include "LinuxFace/web/IStreamTransport.h"
#include "config.hpp"

namespace linuxface
{
namespace web
{

/**
 * WebRTC transport using H.264 encoding via FFmpeg and libdatachannel
 * Provides low-latency video streaming with automatic codec negotiation
 */
class WebRTCTransport : public IStreamTransport
{
  public:
    explicit WebRTCTransport(const StreamingConfig& config);
    ~WebRTCTransport() override;

    // Disable copy/move
    WebRTCTransport(const WebRTCTransport&) = delete;
    WebRTCTransport& operator=(const WebRTCTransport&) = delete;

    bool start() override;
    void stop() override;
    bool isRunning() const override { return running_; }
    void submitFrame(const std::unique_ptr<Image>& frame) override;
    bool hasActiveConnections() const override;
    const char* getName() const override { return "WebRTC/H.264"; }
    Stats getStats() const override;

    // WebRTC signaling
    struct SignalingMessage
    {
        std::string type;       // "offer", "answer", "candidate"
        std::string sdp;        // SDP for offer/answer
        std::string candidate;  // ICE candidate string
        std::string mid;        // Media ID for ICE candidate
    };

    // Create new peer connection and return local SDP offer
    std::string createPeerConnection(const std::string& peerId);
    
    // Process remote SDP answer from client
    bool processAnswer(const std::string& peerId, const std::string& sdp);
    
    // Process ICE candidate from client
    bool processIceCandidate(const std::string& peerId, const std::string& candidate, const std::string& mid);
    
    // Remove peer connection
    void removePeerConnection(const std::string& peerId);

  private:
    struct PeerConnection
    {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::Track> track;
        std::string peerId;
        bool isNegotiating{false};
        std::chrono::steady_clock::time_point lastFrameTime;
    };

    void workerThread();
    bool initializeEncoder(unsigned int width, unsigned int height);
    void cleanupEncoder();
    bool encodeFrame(const std::unique_ptr<Image>& frame, std::vector<uint8_t>& encodedData);
    void broadcastEncodedFrame(const std::vector<uint8_t>& data, bool isKeyFrame);

    StreamingConfig config_;
    std::atomic<bool> running_{false};

    // Frame queue
    std::queue<std::unique_ptr<Image>> frameQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCV_;
    std::thread workerThread_;

    // Peer connections
    std::map<std::string, PeerConnection> peers_;
    mutable std::mutex peersMutex_;

    // FFmpeg encoder state
    AVCodecContext* codecContext_{nullptr};
    AVFrame* avFrame_{nullptr};
    AVPacket* avPacket_{nullptr};
    SwsContext* swsContext_{nullptr};
    unsigned int lastEncoderWidth_{0};
    unsigned int lastEncoderHeight_{0};
    int frameCounter_{0};

    // Statistics
    mutable std::mutex statsMutex_;
    Stats stats_;
};

} // namespace web
} // namespace linuxface
