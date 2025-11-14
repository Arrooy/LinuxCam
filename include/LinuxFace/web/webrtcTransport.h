#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <rtc/rtc.hpp>
#include <string>
#include <thread>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <turbojpeg.h>
}

#include "LinuxFace/Image/image.h"
#include "LinuxFace/web/IStreamTransport.h"
#include "config.hpp"

namespace linuxface
{

// Forward declaration
class wsInputDevice;

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
        std::string type;      // "offer", "answer", "candidate"
        std::string sdp;       // SDP for offer/answer
        std::string candidate; // ICE candidate string
        std::string mid;       // Media ID for ICE candidate
    };

    // Process remote SDP offer from browser and return SDP answer
    std::string processOfferAndCreateAnswer(const std::string& peerId, const std::string& offerSdp);

    // Process ICE candidate from client
    bool processIceCandidate(const std::string& peerId, const std::string& candidate, const std::string& mid);

    // Remove peer connection
    void removePeerConnection(const std::string& peerId);

    // Set input device for receiving camera frames via data channel
    void setInputDevice(std::shared_ptr<wsInputDevice> device);

    // Process decoded H.264 frame (called by H264DecodingHandler)
    void processDecodedH264(const std::vector<std::byte>& h264Data);

  private:
    struct PeerConnection
    {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::Track> track;
        std::shared_ptr<rtc::DataChannel> dataChannel; // For receiving camera frames
        std::string peerId;
        bool isNegotiating{false};
        bool isSendingFrames{false}; // Track if peer is actively sending camera frames
        std::chrono::steady_clock::time_point lastFrameTime;
        
        // Adaptive bitrate metrics
        size_t bytesSent{0};
        size_t framesSent{0};
        std::chrono::steady_clock::time_point lastBitrateCheck;
        int currentBitrate{0};
    };

    void workerThread();
    bool initializeEncoder(unsigned int width, unsigned int height);
    void cleanupEncoder();
    bool encodeFrame(const std::unique_ptr<Image>& frame, std::vector<uint8_t>& encodedData);
    void broadcastEncodedFrame(const std::vector<uint8_t>& data, bool isKeyFrame);
    
    // Adaptive bitrate control
    void adjustBitrateIfNeeded();
    void updateEncoderBitrate(int newBitrate);

    // H.264 decoder for incoming video
    bool initializeDecoder();
    void cleanupDecoder();
    std::vector<uint8_t> convertAVCCToAnnexB(const std::vector<std::byte>& avccData);
    std::unique_ptr<Image> decodeH264Frame(const std::vector<std::byte>& h264Data);

    StreamingConfig config_;
    std::atomic<bool> running_{false};

    // Input device for receiving camera frames
    std::shared_ptr<wsInputDevice> inputDevice_;
    std::mutex inputDeviceMutex_;

    // Latest frame slot (real-time streaming: keep only most recent frame)
    std::unique_ptr<Image> latestFrame_;
    std::mutex frameMutex_;
    std::condition_variable frameCV_;
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
    
    // Adaptive bitrate state
    int currentEncoderBitrate_{0};
    std::chrono::steady_clock::time_point lastBitrateAdjustment_;
    static constexpr int MIN_BITRATE = 1000000;   // 1 Mbps minimum
    static constexpr int MAX_BITRATE = 15000000;  // 15 Mbps maximum
    static constexpr int BITRATE_STEP = 500000;   // 500 Kbps adjustment step

    // FFmpeg decoder state for incoming H.264
    AVCodecContext* decoderContext_{nullptr};
    AVFrame* decodedFrame_{nullptr};
    AVPacket* decoderPacket_{nullptr};
    SwsContext* decoderSwsContext_{nullptr};
    std::mutex decoderMutex_;  // Protect decoder state

    // Statistics
    mutable std::mutex statsMutex_;
    Stats stats_;
};

} // namespace web
} // namespace linuxface
