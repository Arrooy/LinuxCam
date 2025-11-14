#include "LinuxFace/web/webrtcTransport.h"

#include <chrono>
#include <cstdarg>
#include <cstring>
#include <sstream>

#include "LinuxFace/common.h"
#include "LinuxFace/web/wsInputDevice.h"

extern "C"
{
#include <libavutil/log.h>
}

namespace linuxface
{
namespace web
{

namespace
{
// Custom FFmpeg log callback to filter out harmless "no frame!" warnings
void ffmpegLogCallback(void* ptr, int level, const char* fmt, va_list vl)
{
    if (level > AV_LOG_ERROR)
    {
        return;
    }

    // Filter specific "no frame!" warning from H.264 decoder
    // This occurs when SPS/PPS config packets are sent without frame data (expected behavior)
    char message[1024];
    vsnprintf(message, sizeof(message), fmt, vl);

    if (std::strstr(message, "no frame") != nullptr)
    {
        return;
    }

    // Forward other error messages to default handler
    av_log_default_callback(ptr, level, fmt, vl);
}
}  // namespace

WebRTCTransport::WebRTCTransport(const StreamingConfig& config) : config_(config)
{
}

WebRTCTransport::~WebRTCTransport()
{
    stop();
}

bool WebRTCTransport::start()
{
    if (running_)
    {
        return true;
    }

    // Install custom FFmpeg log callback to filter harmless "no frame!" warnings
    av_log_set_callback(ffmpegLogCallback);

    common::logInfo("WebRTCTransport: Starting H.264/WebRTC streaming (bitrate: %d, fps: %d, preset: %s)",
                    config_.webrtcBitrate, config_.webrtcFramerate, config_.webrtcPreset.c_str());

    running_ = true;
    workerThread_ = std::thread(&WebRTCTransport::workerThread, this);

    common::logInfo("WebRTCTransport: Started successfully");
    return true;
}

void WebRTCTransport::stop()
{
    if (!running_)
    {
        return;
    }

    common::logInfo("WebRTCTransport: Stopping...");

    running_ = false;
    frameCV_.notify_all();

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }

    // Close all peer connections
    {
        std::lock_guard<std::mutex> lock(peersMutex_);
        for (auto& [peerId, peer] : peers_)
        {
            if (peer.pc)
            {
                peer.pc->close();
            }
        }
        peers_.clear();
    }

    // Clear latest frame
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        latestFrame_.reset();
    }

    cleanupEncoder();
    cleanupDecoder();

    common::logInfo("WebRTCTransport: Stopped");
}

void WebRTCTransport::submitFrame(const std::unique_ptr<Image>& frame)
{
    if (!running_ || !frame)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.framesSubmitted++;
    }

    std::unique_lock<std::mutex> lock(frameMutex_);

    // For real-time streaming: always replace with latest frame (lowest latency)
    if (latestFrame_)
    {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        stats_.framesDropped++;
    }

    latestFrame_ = frame->deepCopy();
    lock.unlock();
    frameCV_.notify_one();
}

bool WebRTCTransport::hasActiveConnections() const
{
    std::lock_guard<std::mutex> lock(peersMutex_);
    return !peers_.empty();
}

IStreamTransport::Stats WebRTCTransport::getStats() const
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void WebRTCTransport::workerThread()
{
    common::logInfo("WebRTCTransport: Worker thread started");

    while (running_)
    {
        std::unique_ptr<Image> frame;

        {
            std::unique_lock<std::mutex> lock(frameMutex_);
            frameCV_.wait(lock, [this]() { return latestFrame_ != nullptr || !running_; });

            if (!running_)
            {
                break;
            }

            if (latestFrame_)
            {
                frame = std::move(latestFrame_);
            }
        }

        if (frame)
        {
            // Skip encoding if no active peers
            if (!hasActiveConnections())
            {
                continue;
            }

            // Initialize encoder if needed
            if (!codecContext_ || lastEncoderWidth_ != frame->info.width || lastEncoderHeight_ != frame->info.height)
            {
                cleanupEncoder();
                if (!initializeEncoder(frame->info.width, frame->info.height))
                {
                    std::lock_guard<std::mutex> lock(statsMutex_);
                    stats_.encodingErrors++;
                    continue;
                }
            }

            // Encode frame
            std::vector<uint8_t> encodedData;
            if (encodeFrame(frame, encodedData))
            {
                {
                    std::lock_guard<std::mutex> lock(statsMutex_);
                    stats_.framesEncoded++;
                }

                // Determine if this is a key frame
                bool isKeyFrame = (avPacket_ && (avPacket_->flags & AV_PKT_FLAG_KEY));

                // Broadcast to all connected peers
                broadcastEncodedFrame(encodedData, isKeyFrame);

                // Check if bitrate adjustment is needed (every 2 seconds)
                adjustBitrateIfNeeded();
            }
            else
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.encodingErrors++;
            }
        }
    }

    common::logInfo("WebRTCTransport: Worker thread stopped");
}

bool WebRTCTransport::initializeEncoder(unsigned int width, unsigned int height)
{
    common::logInfo("WebRTCTransport: Initializing H.264 encoder for %ux%u", width, height);

    // Try hardware encoders first (NVENC for NVIDIA GPUs), fallback to software
    const char* encoderNames[] = {"h264_nvenc", "libx264"};
    const AVCodec* codec = nullptr;
    const char* selectedEncoder = nullptr;

    for (const char* encoderName : encoderNames)
    {
        codec = avcodec_find_encoder_by_name(encoderName);
        if (codec)
        {
            selectedEncoder = encoderName;
            common::logInfo("WebRTCTransport: Selected encoder: %s", encoderName);
            break;
        }
    }

    if (!codec)
    {
        common::logError("WebRTCTransport: No H.264 encoder found (tried h264_nvenc, libx264)");
        return false;
    }

    codecContext_ = avcodec_alloc_context3(codec);
    if (!codecContext_)
    {
        common::logError("WebRTCTransport: Failed to allocate codec context");
        return false;
    }

    codecContext_->width = width;
    codecContext_->height = height;
    codecContext_->time_base = {1, config_.webrtcFramerate};
    codecContext_->framerate = {config_.webrtcFramerate, 1};
    codecContext_->pix_fmt = AV_PIX_FMT_YUV420P;
    codecContext_->bit_rate = config_.webrtcBitrate;
    codecContext_->gop_size = config_.webrtcFramerate; // One keyframe per second
    codecContext_->max_b_frames = 0;                   // Disable B-frames for lower latency

    // Configure encoder-specific options
    bool isHardwareEncoder = (std::string(selectedEncoder) == "h264_nvenc");

    if (isHardwareEncoder)
    {
        // TODO: make it configurable
        // NVENC hardware encoder settings for low latency
        av_opt_set(codecContext_->priv_data, "preset", "p1", 0);     // Fastest preset (p1-p7)
        av_opt_set(codecContext_->priv_data, "tune", "ll", 0);       // Low-latency tuning
        av_opt_set(codecContext_->priv_data, "rc", "cbr", 0);        // Constant bitrate
        av_opt_set(codecContext_->priv_data, "delay", "0", 0);       // No frame delay
        av_opt_set(codecContext_->priv_data, "zerolatency", "1", 0); // Zero latency mode
    }
    else
    {
        // Software libx264 encoder settings
        av_opt_set(codecContext_->priv_data, "preset", config_.webrtcPreset.c_str(), 0);
        av_opt_set(codecContext_->priv_data, "tune", "zerolatency", 0);
    }

    // Common settings for both encoders
    av_opt_set(codecContext_->priv_data, "annex_b", "1", 0);        // Annex B format for RTP
    av_opt_set(codecContext_->priv_data, "repeat-headers", "1", 0); // Repeat SPS/PPS

    if (avcodec_open2(codecContext_, codec, nullptr) < 0)
    {
        common::logError("WebRTCTransport: Failed to open codec");
        avcodec_free_context(&codecContext_);
        return false;
    }

    avFrame_ = av_frame_alloc();
    if (!avFrame_)
    {
        common::logError("WebRTCTransport: Failed to allocate frame");
        avcodec_free_context(&codecContext_);
        return false;
    }

    avFrame_->format = codecContext_->pix_fmt;
    avFrame_->width = codecContext_->width;
    avFrame_->height = codecContext_->height;

    if (av_frame_get_buffer(avFrame_, 0) < 0)
    {
        common::logError("WebRTCTransport: Failed to allocate frame buffer");
        av_frame_free(&avFrame_);
        avcodec_free_context(&codecContext_);
        return false;
    }

    avPacket_ = av_packet_alloc();
    if (!avPacket_)
    {
        common::logError("WebRTCTransport: Failed to allocate packet");
        av_frame_free(&avFrame_);
        avcodec_free_context(&codecContext_);
        return false;
    }

    lastEncoderWidth_ = width;
    lastEncoderHeight_ = height;
    frameCounter_ = 0;
    currentEncoderBitrate_ = config_.webrtcBitrate;
    lastBitrateAdjustment_ = std::chrono::steady_clock::now();

    common::logInfo("WebRTCTransport: Encoder initialized successfully");
    return true;
}

void WebRTCTransport::cleanupEncoder()
{
    if (swsContext_)
    {
        sws_freeContext(swsContext_);
        swsContext_ = nullptr;
    }

    if (avPacket_)
    {
        av_packet_free(&avPacket_);
    }

    if (avFrame_)
    {
        av_frame_free(&avFrame_);
    }

    if (codecContext_)
    {
        avcodec_free_context(&codecContext_);
    }
}

bool WebRTCTransport::initializeDecoder()
{
    common::logInfo("WebRTCTransport: Initializing H.264 decoder");

    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!decoder)
    {
        common::logError("WebRTCTransport: H.264 decoder not found");
        return false;
    }

    decoderContext_ = avcodec_alloc_context3(decoder);
    if (!decoderContext_)
    {
        common::logError("WebRTCTransport: Failed to allocate decoder context");
        return false;
    }

    if (avcodec_open2(decoderContext_, decoder, nullptr) < 0)
    {
        common::logError("WebRTCTransport: Failed to open H.264 decoder");
        avcodec_free_context(&decoderContext_);
        return false;
    }

    decodedFrame_ = av_frame_alloc();
    if (!decodedFrame_)
    {
        common::logError("WebRTCTransport: Failed to allocate decoded frame");
        avcodec_free_context(&decoderContext_);
        return false;
    }

    decoderPacket_ = av_packet_alloc();
    if (!decoderPacket_)
    {
        common::logError("WebRTCTransport: Failed to allocate decoder packet");
        av_frame_free(&decodedFrame_);
        avcodec_free_context(&decoderContext_);
        return false;
    }

    common::logInfo("WebRTCTransport: H.264 decoder initialized");
    return true;
}

void WebRTCTransport::cleanupDecoder()
{
    if (decoderSwsContext_)
    {
        sws_freeContext(decoderSwsContext_);
        decoderSwsContext_ = nullptr;
    }

    if (decoderPacket_)
    {
        av_packet_free(&decoderPacket_);
    }

    if (decodedFrame_)
    {
        av_frame_free(&decodedFrame_);
    }

    if (decoderContext_)
    {
        avcodec_free_context(&decoderContext_);
    }
}

// Convert H.264 from AVCC format (length-prefixed) to Annex B format (start code prefixed)
// WebCodecs outputs AVCC, but FFmpeg expects Annex B
std::vector<uint8_t> WebRTCTransport::convertAVCCToAnnexB(const std::vector<std::byte>& avccData)
{
    std::vector<uint8_t> annexBData;
    annexBData.reserve(avccData.size() + 1024); // Reserve extra for start codes

    size_t pos = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(avccData.data());
    const size_t dataSize = avccData.size();

    // Check if this is an AVCDecoderConfigurationRecord (starts with configurationVersion = 1)
    // These are typically small (< 100 bytes) and have specific structure
    if (dataSize < 100 && dataSize > 7 && data[0] == 1)
    {
        common::logDebug("WebRTCTransport: Detected AVCDecoderConfigurationRecord (%zu bytes)", dataSize);

        // Parse AVCC decoder configuration record
        // configurationVersion = data[0]
        // AVCProfileIndication = data[1]
        // profile_compatibility = data[2]
        // AVCLevelIndication = data[3]
        // lengthSizeMinusOne = data[4] & 0x03
        uint8_t numSPS = data[5] & 0x1F; // Lower 5 bits
        pos = 6;

        // Extract SPS (Sequence Parameter Set)
        for (uint8_t i = 0; i < numSPS && pos + 2 <= dataSize; i++)
        {
            uint16_t spsLength = (data[pos] << 8) | data[pos + 1];
            pos += 2;

            if (pos + spsLength <= dataSize)
            {
                // Write Annex B start code
                annexBData.push_back(0x00);
                annexBData.push_back(0x00);
                annexBData.push_back(0x00);
                annexBData.push_back(0x01);

                // Copy SPS data
                annexBData.insert(annexBData.end(), data + pos, data + pos + spsLength);
                common::logDebug("WebRTCTransport: Extracted SPS (type 7), length %u", spsLength);

                pos += spsLength;
            }
        }

        // Extract PPS (Picture Parameter Set)
        if (pos + 1 <= dataSize)
        {
            uint8_t numPPS = data[pos];
            pos++;

            for (uint8_t i = 0; i < numPPS && pos + 2 <= dataSize; i++)
            {
                uint16_t ppsLength = (data[pos] << 8) | data[pos + 1];
                pos += 2;

                if (pos + ppsLength <= dataSize)
                {
                    // Write Annex B start code
                    annexBData.push_back(0x00);
                    annexBData.push_back(0x00);
                    annexBData.push_back(0x00);
                    annexBData.push_back(0x01);

                    // Copy PPS data
                    annexBData.insert(annexBData.end(), data + pos, data + pos + ppsLength);
                    common::logDebug("WebRTCTransport: Extracted PPS (type 8), length %u", ppsLength);

                    pos += ppsLength;
                }
            }
        }

        return annexBData;
    }

    // Regular AVCC frame data with 4-byte length prefixes
    while (pos + 4 <= dataSize)
    {
        // Read 4-byte length prefix (big-endian)
        uint32_t nalLength = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16)
                             | (static_cast<uint32_t>(data[pos + 2]) << 8) | static_cast<uint32_t>(data[pos + 3]);

        pos += 4;

        // Sanity check
        if (pos + nalLength > dataSize)
        {
            common::logError("WebRTCTransport: Invalid NAL unit length %u at pos %zu (remaining: %zu)", nalLength,
                             pos - 4, dataSize - pos);
            break;
        }

        // Write Annex B start code: 0x00 0x00 0x00 0x01
        annexBData.push_back(0x00);
        annexBData.push_back(0x00);
        annexBData.push_back(0x00);
        annexBData.push_back(0x01);

        // Log NAL unit type for debugging (first byte & 0x1F)
        if (nalLength > 0)
        {
            uint8_t nalType = data[pos] & 0x1F;
            const char* nalTypeName = "Unknown";
            switch (nalType)
            {
                case 1:
                    nalTypeName = "Non-IDR Slice";
                    break;
                case 5:
                    nalTypeName = "IDR Slice";
                    break;
                case 6:
                    nalTypeName = "SEI";
                    break;
                case 7:
                    nalTypeName = "SPS";
                    break;
                case 8:
                    nalTypeName = "PPS";
                    break;
                case 9:
                    nalTypeName = "Access Unit Delimiter";
                    break;
            }
            common::logDebugVerbose("WebRTCTransport: NAL unit type %u (%s), length %u", nalType, nalTypeName,
                                    nalLength);
        }

        // Copy NAL unit data
        annexBData.insert(annexBData.end(), data + pos, data + pos + nalLength);

        pos += nalLength;
    }

    return annexBData;
}

std::unique_ptr<Image> WebRTCTransport::decodeH264Frame(const std::vector<std::byte>& h264Data)
{
    std::lock_guard<std::mutex> lock(decoderMutex_);

    common::logDebug("WebRTCTransport: decodeH264Frame called with %zu bytes", h264Data.size());

    if (!decoderContext_ && !initializeDecoder())
    {
        common::logDebug("WebRTCTransport: Failed to initialize decoder");
        return nullptr;
    }

    // Convert AVCC format (from WebCodecs) to Annex B format (for FFmpeg)
    std::vector<uint8_t> annexBData = convertAVCCToAnnexB(h264Data);

    if (annexBData.empty())
    {
        common::logError("WebRTCTransport: Failed to convert AVCC to Annex B");
        return nullptr;
    }

    common::logDebugVerbose("WebRTCTransport: Converted %zu bytes AVCC to %zu bytes Annex B", h264Data.size(),
                            annexBData.size());

    // Prepare packet with Annex B H.264 data
    decoderPacket_->data = annexBData.data();
    decoderPacket_->size = annexBData.size();

    common::logDebugVerbose("WebRTCTransport: Sending packet to decoder (size: %d)", decoderPacket_->size);

    // Send packet to decoder
    int ret = avcodec_send_packet(decoderContext_, decoderPacket_);
    if (ret < 0)
    {
        // Ignore errors on configuration packets (SPS/PPS) - they're metadata, not frames
        // AVERROR_INVALIDDATA is common on first packet from WebCodecs
        if (annexBData.size() < 100)
        {
            common::logDebug("WebRTCTransport: Decoder rejected small packet (likely config data), ignoring");
            return nullptr;
        }

        common::logError("WebRTCTransport: Error sending packet to decoder (ret: %d)", ret);
        return nullptr;
    }

    common::logDebug("WebRTCTransport: Packet sent, receiving frame...");

    // Receive decoded frame
    ret = avcodec_receive_frame(decoderContext_, decodedFrame_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        common::logDebug("WebRTCTransport: Decoder needs more packets (ret: %d)", ret);
        return nullptr; // Need more packets
    }
    else if (ret < 0)
    {
        common::logError("WebRTCTransport: Error receiving frame from decoder (ret: %d)", ret);
        return nullptr;
    }

    common::logDebugVerbose("WebRTCTransport: Frame received from decoder: %dx%d", decodedFrame_->width,
                            decodedFrame_->height);

    // Convert YUV420P to RGB for processing
    // Always recreate swscaler if frame dimensions changed
    static int lastDecodedWidth = 0;
    static int lastDecodedHeight = 0;

    if (!decoderSwsContext_ || decodedFrame_->width != lastDecodedWidth || decodedFrame_->height != lastDecodedHeight)
    {
        if (decoderSwsContext_)
        {
            common::logDebug("WebRTCTransport: Recreating decoder SWS context for new resolution %dx%d",
                             decodedFrame_->width, decodedFrame_->height);
            sws_freeContext(decoderSwsContext_);
            decoderSwsContext_ = nullptr;
        }

        decoderSwsContext_ =
            sws_getContext(decodedFrame_->width, decodedFrame_->height, AV_PIX_FMT_YUV420P, decodedFrame_->width,
                           decodedFrame_->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (!decoderSwsContext_)
        {
            common::logError("WebRTCTransport: Failed to create decoder SWS context");
            return nullptr;
        }

        lastDecodedWidth = decodedFrame_->width;
        lastDecodedHeight = decodedFrame_->height;
        common::logInfo("WebRTCTransport: Created decoder SWS context for %dx%d", lastDecodedWidth, lastDecodedHeight);
    }

    // Create a NEW Image for this decoded frame (thread-safe, no shared state)
    const size_t rgbSize = decodedFrame_->width * decodedFrame_->height * 3;
    auto decodedImage = std::make_unique<Image>(rgbSize);

    // Decode directly into Image buffer
    uint8_t* dstData[1] = {decodedImage->data()};
    int dstLinesize[1] = {decodedFrame_->width * 3};

    // Convert YUV420P to RGB directly into Image
    sws_scale(decoderSwsContext_, decodedFrame_->data, decodedFrame_->linesize, 0, decodedFrame_->height, dstData,
              dstLinesize);

    // Set Image metadata
    decodedImage->info.width = decodedFrame_->width;
    decodedImage->info.height = decodedFrame_->height;
    decodedImage->info.pixelSizeBytes = 3;
    decodedImage->info.TJPixelFormat = TJPF_RGB;
    decodedImage->info.format = ImageFormat::RAW;

    common::logDebugVerbose("WebRTCTransport: Decoded H.264 frame to RGB: %dx%d", decodedFrame_->width,
                            decodedFrame_->height);

    av_packet_unref(decoderPacket_);

    return decodedImage;
}

bool WebRTCTransport::encodeFrame(const std::unique_ptr<Image>& frame, std::vector<uint8_t>& encodedData)
{
    if (!codecContext_ || !avFrame_ || !avPacket_)
    {
        return false;
    }

    // Convert RGB/RGBA to YUV420P
    AVPixelFormat srcFormat = (frame->info.pixelSizeBytes == 4) ? AV_PIX_FMT_RGBA : AV_PIX_FMT_RGB24;

    if (!swsContext_ || lastEncoderWidth_ != frame->info.width || lastEncoderHeight_ != frame->info.height)
    {
        if (swsContext_)
        {
            sws_freeContext(swsContext_);
        }

        swsContext_ =
            sws_getContext(frame->info.width, frame->info.height, srcFormat, codecContext_->width,
                           codecContext_->height, codecContext_->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (!swsContext_)
        {
            common::logError("WebRTCTransport: Failed to create SWS context");
            return false;
        }
    }

    // Prepare source data
    const uint8_t* srcData[1] = {frame->data()};
    int srcLinesize[1] = {static_cast<int>(frame->info.width * frame->info.pixelSizeBytes)};

    // Convert to YUV420P
    sws_scale(swsContext_, srcData, srcLinesize, 0, frame->info.height, avFrame_->data, avFrame_->linesize);

    avFrame_->pts = frameCounter_++;

    // Send frame to encoder
    int ret = avcodec_send_frame(codecContext_, avFrame_);
    if (ret < 0)
    {
        common::logError("WebRTCTransport: Error sending frame to encoder");
        return false;
    }

    // Receive encoded packet
    ret = avcodec_receive_packet(codecContext_, avPacket_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        return false; // Need more frames
    }
    else if (ret < 0)
    {
        common::logError("WebRTCTransport: Error receiving packet from encoder");
        return false;
    }

    // Copy encoded data
    encodedData.assign(avPacket_->data, avPacket_->data + avPacket_->size);

    av_packet_unref(avPacket_);

    return true;
}

void WebRTCTransport::broadcastEncodedFrame(const std::vector<uint8_t>& data, bool isKeyFrame)
{
    std::lock_guard<std::mutex> lock(peersMutex_);

    // Track server send timing
    auto sendTime = std::chrono::system_clock::now();
    auto sendMs = std::chrono::duration_cast<std::chrono::milliseconds>(sendTime.time_since_epoch()).count();

    for (auto& [peerId, peer] : peers_)
    {
        // Only send frames to peers that are actively sending camera data
        if (peer.track && peer.pc && peer.pc->state() == rtc::PeerConnection::State::Connected && peer.isSendingFrames)
        {
            try
            {
                // Send RTP packet - convert uint8_t* to std::byte*
                peer.track->send(reinterpret_cast<const std::byte*>(data.data()), data.size());

                {
                    std::lock_guard<std::mutex> statsLock(statsMutex_);
                    stats_.framesSent++;

                    // Log server send timing every 30 frames
                    if (stats_.framesSent % 30 == 0)
                    {
                        common::logInfo(
                            "WebRTCTransport: [SERVER_SEND] Frame %zu sent at t=%lld ms, size=%zu bytes, keyframe=%d",
                            stats_.framesSent, sendMs, data.size(), isKeyFrame);
                    }
                }

                peer.lastFrameTime = std::chrono::steady_clock::now();

                // Track bandwidth metrics for adaptive bitrate
                peer.bytesSent += data.size();
                peer.framesSent++;
            }
            catch (const std::exception& e)
            {
                common::logError("WebRTCTransport: Failed to send frame to peer %s: %s", peerId.c_str(), e.what());
            }
        }
    }
}

std::string WebRTCTransport::processOfferAndCreateAnswer(const std::string& peerId, const std::string& offerSdp)
{
    // Remove existing connection if any
    {
        std::lock_guard<std::mutex> lock(peersMutex_);
        auto it = peers_.find(peerId);
        if (it != peers_.end())
        {
            common::logInfo("WebRTCTransport: Removing existing connection for peer %s", peerId.c_str());
            if (it->second.pc)
            {
                it->second.pc->close();
            }
            peers_.erase(it);
        }
    }

    common::logInfo("WebRTCTransport: Processing offer from peer %s and creating answer", peerId.c_str());

    PeerConnection peerConn;
    peerConn.peerId = peerId;

    // Configure peer connection
    rtc::Configuration rtcConfig;

    // Add STUN servers
    for (const auto& stunServer : config_.stunServers)
    {
        rtcConfig.iceServers.emplace_back(stunServer);
    }

    peerConn.pc = std::make_shared<rtc::PeerConnection>(rtcConfig);

    // Set up callbacks
    peerConn.pc->onStateChange(
        [peerId](rtc::PeerConnection::State state)
        { common::logInfo("WebRTCTransport: Peer %s state changed to %d", peerId.c_str(), static_cast<int>(state)); });

    peerConn.pc->onGatheringStateChange(
        [peerId](rtc::PeerConnection::GatheringState state)
        {
            common::logInfo("WebRTCTransport: Peer %s gathering state changed to %d", peerId.c_str(),
                            static_cast<int>(state));
        });

    // The browser's offer includes a "recvonly" video track (mid: 0).
    // After setRemoteDescription, we'll get this track via onTrack callback.
    // We'll use that track for sending frames back to the browser.
    std::shared_ptr<rtc::Track> negotiatedTrack = nullptr;

    peerConn.pc->onTrack(
        [&negotiatedTrack, peerId](std::shared_ptr<rtc::Track> track)
        {
            common::logInfo("WebRTCTransport: Track callback for peer %s, mid: %s - will use for sending",
                            peerId.c_str(), track->mid().c_str());
            negotiatedTrack = track;
        });

    // Handle incoming data channel from browser
    // This callback may fire asynchronously, so we need to be careful with peer lookup
    peerConn.pc->onDataChannel(
        [this, peerId](std::shared_ptr<rtc::DataChannel> dc)
        {
            common::logInfo("WebRTCTransport: Received data channel '%s' from peer %s", dc->label().c_str(),
                            peerId.c_str());

            // Set up callbacks immediately
            dc->onOpen([peerId]()
                       { common::logInfo("WebRTCTransport: Data channel opened for peer %s", peerId.c_str()); });

            dc->onClosed([peerId]()
                         { common::logInfo("WebRTCTransport: Data channel closed for peer %s", peerId.c_str()); });

            dc->onMessage(
                [this, peerId](auto data)
                {
                    if (std::holds_alternative<rtc::binary>(data))
                    {
                        const auto& binaryData = std::get<rtc::binary>(data);

                        // Track server receive timing
                        auto recvTime = std::chrono::system_clock::now();
                        auto recvMs =
                            std::chrono::duration_cast<std::chrono::milliseconds>(recvTime.time_since_epoch()).count();

                        common::logDebugVerbose(
                            "WebRTCTransport: [SERVER_RECV] Received H.264 data at t=%lld ms: %zu bytes from %s",
                            recvMs, binaryData.size(), peerId.c_str());

                        // Mark peer as actively sending frames (bidirectional communication)
                        {
                            std::lock_guard<std::mutex> lock(peersMutex_);
                            auto it = peers_.find(peerId);
                            if (it != peers_.end() && !it->second.isSendingFrames)
                            {
                                it->second.isSendingFrames = true;
                                common::logInfo("WebRTCTransport: Peer %s is now sending camera frames - enabling "
                                                "bidirectional streaming",
                                                peerId.c_str());
                            }
                        }

                        std::vector<std::byte> h264Data;
                        h264Data.reserve(binaryData.size());
                        for (const auto& byte : binaryData)
                        {
                            h264Data.push_back(byte);
                        }

                        processDecodedH264(h264Data);
                    }
                });

            // Store the data channel - need to find peer safely
            std::lock_guard<std::mutex> lock(peersMutex_);
            auto it = peers_.find(peerId);
            if (it != peers_.end())
            {
                it->second.dataChannel = dc;
                common::logInfo("WebRTCTransport: Data channel stored for peer %s", peerId.c_str());
            }
            else
            {
                common::logWarn("WebRTCTransport: Peer %s not found when storing DataChannel (may not be created yet)",
                                peerId.c_str());
            }
        });

    // Set up local description callback BEFORE any description operations
    std::string localSdp;
    std::promise<void> answerPromise;
    auto answerFuture = answerPromise.get_future();

    peerConn.pc->onLocalDescription(
        [&localSdp, &answerPromise, peerId](rtc::Description desc)
        {
            localSdp = std::string(desc);
            common::logInfo("WebRTCTransport: Local answer generated for peer %s, length: %zu", peerId.c_str(),
                            localSdp.length());
            common::logDebug("WebRTCTransport: Answer SDP:\n%s", localSdp.c_str());
            answerPromise.set_value();
        });

    // Set remote description (offer from browser)
    // This will automatically trigger answer generation via onLocalDescription callback
    try
    {
        common::logDebug("WebRTCTransport: Setting remote description (offer from browser)");
        common::logDebug("WebRTCTransport: Offer SDP:\n%s", offerSdp.c_str());
        rtc::Description offer(offerSdp, "offer");
        peerConn.pc->setRemoteDescription(offer);
    }
    catch (const std::exception& e)
    {
        common::logError("WebRTCTransport: Failed to set remote description: %s", e.what());
        return "";
    }

    // Wait for local description with timeout (answer is auto-generated by libdatachannel)
    common::logDebug("WebRTCTransport: Waiting for automatic answer generation for peer %s", peerId.c_str());
    if (answerFuture.wait_for(std::chrono::seconds(5)) == std::future_status::timeout)
    {
        common::logError("WebRTCTransport: Timeout waiting for local answer");
        return "";
    }

    // Use the negotiated track (browser's recvonly track) for sending frames
    // The track direction is negotiated as sendrecv or sendonly from server perspective
    if (negotiatedTrack)
    {
        peerConn.track = negotiatedTrack;
        common::logInfo("WebRTCTransport: Using negotiated track (mid: %s) for sending frames to browser",
                        negotiatedTrack->mid().c_str());
    }
    else
    {
        common::logError("WebRTCTransport: No track negotiated for peer %s - cannot send frames", peerId.c_str());
    }

    // Store peer connection
    {
        std::lock_guard<std::mutex> lock(peersMutex_);
        peers_[peerId] = std::move(peerConn);
    }

    common::logInfo("WebRTCTransport: Created answer for peer %s", peerId.c_str());
    return localSdp;
}

bool WebRTCTransport::processIceCandidate(const std::string& peerId, const std::string& candidate,
                                          const std::string& mid)
{
    std::lock_guard<std::mutex> lock(peersMutex_);

    auto it = peers_.find(peerId);
    if (it == peers_.end())
    {
        common::logError("WebRTCTransport: Peer %s not found for ICE candidate", peerId.c_str());
        return false;
    }

    try
    {
        rtc::Candidate rtcCandidate(candidate, mid);
        it->second.pc->addRemoteCandidate(rtcCandidate);
        return true;
    }
    catch (const std::exception& e)
    {
        common::logError("WebRTCTransport: Failed to add ICE candidate for peer %s: %s", peerId.c_str(), e.what());
        return false;
    }
}

void WebRTCTransport::removePeerConnection(const std::string& peerId)
{
    std::lock_guard<std::mutex> lock(peersMutex_);

    auto it = peers_.find(peerId);
    if (it != peers_.end())
    {
        common::logInfo("WebRTCTransport: Removing peer connection for %s", peerId.c_str());
        if (it->second.pc)
        {
            it->second.pc->close();
        }
        peers_.erase(it);
    }
}

void WebRTCTransport::setInputDevice(std::shared_ptr<wsInputDevice> device)
{
    std::lock_guard<std::mutex> lock(inputDeviceMutex_);
    inputDevice_ = device;
    common::logInfo("WebRTCTransport: Input device %s", device ? "set" : "cleared");
}

void WebRTCTransport::processDecodedH264(const std::vector<std::byte>& h264Data)
{
    common::logDebug("WebRTCTransport: processDecodedH264 called with %zu bytes", h264Data.size());

    // Decode H.264 frame to RGB (returns Image directly, thread-safe)
    auto decodedImage = decodeH264Frame(h264Data);

    if (decodedImage)
    {
        common::logDebug("WebRTCTransport: H.264 frame decoded successfully");

        std::lock_guard<std::mutex> lock(inputDeviceMutex_);
        if (inputDevice_)
        {
            inputDevice_->pushRGBFrame(std::move(*decodedImage));
        }
        else
        {
            common::logDebug("WebRTCTransport: Decoded H.264 frame ready but no input device set");
        }
    }
    else
    {
        common::logDebug("WebRTCTransport: Failed to decode H.264 frame or needs more data");
    }
}

void WebRTCTransport::adjustBitrateIfNeeded()
{
    // Skip if adaptive bitrate is disabled in config
    if (!config_.enableAdaptiveBitrate)
    {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastBitrateAdjustment_).count();

    // Check bitrate every 5 seconds to avoid thrashing and get better average
    if (elapsed < 5)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(peersMutex_);

    for (auto& [peerId, peer] : peers_)
    {
        if (!peer.isSendingFrames)
        {
            continue; // Skip peers not actively streaming
        }

        // Initialize measurement window on first check
        if (peer.lastBitrateCheck.time_since_epoch().count() == 0)
        {
            peer.lastBitrateCheck = now;
            peer.bytesSent = 0;
            peer.framesSent = 0;
            common::logInfo("WebRTCTransport: Initialized bitrate measurement for peer %s", peerId.c_str());
            continue;
        }

        // Calculate bandwidth measurement window
        auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(now - peer.lastBitrateCheck).count();

        if (timeDiff < 3000)
        {
            continue; // Need at least 3 seconds of data for stable measurement
        }

        // Calculate actual bitrate: (bytes * 8 bits/byte * 1000 ms/s) / milliseconds
        int actualBitrate = (peer.bytesSent * 8 * 1000) / timeDiff;
        int targetBitrate = currentEncoderBitrate_;

        // VBR encoding means actual bitrate can be much lower than target (good compression)
        // Only adjust if we see sustained issues
        common::logInfo("WebRTCTransport: Bitrate check for peer %s: actual=%.2f Mbps, target=%.1f Mbps, bytes=%zu, "
                        "frames=%zu, duration=%.1fs",
                        peerId.c_str(), actualBitrate / 1000000.0, targetBitrate / 1000000.0, peer.bytesSent,
                        peer.framesSent, timeDiff / 1000.0);

        // Skip adjustment if we have insufficient data (< 30 frames in measurement window)
        if (peer.framesSent < 30)
        {
            common::logInfo("WebRTCTransport: Insufficient frames (%zu) for bitrate adjustment, skipping",
                            peer.framesSent);
            peer.bytesSent = 0;
            peer.framesSent = 0;
            peer.lastBitrateCheck = now;
            continue;
        }

        // Only reduce bitrate if actual throughput is SIGNIFICANTLY lower (< 50% of target)
        // This indicates real network congestion, not just good compression
        if (actualBitrate < targetBitrate * 0.5)
        {
            // Sustained network congestion detected: reduce bitrate
            int newBitrate = std::max(MIN_BITRATE, currentEncoderBitrate_ - BITRATE_STEP);

            common::logWarn("WebRTCTransport: Network congestion detected for peer %s "
                            "(actual: %.2f Mbps < target: %.1f Mbps * 0.5), reducing bitrate",
                            peerId.c_str(), actualBitrate / 1000000.0, targetBitrate / 1000000.0);

            updateEncoderBitrate(newBitrate);
        }
        else if (actualBitrate > targetBitrate * 0.95 && currentEncoderBitrate_ < MAX_BITRATE)
        {
            // Network capacity available: increase bitrate gradually
            int newBitrate = std::min(MAX_BITRATE, currentEncoderBitrate_ + BITRATE_STEP);

            common::logInfo("WebRTCTransport: Network capacity available for peer %s "
                            "(actual: %.2f Mbps >= target: %.1f Mbps * 0.95), increasing bitrate",
                            peerId.c_str(), actualBitrate / 1000000.0, targetBitrate / 1000000.0);

            updateEncoderBitrate(newBitrate);
        }

        // Reset measurement counters for next interval
        peer.bytesSent = 0;
        peer.framesSent = 0;
        peer.lastBitrateCheck = now;
    }

    lastBitrateAdjustment_ = now;
}

void WebRTCTransport::updateEncoderBitrate(int newBitrate)
{
    if (!codecContext_)
    {
        common::logError("WebRTCTransport: Cannot update bitrate, encoder not initialized");
        return;
    }

    if (newBitrate == currentEncoderBitrate_)
    {
        return; // No change needed
    }

    // Update FFmpeg encoder bitrate dynamically
    // For libx264, we need to update both bit_rate and rc_max_rate
    codecContext_->bit_rate = newBitrate;
    codecContext_->rc_max_rate = newBitrate;
    codecContext_->rc_buffer_size = newBitrate; // 1 second buffer

    // Also try to update via x264 params (may not take effect until next keyframe)
    char bitrateKbps[32];
    snprintf(bitrateKbps, sizeof(bitrateKbps), "%d", newBitrate / 1000); // x264 expects kbps
    av_opt_set(codecContext_->priv_data, "crf", "23", 0);                // Use CRF mode for better quality
    av_opt_set(codecContext_->priv_data, "maxrate", bitrateKbps, 0);
    av_opt_set(codecContext_->priv_data, "bufsize", bitrateKbps, 0);

    currentEncoderBitrate_ = newBitrate;

    common::logInfo("WebRTCTransport: Adjusted encoder bitrate to %d bps (%.1f Mbps)", newBitrate,
                    newBitrate / 1000000.0);
}

} // namespace web
} // namespace linuxface
