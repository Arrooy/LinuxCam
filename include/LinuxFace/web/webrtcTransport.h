#pragma once

#include <atomic>
#include <chrono>
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
    void processDecodedH264(const std::string& peerId, const std::vector<std::byte>& h264Data);

    // Public for testing - H.264 conversion and codec operations
    std::vector<uint8_t> convertAVCCToAnnexB(const std::vector<std::byte>& avccData);
    std::unique_ptr<Image> decodeH264Frame(const std::vector<std::byte>& h264Data);
    std::unique_ptr<Image> convertDecodedFrameToImage();
    bool initializeEncoder(unsigned int width, unsigned int height);
    bool encodeFrame(const std::unique_ptr<Image>& frame, std::vector<uint8_t>& encodedData);
    bool detectCompleteFrame(const std::vector<uint8_t>& buffer,
                             const std::chrono::steady_clock::time_point& lastPacketTime);

  private:
    enum class ClientType
    {
        UNKNOWN,
        DESKTOP_BROWSER, // Firefox, Chrome on desktop - sends complete Annex B frames
        MOBILE_SAFARI    // iPhone Safari - fragments NAL units across messages
    };

    struct PeerConnection
    {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::Track> track;
        std::shared_ptr<rtc::DataChannel> dataChannel; // For receiving camera frames
        std::string peerId;
        ClientType clientType{ClientType::UNKNOWN};
        bool isNegotiating{false};
        bool isSendingFrames{false}; // Track if peer is actively sending camera frames
        std::chrono::steady_clock::time_point lastFrameTime;
        std::chrono::steady_clock::time_point lastInboundFrameTime;

        // Frame sequencing for chunked transmission (server→client)
        uint32_t nextOutgoingFrameSequence{0};

        // Adaptive bitrate metrics
        size_t bytesSent{0};
        size_t framesSent{0};
        std::chrono::steady_clock::time_point lastBitrateCheck;
        int currentBitrate{0};

        // Per-peer H.264 buffer for mobile clients that fragment NAL units
        std::vector<uint8_t> h264PacketBuffer;
        std::chrono::steady_clock::time_point lastH264PacketTime;
        bool h264BufferHasPendingData{false};

        // Chunk reassembly buffer for large frames from client
        struct ChunkBuffer
        {
            std::vector<std::vector<uint8_t>> chunks;
            size_t receivedCount{0};
            size_t totalChunks{0};
            bool isKeyframe{false};
            std::chrono::steady_clock::time_point firstChunkTime;
            uint32_t frameSequence{0}; // Unique sequence number for this frame
        };
        std::map<uint32_t, ChunkBuffer> incomingChunkBuffers; // Key: frame sequence number
        uint32_t nextIncomingFrameSequence{0}; // Sequence counter for incoming frames
    };

    void workerThread();
    void cleanupEncoder();
    void broadcastEncodedFrame(const std::vector<uint8_t>& data, bool isKeyFrame);

    // Adaptive bitrate control
    void adjustBitrateIfNeeded();
    void updateEncoderBitrate(int newBitrate);

    // H.264 decoder for incoming video
    // If avcc is provided, it will be used to set decoder extradata (AVCDecoderConfigurationRecord)
    bool initializeDecoder(const std::vector<uint8_t>* avcc = nullptr);
    void cleanupDecoder();

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
    static constexpr int MIN_BITRATE = 1000000;  // 1 Mbps minimum
    static constexpr int MAX_BITRATE = 15000000; // 15 Mbps maximum
    static constexpr int BITRATE_STEP = 500000;  // 500 Kbps adjustment step
    static constexpr auto PEER_INACTIVITY_TIMEOUT = std::chrono::milliseconds(2000);

    // FFmpeg decoder state for incoming H.264
    AVCodecContext* decoderContext_{nullptr};
    AVFrame* decodedFrame_{nullptr};
    AVPacket* decoderPacket_{nullptr};
    SwsContext* decoderSwsContext_{nullptr};
    std::mutex decoderMutex_; // Protect decoder state

    // Client-specific H.264 processing
    void processDesktopH264(PeerConnection& peer, const std::vector<std::byte>& h264Data);

    // Chunk protocol helpers (bidirectional)
    struct ChunkHeader
    {
        uint8_t flags;
        uint16_t chunkIdx;
        uint16_t totalChunks;
        uint32_t frameSequence; // Unique frame identifier to prevent collisions
        
        bool isFirstChunk() const { return (flags & 0x01) != 0; }
        bool isLastChunk() const { return (flags & 0x02) != 0; }
        bool isKeyframe() const { return (flags & 0x04) != 0; }
    };
    
    bool parseChunkHeader(const std::vector<std::byte>& data, ChunkHeader& header);
    bool validateChunkHeader(const ChunkHeader& header, size_t dataSize);
    void handleIncomingChunk(PeerConnection& peer, const std::vector<std::byte>& chunkData, const ChunkHeader& header);
    void sendChunkedData(rtc::DataChannel& dc, const std::vector<uint8_t>& data, bool isKeyframe, 
                        int64_t sendMs, uint32_t frameSequence);

    // Statistics
    mutable std::mutex statsMutex_;
    Stats stats_;
};

} // namespace web
} // namespace linuxface
