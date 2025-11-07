#include "LinuxFace/web/webrtcTransport.h"

#include <chrono>
#include <sstream>

#include "LinuxFace/common.h"

namespace linuxface
{
namespace web
{

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
    queueCV_.notify_all();

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

    // Clear frame queue
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!frameQueue_.empty())
        {
            frameQueue_.pop();
        }
    }

    cleanupEncoder();

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

    std::unique_lock<std::mutex> lock(queueMutex_);

    // Drop frame if queue is full (simple backpressure)
    if (frameQueue_.size() >= 2)
    {
        {
            std::lock_guard<std::mutex> statsLock(statsMutex_);
            stats_.framesDropped++;
        }
        lock.unlock();
        queueCV_.notify_one();
        return;
    }

    frameQueue_.push(frame->deepCopy());
    lock.unlock();
    queueCV_.notify_one();
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
            // Skip encoding if no active peers
            if (!hasActiveConnections())
            {
                continue;
            }

            // Initialize encoder if needed
            if (!codecContext_ || lastEncoderWidth_ != frame->info.width ||
                lastEncoderHeight_ != frame->info.height)
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

    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec)
    {
        common::logError("WebRTCTransport: libx264 encoder not found");
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
    codecContext_->gop_size = config_.webrtcFramerate;  // One keyframe per second
    codecContext_->max_b_frames = 0;  // Disable B-frames for lower latency

    // Set preset for encoding speed/quality tradeoff
    av_opt_set(codecContext_->priv_data, "preset", config_.webrtcPreset.c_str(), 0);
    
    // Low-latency tuning
    av_opt_set(codecContext_->priv_data, "tune", "zerolatency", 0);
    
    // Annex B format for RTP packetization
    av_opt_set(codecContext_->priv_data, "annex_b", "1", 0);
    
    // Repeat SPS/PPS for robustness
    av_opt_set(codecContext_->priv_data, "repeat-headers", "1", 0);

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

        swsContext_ = sws_getContext(frame->info.width, frame->info.height, srcFormat, codecContext_->width,
                                     codecContext_->height, codecContext_->pix_fmt, SWS_BILINEAR, nullptr, nullptr,
                                     nullptr);

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
        return false;  // Need more frames
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

    for (auto& [peerId, peer] : peers_)
    {
        if (peer.track && peer.pc && peer.pc->state() == rtc::PeerConnection::State::Connected)
        {
            try
            {
                // Send RTP packet - convert uint8_t* to std::byte*
                peer.track->send(reinterpret_cast<const std::byte*>(data.data()), data.size());

                {
                    std::lock_guard<std::mutex> statsLock(statsMutex_);
                    stats_.framesSent++;
                }

                peer.lastFrameTime = std::chrono::steady_clock::now();
            }
            catch (const std::exception& e)
            {
                common::logError("WebRTCTransport: Failed to send frame to peer %s: %s", peerId.c_str(), e.what());
            }
        }
    }
}

std::string WebRTCTransport::createPeerConnection(const std::string& peerId)
{
    std::lock_guard<std::mutex> lock(peersMutex_);

    // Remove existing connection if any
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

    common::logInfo("WebRTCTransport: Creating peer connection for %s", peerId.c_str());

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
    peerConn.pc->onStateChange([peerId](rtc::PeerConnection::State state) {
        common::logInfo("WebRTCTransport: Peer %s state changed to %d", peerId.c_str(), static_cast<int>(state));
    });

    peerConn.pc->onGatheringStateChange([peerId](rtc::PeerConnection::GatheringState state) {
        common::logInfo("WebRTCTransport: Peer %s gathering state changed to %d", peerId.c_str(),
                        static_cast<int>(state));
    });

    // Create video track
    auto video = rtc::Description::Video("video", rtc::Description::Direction::SendOnly);
    video.addH264Codec(96);  // Payload type 96 for H.264
    video.setBitrate(config_.webrtcBitrate / 1000);  // Convert to kbps

    peerConn.track = peerConn.pc->addTrack(video);

    // Store peer connection
    peers_[peerId] = std::move(peerConn);

    // Create and return local offer
    std::string localSdp;
    std::promise<void> offerPromise;
    auto offerFuture = offerPromise.get_future();

    peerConn.pc->onLocalDescription([&localSdp, &offerPromise](rtc::Description desc) {
        localSdp = std::string(desc);
        offerPromise.set_value();
    });

    peerConn.pc->setLocalDescription();

    // Wait for local description with timeout
    if (offerFuture.wait_for(std::chrono::seconds(5)) == std::future_status::timeout)
    {
        common::logError("WebRTCTransport: Timeout waiting for local description");
        peers_.erase(peerId);
        return "";
    }

    common::logInfo("WebRTCTransport: Created offer for peer %s", peerId.c_str());
    return localSdp;
}

bool WebRTCTransport::processAnswer(const std::string& peerId, const std::string& sdp)
{
    std::lock_guard<std::mutex> lock(peersMutex_);

    auto it = peers_.find(peerId);
    if (it == peers_.end())
    {
        common::logError("WebRTCTransport: Peer %s not found", peerId.c_str());
        return false;
    }

    try
    {
        rtc::Description answer(sdp, "answer");
        it->second.pc->setRemoteDescription(answer);
        common::logInfo("WebRTCTransport: Processed answer from peer %s", peerId.c_str());
        return true;
    }
    catch (const std::exception& e)
    {
        common::logError("WebRTCTransport: Failed to process answer from peer %s: %s", peerId.c_str(), e.what());
        return false;
    }
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

} // namespace web
} // namespace linuxface
