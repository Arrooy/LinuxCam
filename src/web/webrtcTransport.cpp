#include "LinuxFace/web/webrtcTransport.h"

#include <chrono>
#include <cstdarg>
#include <cstring>
#include <rtc/h264rtppacketizer.hpp>
#include <rtc/rtppacketizationconfig.hpp>
#include <sstream>

#include "LinuxFace/common.h"
#include "LinuxFace/web/wsInputDevice.h"

extern "C"
{
#include <libavutil/log.h>
}


namespace linuxface::web
{

namespace
{
// Custom FFmpeg log callback to filter out harmless H.264 decoder warnings/errors
// Safari's WebCodecs encoder produces valid H.264 but with frame_num resets that
// FFmpeg's decoder complains about. These are informational - decoding still works.
void ffmpegLogCallback(void* ptr, int level, const char* fmt, va_list vl)
{
    // Only process warnings and errors (skip info/debug/trace)
    if (level > AV_LOG_WARNING)
    {
        return;
    }

    char message[1024];
    vsnprintf(message, sizeof(message), fmt, vl);

    // Filter known harmless H.264 decoder messages that don't prevent decoding:
    // - "no frame" - SPS/PPS config packets without frame data (expected)
    // - "Frame num change" - Safari encoder resets frame_num at GOP boundaries
    // - "reference picture missing during reorder" - consequence of frame_num reset
    // - "Missing reference picture" - alternative wording for reference issues
    // - "decode_slice_header error" - transient error during reference reset
    // - "number of reference frames" - decoder warning about reference management
    // - "mmco: unref short failure" - memory management warning, non-fatal
    // - "non-existing PPS" - can occur transiently during stream start
    if (std::strstr(message, "no frame") != nullptr ||
        std::strstr(message, "Frame num change") != nullptr ||
        std::strstr(message, "reference picture missing") != nullptr ||
        std::strstr(message, "Missing reference picture") != nullptr ||
        std::strstr(message, "decode_slice_header error") != nullptr ||
        std::strstr(message, "number of reference frames") != nullptr ||
        std::strstr(message, "non-existing PPS") != nullptr ||
        std::strstr(message, "mmco:") != nullptr)
    {
        // Log these at debug level instead of flooding stderr
        if (level <= AV_LOG_ERROR)
        {
            common::logDebug("FFmpeg H.264 (filtered): %s", message);
        }
        return;
    }

    // Forward other important messages to default handler
    av_log_default_callback(ptr, level, fmt, vl);
}
} // namespace

// Simple base64 decoder (returns empty vector on invalid input)
static std::vector<uint8_t> base64Decode(const std::string& input)
{
    static const int8_t table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        // rest initialized to -1
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    std::vector<uint8_t> out;
    out.reserve((input.size() * 3) / 4 + 4);

    int val = 0, valb = -8;
    for (unsigned char c : input)
    {
        if (table[c] == -1) break;
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0)
        {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// Extract sprop-parameter-sets for a given payload type from an SDP blob and return an Annex-B
// formatted buffer containing start-code prefixed SPS and PPS if found. Returns empty vector if not found.
static std::vector<uint8_t> extractSpropAnnexBFromSdp(const std::string& sdp, int payloadType)
{
    std::string needle = "a=fmtp:" + std::to_string(payloadType) + " ";
    size_t pos = sdp.find(needle);
    if (pos == std::string::npos)
    {
        return {};
    }

    // Extract the line
    size_t lineEnd = sdp.find('\n', pos);
    std::string line = sdp.substr(pos, (lineEnd == std::string::npos) ? std::string::npos : (lineEnd - pos));

    // Look for sprop-parameter-sets=... value
    size_t spropPos = line.find("sprop-parameter-sets=");
    if (spropPos == std::string::npos)
    {
        return {};
    }

    spropPos += strlen("sprop-parameter-sets=");
    size_t endPos = line.find(';', spropPos);
    std::string spropValue = line.substr(spropPos, (endPos == std::string::npos) ? std::string::npos : (endPos - spropPos));

    // spropValue contains base64 NAL units separated by commas (SPS,PPS[,VPS])
    std::vector<uint8_t> annexB;
    size_t start = 0;
    while (start < spropValue.size())
    {
        size_t comma = spropValue.find(',', start);
        std::string token = spropValue.substr(start, (comma == std::string::npos) ? std::string::npos : (comma - start));
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t\r\n"));
        token.erase(token.find_last_not_of(" \t\r\n") + 1);

        if (!token.empty())
        {
            auto decoded = base64Decode(token);
            if (!decoded.empty())
            {
                // prepend 4-byte start code
                annexB.push_back(0x00);
                annexB.push_back(0x00);
                annexB.push_back(0x00);
                annexB.push_back(0x01);
                annexB.insert(annexB.end(), decoded.begin(), decoded.end());
            }
        }

        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    return annexB;
}

// Extract sprop-parameter-sets as raw NAL unit byte vectors (no start codes)
static std::vector<std::vector<uint8_t>> extractSpropNalUnitsFromSdp(const std::string& sdp, int payloadType)
{
    std::vector<std::vector<uint8_t>> result;
    std::string needle = "a=fmtp:" + std::to_string(payloadType) + " ";
    size_t pos = sdp.find(needle);
    if (pos == std::string::npos)
    {
        return result;
    }

    size_t lineEnd = sdp.find('\n', pos);
    std::string line = sdp.substr(pos, (lineEnd == std::string::npos) ? std::string::npos : (lineEnd - pos));

    size_t spropPos = line.find("sprop-parameter-sets=");
    if (spropPos == std::string::npos)
    {
        return result;
    }

    spropPos += strlen("sprop-parameter-sets=");
    size_t endPos = line.find(';', spropPos);
    std::string spropValue = line.substr(spropPos, (endPos == std::string::npos) ? std::string::npos : (endPos - spropPos));

    size_t start = 0;
    while (start < spropValue.size())
    {
        size_t comma = spropValue.find(',', start);
        std::string token = spropValue.substr(start, (comma == std::string::npos) ? std::string::npos : (comma - start));
        token.erase(0, token.find_first_not_of(" \t\r\n"));
        token.erase(token.find_last_not_of(" \t\r\n") + 1);

        if (!token.empty())
        {
            auto decoded = base64Decode(token);
            if (!decoded.empty())
            {
                result.emplace_back(std::move(decoded));
            }
        }

        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    return result;
}

// Build an AVCDecoderConfigurationRecord (AVCC) from SPS/PPS NAL units (raw NALs without start codes)
// Returns true on success and fills outAvcc.
static bool buildAvccFromNalUnits(const std::vector<std::vector<uint8_t>>& nals, std::vector<uint8_t>& outAvcc)
{
    // Need at least one SPS and one PPS
    if (nals.empty()) return false;

    // Identify SPS (nal type 7) and PPS (nal type 8)
    std::vector<std::vector<uint8_t>> spsList;
    std::vector<std::vector<uint8_t>> ppsList;

    for (const auto& nal : nals)
    {
        if (nal.empty()) continue;
        uint8_t nalType = nal[0] & 0x1F;
        if (nalType == 7)
            spsList.push_back(nal);
        else if (nalType == 8)
            ppsList.push_back(nal);
    }

    if (spsList.empty() || ppsList.empty())
    {
        return false;
    }

    // Use first SPS to extract profile/compat/level
    const auto& firstSps = spsList[0];
    if (firstSps.size() < 4) return false;

    uint8_t profile_idc = firstSps[1];
    uint8_t profile_compat = firstSps[2];
    uint8_t level_idc = firstSps[3];

    outAvcc.clear();
    outAvcc.push_back(0x01);                 // configurationVersion
    outAvcc.push_back(profile_idc);          // AVCProfileIndication
    outAvcc.push_back(profile_compat);       // profile_compatibility
    outAvcc.push_back(level_idc);            // AVCLevelIndication
    outAvcc.push_back(0xFF);                 // lengthSizeMinusOne (6 bits reserved 111111 + 2 bits lengthSizeMinusOne=3)

    // numOfSequenceParameterSets (3 bits reserved '111' + 5 bits count)
    uint8_t numSps = static_cast<uint8_t>(std::min<size_t>(spsList.size(), 31));
    outAvcc.push_back(0xE0 | (numSps & 0x1F));

    // Add SPS (length + data)
    for (const auto& sps : spsList)
    {
        uint16_t len = static_cast<uint16_t>(sps.size());
        outAvcc.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        outAvcc.push_back(static_cast<uint8_t>(len & 0xFF));
        outAvcc.insert(outAvcc.end(), sps.begin(), sps.end());
    }

    // PPS
    uint8_t numPps = static_cast<uint8_t>(std::min<size_t>(ppsList.size(), 255));
    outAvcc.push_back(numPps);
    for (const auto& pps : ppsList)
    {
        uint16_t len = static_cast<uint16_t>(pps.size());
        outAvcc.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        outAvcc.push_back(static_cast<uint8_t>(len & 0xFF));
        outAvcc.insert(outAvcc.end(), pps.begin(), pps.end());
    }

    return true;
}

// Small hex-dump helper for diagnostics
static std::string hexDump(const uint8_t* data, size_t len)
{
    std::ostringstream ss;
    ss << std::hex;
    size_t to = std::min<size_t>(len, 64);
    for (size_t i = 0; i < to; ++i)
    {
        ss << (int)data[i];
        if (i + 1 < to) ss << ",";
    }
    return ss.str();
}

// Extract SPS and PPS NAL units from Annex-B data and build AVCC (AVCDecoderConfigurationRecord)
// Returns true if successful and fills outAvcc
static bool extractAvccFromAnnexB(const std::vector<uint8_t>& annexB, std::vector<uint8_t>& outAvcc)
{
    std::vector<std::vector<uint8_t>> nalUnits;
    
    // Parse Annex-B to extract individual NAL units
    size_t pos = 0;
    const uint8_t* b = annexB.data();
    const size_t sz = annexB.size();
    
    while (pos < sz)
    {
        // Find start code
        size_t scLen = 0;
        if (pos + 4 <= sz && b[pos] == 0x00 && b[pos + 1] == 0x00 && b[pos + 2] == 0x00 && b[pos + 3] == 0x01)
        {
            scLen = 4;
        }
        else if (pos + 3 <= sz && b[pos] == 0x00 && b[pos + 1] == 0x00 && b[pos + 2] == 0x01)
        {
            scLen = 3;
        }
        else
        {
            pos++;
            continue;
        }
        
        size_t nalStart = pos + scLen;
        if (nalStart >= sz) break;
        
        // Find end of NAL (next start code or end of data)
        size_t nalEnd = nalStart + 1;
        while (nalEnd + 2 < sz)
        {
            if (b[nalEnd] == 0x00 && b[nalEnd + 1] == 0x00 && 
                (b[nalEnd + 2] == 0x01 || (nalEnd + 3 < sz && b[nalEnd + 2] == 0x00 && b[nalEnd + 3] == 0x01)))
            {
                break;
            }
            nalEnd++;
        }
        if (nalEnd + 2 >= sz) nalEnd = sz;
        
        // Extract NAL unit (without start code)
        if (nalEnd > nalStart)
        {
            std::vector<uint8_t> nal(b + nalStart, b + nalEnd);
            nalUnits.push_back(std::move(nal));
        }
        
        pos = nalEnd;
    }
    
    // Use existing helper to build AVCC from NAL units
    return buildAvccFromNalUnits(nalUnits, outAvcc);
}

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
    common::logInfo("WebRTCTransport: Worker thread started with low-latency optimizations");

    while (running_)
    {
        std::unique_ptr<Image> frame;

        {
            std::unique_lock<std::mutex> lock(frameMutex_);
            // Use shorter timeout (5ms) for responsive frame processing
            // Balance between latency and mobile Safari timeout handling
            frameCV_.wait_for(lock, std::chrono::milliseconds(5),
                              [this]() { return latestFrame_ != nullptr || !running_; });

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

bool WebRTCTransport::initializeDecoder(const std::vector<uint8_t>* avcc)
{
    common::logInfo("WebRTCTransport: Initializing H.264 decoder with low-latency settings");

    // If decoder already exists, nothing to do. Note: extradata must be set before opening codec.
    if (decoderContext_)
    {
        common::logDebug("WebRTCTransport: Decoder context already exists; skipping init");
        return true;
    }

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

    // If caller provided an AVCC (AVCDecoderConfigurationRecord), attach it as extradata
    if (avcc && !avcc->empty())
    {
        // Allocate extradata with required padding
        uint8_t* ed = static_cast<uint8_t*>(av_malloc(avcc->size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (ed)
        {
            memcpy(ed, avcc->data(), avcc->size());
            // zero padding
            memset(ed + avcc->size(), 0, AV_INPUT_BUFFER_PADDING_SIZE);
            decoderContext_->extradata = ed;
            decoderContext_->extradata_size = static_cast<int>(avcc->size());
            common::logInfo("WebRTCTransport: Set decoder extradata (AVCC) with %zu bytes", avcc->size());
        }
        else
        {
            common::logWarn("WebRTCTransport: Failed to allocate extradata for AVCC");
        }
    }

    // Configure decoder for low latency
    decoderContext_->flags |= AV_CODEC_FLAG_LOW_DELAY; // Enable low delay mode
    decoderContext_->flags2 |= AV_CODEC_FLAG2_FAST;    // Enable fast decoding
    decoderContext_->flags2 |= AV_CODEC_FLAG2_SHOW_ALL; // Show all frames even if errors
    decoderContext_->thread_count = 1;                 // Single thread for minimal latency

    // Set error resilience for real-time streaming from mobile browsers
    // Safari's WebCodecs encoder resets frame_num frequently, which FFmpeg complains about
    // but can still decode. Suppress errors and continue.
    decoderContext_->err_recognition = AV_EF_IGNORE_ERR | AV_EF_AGGRESSIVE;
    decoderContext_->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
    
    // These options help with discontinuous streams (Safari WebCodecs)
    av_opt_set(decoderContext_->priv_data, "err_detect", "ignore_err", 0);
    av_opt_set_int(decoderContext_->priv_data, "skip_frame", AVDISCARD_NONE, 0);
    
    // Skip loop filter for lower latency (trades quality for speed)
    decoderContext_->skip_loop_filter = AVDISCARD_ALL;

    if (avcodec_open2(decoderContext_, decoder, nullptr) < 0)
    {
        common::logError("WebRTCTransport: Failed to open H.264 decoder");
        if (decoderContext_)
        {
            avcodec_free_context(&decoderContext_);
        }
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

    common::logInfo("WebRTCTransport: H.264 decoder initialized with low-latency optimizations");
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
        if (decoderContext_->extradata)
        {
            av_freep(&decoderContext_->extradata);
            decoderContext_->extradata_size = 0;
        }
        avcodec_free_context(&decoderContext_);
    }
}

// Convert H.264 from AVCC format (length-prefixed) to Annex B format (start code prefixed)
// WebCodecs outputs AVCC, but FFmpeg expects Annex B
std::vector<uint8_t> WebRTCTransport::convertAVCCToAnnexB(const std::vector<std::byte>& avccData)
{
    {
        std::lock_guard<std::mutex> stlock(statsMutex_);
        stats_.h264AvccToAnnexBConversionsAttempted++;
    }
    std::vector<uint8_t> annexBData;
    annexBData.reserve(avccData.size() + 1024); // Reserve extra for start codes

    size_t pos = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(avccData.data());
    const size_t dataSize = avccData.size();

    // Minimum valid sizes for detection
    constexpr size_t MIN_CONFIG_SIZE = 7;
    constexpr size_t MAX_CONFIG_SIZE = 200; // Increased from 100 for compatibility

    // Check if this is an AVCDecoderConfigurationRecord (starts with configurationVersion = 1)
    // Structure: version(1) + profile(1) + compatibility(1) + level(1) + reserved+lengthSize(1) + reserved+numSPS(1) +
    // ...
    if (dataSize >= MIN_CONFIG_SIZE && dataSize <= MAX_CONFIG_SIZE && data[0] == 1)
    {
        common::logInfo("WebRTCTransport: Detected AVCDecoderConfigurationRecord (%zu bytes)", dataSize);
        common::logDebug("WebRTCTransport:   Profile: %u, Compatibility: %u, Level: %u", data[1], data[2], data[3]);

        // Parse AVCC decoder configuration record
        // Byte 0: configurationVersion (must be 1)
        // Byte 1: AVCProfileIndication
        // Byte 2: profile_compatibility
        // Byte 3: AVCLevelIndication
        // Byte 4: 6 bits reserved (all 1) + 2 bits lengthSizeMinusOne
        // Byte 5: 3 bits reserved (all 1) + 5 bits numOfSequenceParameterSets

        if (dataSize < 6)
        {
            common::logError("WebRTCTransport: AVCDecoderConfigurationRecord too short (%zu bytes)", dataSize);
            return annexBData;
        }

        uint8_t lengthSizeMinusOne = data[4] & 0x03;
        uint8_t numSPS = data[5] & 0x1F; // Lower 5 bits
        pos = 6;

        common::logDebug("WebRTCTransport:   NAL length size: %u bytes, Number of SPS: %u", lengthSizeMinusOne + 1,
                         numSPS);

        // Validate numSPS is reasonable
        if (numSPS > 32)
        {
            common::logError("WebRTCTransport: Invalid numSPS: %u (too large)", numSPS);
            return annexBData;
        }

        // Extract SPS (Sequence Parameter Set)
        for (uint8_t i = 0; i < numSPS && pos + 2 <= dataSize; i++)
        {
            // Read SPS length (big-endian 16-bit)
            uint16_t spsLength = (static_cast<uint16_t>(data[pos]) << 8) | static_cast<uint16_t>(data[pos + 1]);
            pos += 2;

            common::logDebug("WebRTCTransport:   SPS #%u: length=%u, pos=%zu, remaining=%zu", i, spsLength, pos,
                             dataSize - pos);

            // Validate SPS length
            if (spsLength == 0 || spsLength > 1024)
            {
                common::logError("WebRTCTransport: Invalid SPS length: %u", spsLength);
                return annexBData;
            }

            if (pos + spsLength > dataSize)
            {
                common::logError("WebRTCTransport: SPS extends beyond buffer (length=%u, available=%zu)", spsLength,
                                 dataSize - pos);
                return annexBData;
            }

            // Write Annex B start code (0x00 0x00 0x00 0x01)
            annexBData.push_back(0x00);
            annexBData.push_back(0x00);
            annexBData.push_back(0x00);
            annexBData.push_back(0x01);

            // Copy SPS data
            annexBData.insert(annexBData.end(), data + pos, data + pos + spsLength);

            // Verify NAL type
            uint8_t nalType = data[pos] & 0x1F;
            if (nalType != 7)
            {
                common::logWarn("WebRTCTransport: Expected SPS (type 7) but got type %u", nalType);
            }
            else
            {
                common::logInfo("WebRTCTransport: Extracted SPS (type 7), length %u", spsLength);
            }

            pos += spsLength;
        }

        // Extract PPS (Picture Parameter Set)
        if (pos >= dataSize)
        {
            common::logError("WebRTCTransport: No room for PPS (pos=%zu, size=%zu)", pos, dataSize);
            return annexBData;
        }

        uint8_t numPPS = data[pos];
        pos++;

        common::logDebug("WebRTCTransport:   Number of PPS: %u", numPPS);

        // Validate numPPS is reasonable
        if (numPPS > 32)
        {
            common::logError("WebRTCTransport: Invalid numPPS: %u (too large)", numPPS);
            return annexBData;
        }

        for (uint8_t i = 0; i < numPPS && pos + 2 <= dataSize; i++)
        {
            // Read PPS length (big-endian 16-bit)
            uint16_t ppsLength = (static_cast<uint16_t>(data[pos]) << 8) | static_cast<uint16_t>(data[pos + 1]);
            pos += 2;

            common::logDebug("WebRTCTransport:   PPS #%u: length=%u, pos=%zu, remaining=%zu", i, ppsLength, pos,
                             dataSize - pos);

            // Validate PPS length
            if (ppsLength == 0 || ppsLength > 1024)
            {
                common::logError("WebRTCTransport: Invalid PPS length: %u", ppsLength);
                return annexBData;
            }

            if (pos + ppsLength > dataSize)
            {
                common::logError("WebRTCTransport: PPS extends beyond buffer (length=%u, available=%zu)", ppsLength,
                                 dataSize - pos);
                return annexBData;
            }

            // Write Annex B start code (0x00 0x00 0x00 0x01)
            annexBData.push_back(0x00);
            annexBData.push_back(0x00);
            annexBData.push_back(0x00);
            annexBData.push_back(0x01);

            // Copy PPS data
            annexBData.insert(annexBData.end(), data + pos, data + pos + ppsLength);

            // Verify NAL type
            uint8_t nalType = data[pos] & 0x1F;
            if (nalType != 8)
            {
                common::logWarn("WebRTCTransport: Expected PPS (type 8) but got type %u", nalType);
            }
            else
            {
                common::logInfo("WebRTCTransport: Extracted PPS (type 8), length %u", ppsLength);
            }

            pos += ppsLength;
        }

        if (annexBData.empty())
        {
            common::logError("WebRTCTransport: Failed to extract any SPS/PPS from configuration");
        }
        else
        {
            common::logInfo("WebRTCTransport: Successfully converted %zu bytes config to %zu bytes Annex B", dataSize,
                            annexBData.size());
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

        // Sanity check - NAL units larger than 10MB are suspicious
        if (nalLength > 10 * 1024 * 1024)
        {
            common::logError("WebRTCTransport: Suspiciously large NAL unit length %u at pos %zu", nalLength, pos - 4);
            break;
        }

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

    if (annexBData.empty())
    {
        common::logWarn("WebRTCTransport: No NAL units extracted from %zu bytes of AVCC data", dataSize);
        {
            std::lock_guard<std::mutex> stlock(statsMutex_);
            stats_.h264AvccToAnnexBConversionsFailed++;
        }
    }
    else
    {
        std::lock_guard<std::mutex> stlock(statsMutex_);
        stats_.h264AvccToAnnexBConversionsSucceeded++;
    }

    return annexBData;
}

std::unique_ptr<Image> WebRTCTransport::decodeH264Frame(const std::vector<std::byte>& h264Data)
{
    std::lock_guard<std::mutex> lock(decoderMutex_);

    common::logDebug("WebRTCTransport: decodeH264Frame called with %zu bytes", h264Data.size());

    if (!decoderContext_ && !initializeDecoder())
    {
        common::logError("WebRTCTransport: Failed to initialize decoder");
        return nullptr;
    }

    // Check if data is already in Annex B format (starts with 0x00 0x00 0x00 0x01 or 0x00 0x00 0x01)
    const uint8_t* data = reinterpret_cast<const uint8_t*>(h264Data.data());
    const size_t dataSize = h264Data.size();
    bool isAnnexB = false;

    if (dataSize >= 4)
    {
        // Check for 4-byte start code
        if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01)
        {
            isAnnexB = true;
            common::logDebug("WebRTCTransport: Data is already in Annex B format (4-byte start code)");
        }
        // Check for 3-byte start code
        else if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01)
        {
            isAnnexB = true;
            common::logDebug("WebRTCTransport: Data is already in Annex B format (3-byte start code)");
        }
    }

    {
        std::lock_guard<std::mutex> stlock(statsMutex_);
        stats_.h264DecodeAttempts++;
    }
    std::vector<uint8_t> annexBData;

    if (isAnnexB)
    {
        // Data is already in Annex B format, use directly
        annexBData.assign(data, data + dataSize);
        common::logDebug("WebRTCTransport: Using data as-is (already Annex B format)");
    }
    else
    {
        // Convert AVCC format (from WebCodecs) to Annex B format (for FFmpeg)
        {
            std::lock_guard<std::mutex> stlock(statsMutex_);
            stats_.h264AvccToAnnexBConversionsAttempted++;
        }
        annexBData = convertAVCCToAnnexB(h264Data);

        if (annexBData.empty())
        {
            std::lock_guard<std::mutex> stlock(statsMutex_);
            stats_.h264AvccToAnnexBConversionsFailed++;
            common::logWarn("WebRTCTransport: Failed to convert AVCC to Annex B or empty result");
            return nullptr;
        }

        common::logDebugVerbose("WebRTCTransport: Converted %zu bytes AVCC to %zu bytes Annex B", h264Data.size(),
                                annexBData.size());
        {
            std::lock_guard<std::mutex> stlock(statsMutex_);
            stats_.h264AvccToAnnexBConversionsSucceeded++;
        }
    }

    // Heuristic: If Annex-B data contains leading SPS/PPS NALs followed by slice NALs,
    // send the SPS/PPS as a separate small packet first to prime the decoder.
    // Some clients (Safari/WebCodecs) may prepend SPS/PPS and decoders can still fail
    // when the combined packet is interpreted incorrectly. Sending them separately
    // ensures decoder has parameter sets available before slice packets arrive.
    if (!annexBData.empty())
    {
        // Find offset where first slice (type 1 or 5) NAL starts
        size_t pos = 0;
        size_t firstSliceOffset = std::string::npos;
        bool foundSPS = false;
        bool foundPPS = false;

        const uint8_t* b = annexBData.data();
        const size_t sz = annexBData.size();

        while (pos + 4 < sz)
        {
            // detect start code
            size_t scLen = 0;
            if (pos + 4 <= sz && b[pos] == 0x00 && b[pos + 1] == 0x00 && b[pos + 2] == 0x00 && b[pos + 3] == 0x01)
            {
                scLen = 4;
            }
            else if (pos + 3 <= sz && b[pos] == 0x00 && b[pos + 1] == 0x00 && b[pos + 2] == 0x01)
            {
                scLen = 3;
            }

            if (scLen == 0)
            {
                pos++;
                continue;
            }

            size_t nalHeaderPos = pos + scLen;
            if (nalHeaderPos >= sz) break;
            uint8_t nalType = b[nalHeaderPos] & 0x1F;

            if (nalType == 7) foundSPS = true;
            else if (nalType == 8) foundPPS = true;
            else if (nalType == 1 || nalType == 5)
            {
                firstSliceOffset = pos;
                break;
            }

            // advance to next start code
            pos = nalHeaderPos + 1;
            while (pos + 3 < sz && !(b[pos] == 0x00 && b[pos + 1] == 0x00 && (b[pos + 2] == 0x00 || b[pos + 2] == 0x01)))
            {
                pos++;
            }
        }

        if (firstSliceOffset != std::string::npos && (foundSPS || foundPPS) && firstSliceOffset > 0)
        {
            // Build SPS/PPS-only packet (from start to just before first slice)
            size_t configSize = firstSliceOffset;
            if (configSize < 200) // small configs are expected
            {
                av_packet_unref(decoderPacket_);
                if (configSize > INT_MAX)
                {
                    common::logWarn("WebRTCTransport: SPS/PPS config size too large: %zu", configSize);
                }
                else
                {
                    int apret2 = av_new_packet(decoderPacket_, static_cast<int>(configSize));
                    if (apret2 >= 0)
                    {
                        memcpy(decoderPacket_->data, annexBData.data(), configSize);
                        decoderPacket_->size = static_cast<int>(configSize);
                        int r2 = avcodec_send_packet(decoderContext_, decoderPacket_);
                        if (r2 == 0)
                        {
                            common::logDebug("WebRTCTransport: Sent SPS/PPS-only packet to decoder (%zu bytes)", configSize);
                        }
                        else if (r2 == AVERROR_INVALIDDATA)
                        {
                            common::logDebug("WebRTCTransport: Decoder returned INVALIDDATA for SPS/PPS-only packet (expected in some cases)");
                        }
                        else if (r2 == AVERROR(EAGAIN))
                        {
                            common::logDebug("WebRTCTransport: Decoder EAGAIN on SPS/PPS send - will drain later");
                        }
                        else
                        {
                            char errBuf2[AV_ERROR_MAX_STRING_SIZE];
                            av_strerror(r2, errBuf2, sizeof(errBuf2));
                            common::logWarn("WebRTCTransport: Sending SPS/PPS-only packet failed (ret: %d, %s)", r2, errBuf2);
                        }
                    }
                }
            }
        }
    }

    // Safari WebCodecs encoder resets GOP (frame_num) periodically, causing "Frame num change from X to 0"
    // errors in FFmpeg. Detect IDR frames and flush decoder to reset reference picture buffer.
    // Also detect SPS which typically precedes keyframes, and flush on those too.
    bool containsIDR = false;
    bool containsSPS = false;
    bool containsPPS = false;
    {
        const uint8_t* b = annexBData.data();
        const size_t sz = annexBData.size();
        size_t pos = 0;
        
        while (pos + 4 < sz)
        {
            // Look for start code (0x00 0x00 0x01 or 0x00 0x00 0x00 0x01)
            size_t scLen = 0;
            if (b[pos] == 0x00 && b[pos + 1] == 0x00)
            {
                if (b[pos + 2] == 0x01)
                {
                    scLen = 3;
                }
                else if (b[pos + 2] == 0x00 && pos + 3 < sz && b[pos + 3] == 0x01)
                {
                    scLen = 4;
                }
            }
            
            if (scLen > 0 && pos + scLen < sz)
            {
                uint8_t nalType = b[pos + scLen] & 0x1F;
                if (nalType == 5) // IDR slice
                {
                    containsIDR = true;
                }
                else if (nalType == 7) // SPS
                {
                    containsSPS = true;
                }
                else if (nalType == 8) // PPS
                {
                    containsPPS = true;
                }
                
                // Skip to next potential start code
                pos += scLen + 1;
            }
            else
            {
                pos++;
            }
        }
    }

    // Safari WebCodecs encoder behavior analysis:
    // - Resets frame_num to 0 every ~12 frames (GOP boundary)
    // - Sends SPS/PPS only with IDR frames
    // - FFmpeg H.264 decoder prints warnings but often still decodes successfully
    //
    // The "Frame num change" and "non-existing PPS" are WARNINGS from libavcodec,
    // not API errors. The decoder is error-resilient and may still produce frames.
    // Reinitializing the decoder loses SPS/PPS state and makes things worse.
    // Flushing alone doesn't reset frame_num tracking.
    //
    // Best approach: Don't interfere. Let the decoder handle it with error resilience.
    // The AV_CODEC_FLAG2_SHOW_ALL and error concealment flags should help.
    
    if (containsIDR || (containsSPS && containsPPS))
    {
        common::logDebug("WebRTCTransport: Keyframe detected (IDR=%d, SPS=%d, PPS=%d) - continuing without flush", 
                        containsIDR, containsSPS, containsPPS);
    }

    // Prepare packet with Annex B H.264 data
    // Use av_new_packet to allocate packet-owned buffer so the decoder has stable memory ownership
    av_packet_unref(decoderPacket_);
    if (annexBData.size() > INT_MAX)
    {
        common::logError("WebRTCTransport: Annex B data size too large: %zu", annexBData.size());
        return nullptr;
    }
    int apret = av_new_packet(decoderPacket_, static_cast<int>(annexBData.size()));
    if (apret < 0)
    {
        common::logError("WebRTCTransport: Failed to allocate AVPacket buffer (%d)", apret);
        return nullptr;
    }
    memcpy(decoderPacket_->data, annexBData.data(), annexBData.size());
    decoderPacket_->size = static_cast<int>(annexBData.size());

    common::logDebugVerbose("WebRTCTransport: Sending packet to decoder (size: %d)", decoderPacket_->size);

    // Send packet to decoder
    // IMPORTANT: EAGAIN on send means decoder input buffer is full - we need to drain output first
    int ret = avcodec_send_packet(decoderContext_, decoderPacket_);
    bool packetSent = false;
    
    if (ret == 0)
    {
        packetSent = true;
    }
    else if (ret == AVERROR(EAGAIN))
    {
        // Decoder input buffer full - drain frames first, then retry send
        common::logDebug("WebRTCTransport: Decoder input full (EAGAIN on send), draining frames before resend");
        std::lock_guard<std::mutex> stlock(statsMutex_);
        stats_.h264DecodeEagain++;
        // Don't return - continue to receive frames below, then retry send
    }
    else if (ret == AVERROR_INVALIDDATA && annexBData.size() < 200)
    {
        // Configuration packets (SPS/PPS only) will return AVERROR_INVALIDDATA because they don't
        // contain frame data, but the decoder DOES store them internally for future use.
        common::logDebug("WebRTCTransport: Decoder stored config packet (%zu bytes), returned INVALIDDATA "
                         "(expected for SPS/PPS-only packets)",
                         annexBData.size());
        packetSent = true;  // Consider it sent even though it returned error
    }
    else if (ret == AVERROR_INVALIDDATA && decoderContext_->extradata_size == 0)
    {
        // Decoder has no extradata (SPS/PPS) - try to extract from in-band NALs and reinitialize
        common::logWarn("WebRTCTransport: Decode failed with no extradata - attempting to extract SPS/PPS from frame");
        
        std::vector<uint8_t> extractedAvcc;
        if (extractAvccFromAnnexB(annexBData, extractedAvcc))
        {
            common::logInfo("WebRTCTransport: Extracted AVCC from in-band NALs (%zu bytes) - reinitializing decoder", 
                           extractedAvcc.size());
            
            // Reinitialize decoder with extracted AVCC
            cleanupDecoder();
            if (initializeDecoder(&extractedAvcc))
            {
                common::logInfo("WebRTCTransport: Decoder reinitialized with in-band AVCC extradata");
                
                // Retry sending the packet
                av_packet_unref(decoderPacket_);
                int apret2 = av_new_packet(decoderPacket_, static_cast<int>(annexBData.size()));
                if (apret2 >= 0)
                {
                    memcpy(decoderPacket_->data, annexBData.data(), annexBData.size());
                    decoderPacket_->size = static_cast<int>(annexBData.size());
                    ret = avcodec_send_packet(decoderContext_, decoderPacket_);
                    if (ret == 0)
                    {
                        packetSent = true;
                        common::logInfo("WebRTCTransport: Retry after reinit succeeded");
                    }
                    else
                    {
                        char errBuf2[AV_ERROR_MAX_STRING_SIZE];
                        av_strerror(ret, errBuf2, sizeof(errBuf2));
                        common::logError("WebRTCTransport: Retry after reinit failed (ret: %d, %s)", ret, errBuf2);
                        return nullptr;
                    }
                }
            }
            else
            {
                common::logError("WebRTCTransport: Failed to reinitialize decoder with extracted AVCC");
                return nullptr;
            }
        }
        else
        {
            // No SPS/PPS in this frame - log and skip
            std::string dump = hexDump(decoderPacket_->data, std::min<size_t>(decoderPacket_->size, 64));
            common::logWarn("WebRTCTransport: No SPS/PPS found in frame - cannot decode until config received. "
                           "First bytes: %s", dump.c_str());
            return nullptr;
        }
    }
    else
    {
        char errBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errBuf, sizeof(errBuf));
        if (ret == AVERROR_INVALIDDATA)
        {
            // Provide extra diagnostics to help debug missing/incorrect SPS/PPS references
            std::string dump = hexDump(decoderPacket_->data, std::min<size_t>(decoderPacket_->size, 64));
            int extradataSz = decoderContext_ ? decoderContext_->extradata_size : 0;
            common::logError("WebRTCTransport: Error sending packet to decoder (INVALIDDATA, ret: %d, %s). "
                             "First bytes: %s, decoder extradata_size=%d",
                             ret, errBuf, dump.c_str(), extradataSz);
        }
        else
        {
            common::logError("WebRTCTransport: Error sending packet to decoder (ret: %d, %s)", ret, errBuf);
        }
        return nullptr;
    }

    common::logDebugVerbose("WebRTCTransport: Receiving frames from decoder (packet_sent=%d)...", packetSent);

    // Receive decoded frame(s) - decoder may buffer multiple frames
    // Keep trying to receive frames until EAGAIN (need more input)
    std::unique_ptr<Image> firstDecodedImage = nullptr;
    
    for (int attemptCount = 0; attemptCount < 10; attemptCount++)
    {
        ret = avcodec_receive_frame(decoderContext_, decodedFrame_);
        
        if (ret == 0)
        {
            // Frame decoded successfully
            common::logDebugVerbose("WebRTCTransport: Frame received from decoder attempt %d: %dx%d", 
                                   attemptCount, decodedFrame_->width, decodedFrame_->height);
            
            // If this is the first frame we've decoded this call, save it to return
            // (subsequent frames are discarded to maintain real-time streaming)
            if (!firstDecodedImage)
            {
                firstDecodedImage = convertDecodedFrameToImage();
                if (!firstDecodedImage)
                {
                    common::logError("WebRTCTransport: Failed to convert decoded frame to image");
                    return nullptr;
                }
            }
            else
            {
                // Discard extra frames (decoder caught up) - log for debugging
                common::logDebug("WebRTCTransport: Discarding buffered frame %d (keeping first frame only)", attemptCount);
            }
            
            // Continue loop to drain any other buffered frames
        }
        else if (ret == AVERROR(EAGAIN))
        {
            // Need more input packets - this is normal
            common::logDebugVerbose("WebRTCTransport: Decoder needs more packets (EAGAIN on receive attempt %d)", attemptCount);
            std::lock_guard<std::mutex> stlock(statsMutex_);
            stats_.h264DecodeEagain++;
            break;  // Exit receive loop
        }
        else if (ret == AVERROR_EOF)
        {
            common::logWarn("WebRTCTransport: Decoder EOF");
            break;
        }
        else
        {
            std::lock_guard<std::mutex> stlock(statsMutex_);
            stats_.h264DecodeFailures++;
            char errBuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errBuf, sizeof(errBuf));
            common::logError("WebRTCTransport: Error receiving frame from decoder (ret: %d, %s)", ret, errBuf);
            break;
        }
    }
    
    // If packet wasn't sent earlier due to EAGAIN, retry now after draining
    if (!packetSent)
    {
        common::logDebug("WebRTCTransport: Retrying packet send after draining frames");
        ret = avcodec_send_packet(decoderContext_, decoderPacket_);
        if (ret < 0 && ret != AVERROR(EAGAIN))
        {
            char errBuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errBuf, sizeof(errBuf));
            common::logError("WebRTCTransport: Error resending packet after drain (ret: %d, %s)", ret, errBuf);
        }
    }
    
    // Return the first decoded frame (or nullptr if no frame was decoded)
    if (firstDecodedImage)
    {
        return firstDecodedImage;
    }
    
    // No frame decoded - this is normal for P-frames that need reference frames
    common::logDebugVerbose("WebRTCTransport: No frame decoded this call (decoder accumulating data)");
    return nullptr;
}

std::unique_ptr<Image> WebRTCTransport::convertDecodedFrameToImage()
{
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
    // Allocate RGB buffer: width * height * 3 bytes (RGB24)
    const size_t rgbSize = decodedFrame_->width * decodedFrame_->height * 3;
    auto decodedImage = std::make_unique<Image>(rgbSize);

    // CRITICAL: Allocate temporary aligned buffer for sws_scale to prevent crashes
    // FFmpeg's SIMD operations require 32-byte alignment, but our Image class doesn't support stride
    // So we decode to aligned temp buffer, then copy to unaligned Image buffer
    const int alignedLinesize = ((decodedFrame_->width * 3 + 31) / 32) * 32;
    const size_t alignedSize = alignedLinesize * decodedFrame_->height;
    std::vector<uint8_t> alignedBuffer(alignedSize);

    uint8_t* dstData[1] = {alignedBuffer.data()};
    int dstLinesize[1] = {alignedLinesize};

    // Convert YUV420P to RGB into aligned temporary buffer
    sws_scale(decoderSwsContext_, decodedFrame_->data, decodedFrame_->linesize, 0, decodedFrame_->height, dstData,
              dstLinesize);

    // Copy from aligned buffer to Image buffer, removing padding
    const int actualLinesize = decodedFrame_->width * 3;
    uint8_t* imageDst = decodedImage->data();
    const uint8_t* alignedSrc = alignedBuffer.data();

    for (int y = 0; y < decodedFrame_->height; y++)
    {
        std::memcpy(imageDst + y * actualLinesize, alignedSrc + y * alignedLinesize, actualLinesize);
    }

    // Set Image metadata
    decodedImage->info.width = decodedFrame_->width;
    decodedImage->info.height = decodedFrame_->height;
    decodedImage->info.pixelSizeBytes = 3;
    decodedImage->info.TJPixelFormat = TJPF_RGB;
    decodedImage->info.format = ImageFormat::RAW;

    common::logDebugVerbose("WebRTCTransport: Converted decoded frame to RGB: %dx%d", decodedFrame_->width,
                            decodedFrame_->height);

    {
        std::lock_guard<std::mutex> stlock(statsMutex_);
        stats_.h264DecodeSuccesses++;
        
        // Log decode success rate every 50 frames
        if (stats_.h264DecodeSuccesses % 50 == 0)
        {
            double successRate = (stats_.h264DecodeAttempts > 0) 
                ? (100.0 * stats_.h264DecodeSuccesses / stats_.h264DecodeAttempts)
                : 0.0;
            common::logInfo("WebRTCTransport: H.264 decode stats - attempts: %lu, successes: %lu, failures: %lu, rate: %.1f%%",
                           stats_.h264DecodeAttempts, stats_.h264DecodeSuccesses, 
                           stats_.h264DecodeFailures, successRate);
        }
    }

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

bool WebRTCTransport::parseChunkHeader(const std::vector<std::byte>& data, ChunkHeader& header)
{
    // Check for old 5-byte header format (backwards compatibility during transition)
    // Old format: [flags:1][chunkIdx:2][totalChunks:2] = 5 bytes
    // New format: [flags:1][chunkIdx:2][totalChunks:2][frameSequence:4] = 9 bytes
    
    if (data.size() < 5)
    {
        return false; // Too small for any header
    }
    
    header.flags = static_cast<uint8_t>(data[0]);
    header.chunkIdx = static_cast<uint16_t>(data[1]) | (static_cast<uint16_t>(data[2]) << 8);
    header.totalChunks = static_cast<uint16_t>(data[3]) | (static_cast<uint16_t>(data[4]) << 8);
    
    // Detect format by checking if we have 9 bytes AND if totalChunks is reasonable
    // If totalChunks is invalid (>100), assume it's old 5-byte format misaligned
    if (data.size() >= 9 && header.totalChunks >= 2 && header.totalChunks <= 100)
    {
        // Try parsing as 9-byte format
        header.frameSequence = static_cast<uint32_t>(data[5]) | (static_cast<uint32_t>(data[6]) << 8) |
                              (static_cast<uint32_t>(data[7]) << 16) | (static_cast<uint32_t>(data[8]) << 24);
        
        common::logDebug("WebRTCTransport: Parsed 9-byte header: flags=%u, chunkIdx=%u, totalChunks=%u, seq=%u",
                               header.flags, header.chunkIdx, header.totalChunks, header.frameSequence);
    }
    else
    {
        common::logDebug("WebRTCTransport: Parsed 5-byte header - Aborting");
        return false;
    }
    
    return true;
}

bool WebRTCTransport::validateChunkHeader(const ChunkHeader& header, size_t dataSize)
{
    // Sanity checks: reasonable chunk counts and valid index
    if (header.totalChunks < 2 || header.totalChunks > 100)
    {
        common::logWarn("WebRTCTransport: Invalid totalChunks: %u", header.totalChunks);
        return false;
    }
    
    if (header.chunkIdx >= header.totalChunks)
    {
        common::logWarn("WebRTCTransport: Invalid chunkIdx %u >= totalChunks %u", header.chunkIdx, header.totalChunks);
        return false;
    }
    
    // Ensure we have data beyond the header (at least 5 bytes for old format, 9 for new)
    const size_t minHeaderSize = 5;
    if (dataSize <= minHeaderSize)
    {
        common::logWarn("WebRTCTransport: Chunk has no payload data (size=%zu)", dataSize);
        return false;
    }
    
    return true;
}

void WebRTCTransport::handleIncomingChunk(PeerConnection& peer, const std::vector<std::byte>& chunkData, 
                                          const ChunkHeader& header)
{
    // Use frame sequence as unique identifier (or fallback to totalChunks for old format)
    const uint32_t frameId = (header.frameSequence != 0) ? header.frameSequence : header.totalChunks;
    
    // Detect header size: 9 bytes if we have sequence, 5 bytes for legacy
    const size_t headerSize = (header.frameSequence != 0) ? 9 : 5;
    
    // Create or get existing chunk buffer for this frame
    auto& frameBuffer = peer.incomingChunkBuffers[frameId];
    
    // Initialize buffer on first chunk
    if (frameBuffer.receivedCount == 0)
    {
        frameBuffer.chunks.resize(header.totalChunks);
        frameBuffer.totalChunks = header.totalChunks;
        frameBuffer.isKeyframe = header.isKeyframe();
        frameBuffer.frameSequence = header.frameSequence;
        frameBuffer.firstChunkTime = std::chrono::steady_clock::now();
        
        common::logDebug("WebRTCTransport: Starting frame reassembly (seq=%u, totalChunks=%u, keyframe=%d)",
                        frameId, header.totalChunks, header.isKeyframe());
    }
    
    // Store chunk data (skip header)
    const size_t payloadSize = chunkData.size() - headerSize;
    frameBuffer.chunks[header.chunkIdx].resize(payloadSize);
    std::memcpy(frameBuffer.chunks[header.chunkIdx].data(),
                reinterpret_cast<const uint8_t*>(chunkData.data()) + headerSize,
                payloadSize);
    
    frameBuffer.receivedCount++;
    
    common::logDebug("WebRTCTransport: Received chunk %u/%u for frame seq=%u (%zu bytes payload)",
                    header.chunkIdx + 1, header.totalChunks, frameId, payloadSize);
    
    // Check if frame is complete
    if (frameBuffer.receivedCount == frameBuffer.totalChunks)
    {
        // Measure reassembly time
        auto reassemblyTime = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            reassemblyTime - frameBuffer.firstChunkTime).count();
        
        // Calculate total frame size
        size_t totalSize = 0;
        for (const auto& chunk : frameBuffer.chunks)
        {
            totalSize += chunk.size();
        }
        
        common::logInfo("WebRTCTransport: Frame complete (seq=%u, %u chunks, %zu bytes, reassembly took %ldms)",
                       frameId, frameBuffer.totalChunks, totalSize, elapsedMs);
    }
}

void WebRTCTransport::sendChunkedData(rtc::DataChannel& dc, const std::vector<uint8_t>& data, 
                                      bool isKeyframe, int64_t sendMs, uint32_t frameSequence)
{
    constexpr size_t CHUNK_SIZE = 12 * 1024; // 12KB per chunk
    const uint16_t totalChunks = static_cast<uint16_t>((data.size() + CHUNK_SIZE - 1) / CHUNK_SIZE);
    
    common::logDebug("WebRTCTransport: Chunking %zu bytes into %u chunks (seq=%u, keyframe=%d)",
                    data.size(), totalChunks, frameSequence, isKeyframe);
    
    for (uint16_t chunkIdx = 0; chunkIdx < totalChunks; chunkIdx++)
    {
        const size_t offset = chunkIdx * CHUNK_SIZE;
        const size_t chunkSize = std::min(CHUNK_SIZE, data.size() - offset);
        
        // Build chunk: [flags:1][chunkIdx:2][totalChunks:2][frameSequence:4][data...]
        std::vector<uint8_t> chunk(9 + chunkSize);
        
        // Flags: bit 0=first, bit 1=last, bit 2=keyframe
        uint8_t flags = 0;
        if (chunkIdx == 0) flags |= 0x01;
        if (chunkIdx == totalChunks - 1) flags |= 0x02;
        if (isKeyframe) flags |= 0x04;
        
        chunk[0] = flags;
        chunk[1] = chunkIdx & 0xFF;
        chunk[2] = (chunkIdx >> 8) & 0xFF;
        chunk[3] = totalChunks & 0xFF;
        chunk[4] = (totalChunks >> 8) & 0xFF;
        chunk[5] = frameSequence & 0xFF;
        chunk[6] = (frameSequence >> 8) & 0xFF;
        chunk[7] = (frameSequence >> 16) & 0xFF;
        chunk[8] = (frameSequence >> 24) & 0xFF;
        
        // Copy payload
        std::memcpy(chunk.data() + 9, data.data() + offset, chunkSize);
        
        dc.send(reinterpret_cast<const std::byte*>(chunk.data()), chunk.size());
        
        common::logDebug("WebRTCTransport: Sent chunk %u/%u (seq=%u, %zu bytes + 9 byte header, timestamp=%ld)",
                        chunkIdx + 1, totalChunks, frameSequence, chunkSize, sendMs);
    }
}

void WebRTCTransport::broadcastEncodedFrame(const std::vector<uint8_t>& data, bool isKeyFrame)
{
    std::lock_guard<std::mutex> lock(peersMutex_);

    // Track server send timing
    auto sendTime = std::chrono::system_clock::now();
    auto sendMs = std::chrono::duration_cast<std::chrono::milliseconds>(sendTime.time_since_epoch()).count();

    auto now = std::chrono::steady_clock::now();

    for (auto& [peerId, peer] : peers_)
    {
        // Check if peer is connected and actively sending frames
        bool isPeerConnected = peer.dataChannel && peer.dataChannel->isOpen() && peer.pc
                               && peer.pc->state() == rtc::PeerConnection::State::Connected && peer.isSendingFrames;

        if (!isPeerConnected)
        {
            continue;
        }

        // Skip peers that haven't sent us frames recently (mirror-only contract)
        if (peer.lastInboundFrameTime.time_since_epoch().count() == 0)
        {
            continue;
        }

        auto silence = now - peer.lastInboundFrameTime;
        if (silence > PEER_INACTIVITY_TIMEOUT)
        {
            peer.isSendingFrames = false;
            common::logWarn(
                "WebRTCTransport: Peer %s timed out after %.1f ms without inbound frames - pausing downlink",
                peerId.c_str(), std::chrono::duration<double, std::milli>(silence).count());
            continue;
        }

        try
        {
            constexpr size_t CHUNK_SIZE = 12 * 1024; // 12KB per chunk

            // Get unique frame sequence for this peer
            const uint32_t frameSeq = peer.nextOutgoingFrameSequence++;
            
            // For small frames (< CHUNK_SIZE), send directly without chunking overhead
            if (data.size() <= CHUNK_SIZE)
            {
                peer.dataChannel->send(reinterpret_cast<const std::byte*>(data.data()), data.size());
                
                {
                    std::lock_guard<std::mutex> statsLock(statsMutex_);
                    stats_.framesSent++;

                    if (stats_.framesSent % 30 == 0)
                    {
                        common::logInfo("WebRTCTransport: [SERVER_SEND] Frame %zu at t=%lld ms (single: %zu bytes)",
                                      stats_.framesSent, sendMs, data.size());
                    }
                }
            }
            else
            {
                // Large frame - use helper method for chunking with unique sequence
                sendChunkedData(*peer.dataChannel, data, isKeyFrame, sendMs, frameSeq);
                
                {
                    std::lock_guard<std::mutex> statsLock(statsMutex_);
                    stats_.framesSent++;

                    if (stats_.framesSent % 30 == 0)
                    {
                        common::logInfo("WebRTCTransport: [SERVER_SEND] Frame %zu at t=%lld ms (chunked: %zu bytes, seq=%u)",
                                      stats_.framesSent, sendMs, data.size(), frameSeq);
                    }
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
                            "WebRTCTransport: [SERVER_RECV] Received data at t=%lld ms: %zu bytes from %s",
                            recvMs, binaryData.size(), peerId.c_str());

                        // Mark peer as actively sending frames (bidirectional communication)
                        {
                            auto now = std::chrono::steady_clock::now();
                            std::lock_guard<std::mutex> lock(peersMutex_);
                            auto it = peers_.find(peerId);
                            if (it != peers_.end())
                            {
                                it->second.lastInboundFrameTime = now;
                                if (!it->second.isSendingFrames)
                                {
                                    it->second.isSendingFrames = true;
                                    common::logInfo("WebRTCTransport: Peer %s is now sending camera frames - enabling "
                                                    "bidirectional streaming",
                                                    peerId.c_str());
                                }
                            }
                        }

                        // Check for decoder config message from client (type prefix 0x01 = AVCC config)
                        // Format: [0x01][AVCC bytes...] where AVCC starts with 0x01 (configurationVersion)
                        // So valid config message bytes: [0x01][0x01][profile][compat][level][...]
                        // This differs from chunk messages where byte[1] is chunk index (usually 0x00)
                        // Client sends this immediately after DataChannel opens so server can init decoder
                        if (binaryData.size() >= 8 && 
                            static_cast<uint8_t>(binaryData[0]) == 0x01 &&
                            static_cast<uint8_t>(binaryData[1]) == 0x01)  // AVCC configurationVersion
                        {
                            // Extract AVCC (skip first byte which is message type)
                            std::vector<uint8_t> avccData(binaryData.size() - 1);
                            for (size_t i = 1; i < binaryData.size(); ++i)
                            {
                                avccData[i - 1] = static_cast<uint8_t>(binaryData[i]);
                            }
                            
                            // Further validate AVCC structure
                            if (avccData.size() >= 7)
                            {
                                common::logInfo("WebRTCTransport: Received decoder config from client (AVCC, %zu bytes)", 
                                               avccData.size());
                                
                                // Reinitialize decoder with client-provided AVCC extradata
                                // Must cleanup existing decoder first since extradata must be set before open
                                {
                                    std::lock_guard<std::mutex> decoderLock(decoderMutex_);
                                    cleanupDecoder();
                                }
                                
                                if (initializeDecoder(&avccData))
                                {
                                    common::logInfo("WebRTCTransport: Decoder reinitialized with client AVCC extradata (%zu bytes)", 
                                                   avccData.size());
                                }
                                else
                                {
                                    common::logError("WebRTCTransport: Failed to reinitialize decoder with client AVCC");
                                }
                                return; // Config message handled
                            }
                            // else: too short to be valid AVCC, fall through to chunk parser
                        }

                        // Try to parse as chunked message
                        ChunkHeader header;
                        if (parseChunkHeader(binaryData, header) && validateChunkHeader(header, binaryData.size()))
                        {
                            // Process chunk and potentially reassemble complete frame
                            std::vector<std::byte> completeFrame;
                            bool frameComplete = false;
                            
                            {
                                std::lock_guard<std::mutex> lock(peersMutex_);
                                auto peerIt = peers_.find(peerId);
                                if (peerIt == peers_.end())
                                {
                                    common::logWarn("WebRTCTransport: Peer %s not found for chunk reassembly", peerId.c_str());
                                    return;
                                }
                                
                                auto& peer = peerIt->second;
                                handleIncomingChunk(peer, binaryData, header);
                                
                                // Check if frame is complete after adding this chunk
                                const uint32_t frameId = header.frameSequence;
                                auto bufferIt = peer.incomingChunkBuffers.find(frameId);
                                if (bufferIt != peer.incomingChunkBuffers.end() && 
                                    bufferIt->second.receivedCount == bufferIt->second.totalChunks)
                                {
                                    // Reassemble complete frame
                                    auto& frameBuffer = bufferIt->second;
                                    size_t totalSize = 0;
                                    for (const auto& chunk : frameBuffer.chunks)
                                    {
                                        totalSize += chunk.size();
                                    }
                                    
                                    completeFrame.reserve(totalSize);
                                    for (const auto& chunk : frameBuffer.chunks)
                                    {
                                        for (uint8_t byte : chunk)
                                        {
                                            completeFrame.push_back(static_cast<std::byte>(byte));
                                        }
                                    }
                                    
                                    // Clean up buffer
                                    peer.incomingChunkBuffers.erase(frameId);
                                    frameComplete = true;
                                }
                            } // Lock released here
                            
                            // Process reassembled frame outside lock to avoid deadlock
                            if (frameComplete)
                            {
                                processDecodedH264(peerId, completeFrame);
                            }
                            
                            return; // Chunked message handled
                        }
                        
                        // Not chunked - treat as complete H.264 frame
                        common::logDebugVerbose("WebRTCTransport: Received complete frame: %zu bytes", binaryData.size());
                        processDecodedH264(peerId, binaryData);
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

        // Set up H.264 RTP packetizer for automatic packetization of Annex B frames
        // Extract RTP parameters from the negotiated track's media description
        auto media = negotiatedTrack->description();

        // Get SSRC and CNAME from media description
        auto ssrcs = media.getSSRCs();
        uint32_t ssrc = ssrcs.empty() ? 1 : ssrcs[0]; // Use first SSRC or default

        auto cnameOpt = ssrcs.empty() ? std::nullopt : media.getCNameForSsrc(ssrcs[0]);
        std::string cname = cnameOpt.value_or("linuxface-video");

        // Find the first H.264 payload type from the media description
        uint8_t payloadType = 96; // Default fallback
        auto payloadTypes = media.payloadTypes();

        for (int pt : payloadTypes)
        {
            const auto* rtpMap = media.rtpMap(pt);
            if (rtpMap
                && (rtpMap->format.find("H264") != std::string::npos
                    || rtpMap->format.find("h264") != std::string::npos))
            {
                payloadType = static_cast<uint8_t>(pt);
                common::logInfo("WebRTCTransport: Found H.264 payload type %u (format: %s)", payloadType,
                                rtpMap->format.c_str());
                break;
            }
        }

        // Attempt to extract SPS/PPS from the offer SDP (sprop-parameter-sets) and feed decoder
        try
        {
            // Extract raw sprop NAL units (SPS/PPS) from SDP and build AVCC extradata
            auto spropNals = extractSpropNalUnitsFromSdp(offerSdp, payloadType);
            if (!spropNals.empty())
            {
                std::vector<uint8_t> avcc;
                if (buildAvccFromNalUnits(spropNals, avcc))
                {
                    common::logInfo("WebRTCTransport: Found sprop-parameter-sets for payload %u, setting decoder extradata", payloadType);

                    // Initialize decoder with AVCC extradata so FFmpeg knows SPS/PPS before slices arrive
                    if (!initializeDecoder(&avcc))
                    {
                        common::logWarn("WebRTCTransport: Failed to initialize decoder with AVCC extradata, falling back to sending Annex-B config");
                        // fallback: send Annex-B formatted NALs to decoder to prime it
                        auto spropAnnexB = extractSpropAnnexBFromSdp(offerSdp, payloadType);
                        if (!spropAnnexB.empty())
                        {
                            std::vector<std::byte> cfg(spropAnnexB.size());
                            for (size_t i = 0; i < spropAnnexB.size(); ++i)
                                cfg[i] = static_cast<std::byte>(spropAnnexB[i]);
                            (void)decodeH264Frame(cfg);
                        }
                    }
                    else
                    {
                        // Decoder initialized with extradata; nothing more to do
                    }
                }
                else
                {
                    common::logWarn("WebRTCTransport: Could not build AVCC from sprop tokens - skipping extradata setup");
                }
            }
        }
        catch (const std::exception& e)
        {
            common::logWarn("WebRTCTransport: Failed to parse sprop-parameter-sets: %s", e.what());
        }

        common::logInfo("WebRTCTransport: RTP config: ssrc=%u, cname=%s, payloadType=%u, clockRate=%u", ssrc,
                        cname.c_str(), payloadType, rtc::H264RtpPacketizer::ClockRate);

        // Create RTP packetization config with parameters from negotiated media
        auto rtpConfig =
            std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, payloadType, rtc::H264RtpPacketizer::ClockRate);

        // Create H.264 packetizer with Annex B start sequence format (0x00 0x00 0x00 0x01)
        // Our FFmpeg encoder outputs Annex B format, so StartSequence is correct
        auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtpConfig);

        // Set the media handler on the track - this will automatically packetize H.264 frames into RTP
        negotiatedTrack->setMediaHandler(packetizer);
        common::logInfo("WebRTCTransport: H.264 RTP packetizer configured successfully");
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

    // Debug log to see what we're receiving
    common::logInfo("WebRTCTransport: Received ICE candidate - candidate: '%s', mid: '%s'", candidate.c_str(),
                    mid.c_str());

    try
    {
        rtc::Candidate rtcCandidate(candidate, mid);
        it->second.pc->addRemoteCandidate(rtcCandidate);
        common::logInfo("WebRTCTransport: Successfully added ICE candidate for peer %s", peerId.c_str());
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

void WebRTCTransport::processDecodedH264(const std::string& peerId, const std::vector<std::byte>& h264Data)
{
    common::logDebug("WebRTCTransport: processDecodedH264 called for peer %s with %zu bytes", peerId.c_str(),
                     h264Data.size());

    if (h264Data.empty())
    {
        common::logWarn("WebRTCTransport: Empty H.264 data received from peer %s", peerId.c_str());
        return;
    }

    std::lock_guard<std::mutex> lock(peersMutex_);
    auto it = peers_.find(peerId);
    if (it == peers_.end())
    {
        common::logWarn("WebRTCTransport: Received H.264 data from unknown peer %s", peerId.c_str());
        return;
    }

    PeerConnection& peer = it->second;

    // Frames arrive complete either:
    //   1. Via chunk reassembly (Safari/modern browsers)
    //   2. Directly complete (Firefox/Chrome desktop)
    processDesktopH264(peer, h264Data);
}

void WebRTCTransport::processDesktopH264(PeerConnection& peer, const std::vector<std::byte>& h264Data)
{
    common::logDebug("WebRTCTransport: Processing desktop H.264 frame (%zu bytes)", h264Data.size());

    // Desktop browsers send complete frames in Annex B format - decode directly
    // Process immediately without any buffering for minimum latency
    auto decodedImage = decodeH264Frame(h264Data);

    if (decodedImage)
    {
        common::logInfo("WebRTCTransport: Desktop H.264 decoded OK: %dx%d (%zu bytes input)", 
                       decodedImage->info.width, decodedImage->info.height, h264Data.size());

        // Push frame immediately for lowest latency
        std::lock_guard<std::mutex> lock(inputDeviceMutex_);
        if (inputDevice_)
        {
            // Track latency for debugging
            auto now = std::chrono::high_resolution_clock::now();
            inputDevice_->pushRGBFrame(std::move(*decodedImage));
            auto elapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - now)
                    .count();
            common::logDebug("WebRTCTransport: Frame pushed to input device in %ld μs", elapsed);
        }
        else
        {
            common::logWarn("WebRTCTransport: No input device available for decoded frame");
        }
    }
    else
    {
        common::logWarn("WebRTCTransport: Desktop H.264 decode failed - attempting fallback buffering/repair");

        // Heuristic: if the packet alone is not a complete Annex-B frame, append it to peer buffer and
        // process once we have a complete frame, similar to mobileSafari handling.
        const uint8_t* data = reinterpret_cast<const uint8_t*>(h264Data.data());
        std::vector<uint8_t> buffer(data, data + h264Data.size());
        auto now = std::chrono::steady_clock::now();
        if (!detectCompleteFrame(buffer, now))
        {
            // Append to per-peer buffer
            constexpr size_t MAX_BUFFER_SIZE = 5 * 1024 * 1024; // 5MB max
            if (peer.h264PacketBuffer.size() + buffer.size() > MAX_BUFFER_SIZE)
            {
                common::logWarn("WebRTCTransport: Desktop H.264 buffer overflow, clearing buffer");
                peer.h264PacketBuffer.clear();
                peer.h264BufferHasPendingData = false;
                {
                    std::lock_guard<std::mutex> stlock(statsMutex_);
                    stats_.h264BufferOverflows++;
                }
            }
            peer.h264PacketBuffer.insert(peer.h264PacketBuffer.end(), buffer.begin(), buffer.end());
            peer.lastH264PacketTime = now;
            peer.h264BufferHasPendingData = true;
            {
                std::lock_guard<std::mutex> stlock(statsMutex_);
                stats_.h264DesktopFallbacks++;
            }
            common::logDebug("WebRTCTransport: Appended %zu bytes to desktop buffer (total: %zu bytes)", buffer.size(),
                             peer.h264PacketBuffer.size());

            // If after appending we have a complete frame, process it
            if (detectCompleteFrame(peer.h264PacketBuffer, peer.lastH264PacketTime))
            {
                common::logDebug("WebRTCTransport: Assembled complete desktop H.264 frame (%zu bytes) after buffering",
                                 peer.h264PacketBuffer.size());
                std::vector<std::byte> frameData(peer.h264PacketBuffer.size());
                std::memcpy(frameData.data(), peer.h264PacketBuffer.data(), peer.h264PacketBuffer.size());
                auto decodedImage2 = decodeH264Frame(frameData);
                if (decodedImage2)
                {
                    common::logDebug("WebRTCTransport: Desktop H.264 decoded successfully after buffering: %dx%d",
                                     decodedImage2->info.width, decodedImage2->info.height);
                    std::lock_guard<std::mutex> lock(inputDeviceMutex_);
                    if (inputDevice_)
                    {
                        inputDevice_->pushRGBFrame(std::move(*decodedImage2));
                    }
                }
                else
                {
                    common::logWarn("WebRTCTransport: Desktop H.264 decode failed even after buffering");
                }
                peer.h264PacketBuffer.clear();
                peer.h264BufferHasPendingData = false;
            }
        }
        else
        {
            // Packet appears complete but decode still failed; log and drop to avoid indefinite buffering
            common::logWarn("WebRTCTransport: Desktop packet appears complete but decode failed; dropping packet");
        }
    }
}

bool WebRTCTransport::detectCompleteFrame(const std::vector<uint8_t>& buffer,
                                          const std::chrono::steady_clock::time_point& lastPacketTime)
{
    if (buffer.size() < 5)
    {
        return false; // Too small to be a complete frame
    }

    // Track which NAL units we've found and their positions
    bool foundSPS = false;
    bool foundPPS = false;
    bool foundIDR = false;
    bool foundSlice = false;
    int nalUnitsFound = 0;

    // Scan for NAL unit start codes (0x00 0x00 0x00 0x01) and check NAL types
    for (size_t i = 0; i + 4 < buffer.size(); ++i)
    {
        if (buffer[i] == 0x00 && buffer[i + 1] == 0x00 && buffer[i + 2] == 0x00 && buffer[i + 3] == 0x01)
        {
            if (i + 4 < buffer.size()) // Ensure we can read NAL header
            {
                uint8_t nalHeader = buffer[i + 4];
                uint8_t nalType = nalHeader & 0x1F; // Lower 5 bits
                nalUnitsFound++;

                switch (nalType)
                {
                    case 7:
                        foundSPS = true;
                        break; // Sequence Parameter Set
                    case 8:
                        foundPPS = true;
                        break; // Picture Parameter Set
                    case 5:
                        foundIDR = true;
                        break; // IDR slice (keyframe)
                    case 1:
                        foundSlice = true;
                        break; // Non-IDR slice (P-frame)
                }
            }
        }
    }

    // Keyframe must have: SPS + PPS + IDR (3+ NAL units)
    if (foundSPS && foundPPS && foundIDR && nalUnitsFound >= 3)
    {
        common::logDebug("WebRTCTransport: Complete keyframe detected (SPS+PPS+IDR, %d NALs, %zu bytes)", nalUnitsFound,
                         buffer.size());
        return true;
    }

    // P-frame detection: Need to verify slice is COMPLETE, not fragmented
    // iPhone hardware encoder sends fragmented slices across multiple packets.
    // Strategy: Wait for timeout (50ms) after last packet before accepting P-frame
    // This allows fragmented slices to accumulate into complete frames.
    if (foundSlice)
    {
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastPacket = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPacketTime).count();

        // Wait 50ms after last packet to ensure all fragments received
        if (timeSinceLastPacket >= 50)
        {
            common::logDebug("WebRTCTransport: P-frame timeout reached (%lld ms, %zu bytes, %d NALs)",
                             timeSinceLastPacket, buffer.size(), nalUnitsFound);
            return true;
        }
        else
        {
            common::logDebugVerbose("WebRTCTransport: P-frame waiting for more data (%lld ms < 50ms)",
                                    timeSinceLastPacket);
            return false;
        }
    }

    // Incomplete frame - log periodically to avoid spam
    static size_t logCounter = 0;
    if (++logCounter % 10 == 0)
    {
        common::logDebug("WebRTCTransport: Incomplete frame (%zu bytes, %d NALs, SPS:%d PPS:%d IDR:%d Slice:%d)",
                         buffer.size(), nalUnitsFound, foundSPS, foundPPS, foundIDR, foundSlice);
    }

    return false;
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

} // namespace linuxface::web

