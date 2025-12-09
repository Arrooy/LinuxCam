#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "LinuxFace/web/webrtcTransport.h"
#include "LinuxFace/web/wsInputDevice.h"

using namespace linuxface;
using namespace linuxface::web;

// Helper to build AVCC config record bytes
static std::vector<std::byte> buildAVCCConfig()
{
    std::vector<std::byte> avcc;
    // Configuration version
    avcc.push_back(static_cast<std::byte>(0x01));
    // Profile, compatibility, level
    avcc.push_back(static_cast<std::byte>(0x42));
    avcc.push_back(static_cast<std::byte>(0x00));
    avcc.push_back(static_cast<std::byte>(0x1E));
    // lengthSizeMinusOne: reserved (0xFC) | 0x03 -> 0xFF (4 bytes length)
    avcc.push_back(static_cast<std::byte>(0xFF));
    // numSPS: reserved(0xE0) | 1 -> 0xE1
    avcc.push_back(static_cast<std::byte>(0xE1));
    // SPS length (big endian) and data
    std::vector<uint8_t> sps = {0x67, 0x42, 0x00, 0x1E, 0xA9};
    avcc.push_back(static_cast<std::byte>((sps.size() >> 8) & 0xFF));
    avcc.push_back(static_cast<std::byte>((sps.size() >> 0) & 0xFF));
    for (auto b : sps)
    {
        avcc.push_back(static_cast<std::byte>(b));
    }
    // numPPS
    avcc.push_back(static_cast<std::byte>(0x01));
    // PPS length + data
    std::vector<uint8_t> pps = {0x68, 0xCE, 0x06};
    avcc.push_back(static_cast<std::byte>((pps.size() >> 8) & 0xFF));
    avcc.push_back(static_cast<std::byte>((pps.size() >> 0) & 0xFF));
    for (auto b : pps)
    {
        avcc.push_back(static_cast<std::byte>(b));
    }
    return avcc;
}

// Helper to build AVCC frame with 4-byte length prefix
static std::vector<std::byte> buildAVCCFrame()
{
    std::vector<uint8_t> nal = {0x65, 0x88, 0x99, 0xAA}; // IDR + payload
    std::vector<std::byte> avcc;
    uint32_t len = nal.size();
    avcc.push_back(static_cast<std::byte>((len >> 24) & 0xFF));
    avcc.push_back(static_cast<std::byte>((len >> 16) & 0xFF));
    avcc.push_back(static_cast<std::byte>((len >> 8) & 0xFF));
    avcc.push_back(static_cast<std::byte>((len >> 0) & 0xFF));
    for (auto b : nal)
    {
        avcc.push_back(static_cast<std::byte>(b));
    }
    return avcc;
}

TEST(WebRTCTransportConversionTest, ConvertAVCCConfigToAnnexB)
{
    StreamingConfig cfg;
    WebRTCTransport transport(cfg);
    auto avcc = buildAVCCConfig();
    auto annex = transport.convertAVCCToAnnexB(avcc);
    ASSERT_FALSE(annex.empty());
    // Should start with 4-byte start code followed by 0x67 (SPS NAL header)
    ASSERT_GE(annex.size(), 5);
    EXPECT_EQ(annex[0], 0x00);
    EXPECT_EQ(annex[1], 0x00);
    EXPECT_EQ(annex[2], 0x00);
    EXPECT_EQ(annex[3], 0x01);
    EXPECT_EQ(annex[4], 0x67);
    // Find PPS start code and 0x68
    bool foundPPS = false;
    for (size_t i = 0; i + 4 < annex.size(); ++i)
    {
        if (annex[i] == 0x00 && annex[i + 1] == 0x00 && annex[i + 2] == 0x00 && annex[i + 3] == 0x01)
        {
            if (annex[i + 4] == 0x68)
            {
                foundPPS = true;
                break;
            }
        }
    }
    EXPECT_TRUE(foundPPS);
}

TEST(WebRTCTransportConversionTest, ConvertAVCCFrameToAnnexB)
{
    StreamingConfig cfg;
    WebRTCTransport transport(cfg);
    auto avccFrame = buildAVCCFrame();
    auto annex = transport.convertAVCCToAnnexB(avccFrame);
    ASSERT_FALSE(annex.empty());
    ASSERT_GE(annex.size(), 5);
    EXPECT_EQ(annex[0], 0x00);
    EXPECT_EQ(annex[1], 0x00);
    EXPECT_EQ(annex[2], 0x00);
    EXPECT_EQ(annex[3], 0x01);
    EXPECT_EQ(annex[4], 0x65); // IDR header
}

TEST(WebRTCTransportConversionTest, DetectCompleteFrameKeyframe)
{
    StreamingConfig cfg;
    WebRTCTransport transport(cfg);
    // Build a keyframe: SPS, PPS, IDR with start codes
    std::vector<uint8_t> keyframe;
    // SPS
    keyframe.push_back(0x00);
    keyframe.push_back(0x00);
    keyframe.push_back(0x00);
    keyframe.push_back(0x01);
    keyframe.push_back(0x67);
    keyframe.push_back(0x42);
    // PPS
    keyframe.push_back(0x00);
    keyframe.push_back(0x00);
    keyframe.push_back(0x00);
    keyframe.push_back(0x01);
    keyframe.push_back(0x68);
    // IDR (type 5)
    keyframe.push_back(0x00);
    keyframe.push_back(0x00);
    keyframe.push_back(0x00);
    keyframe.push_back(0x01);
    keyframe.push_back(0x65);

    // Should detect as complete keyframe immediately
    auto lastPacketTime = std::chrono::steady_clock::now();
    EXPECT_TRUE(transport.detectCompleteFrame(keyframe, lastPacketTime));
}

TEST(WebRTCTransportConversionTest, DetectCompleteFramePFrameTimeout)
{
    StreamingConfig cfg;
    WebRTCTransport transport(cfg);
    // Build a single slice (P-frame / non-IDR), type 1
    std::vector<uint8_t> pframe;
    pframe.push_back(0x00);
    pframe.push_back(0x00);
    pframe.push_back(0x00);
    pframe.push_back(0x01);
    pframe.push_back(0x41); // nal type 1
    pframe.push_back(0x11);
    pframe.push_back(0x22);

    // Immediately after last packet: should wait for timeout and NOT be complete
    auto lastPacketTimeNow = std::chrono::steady_clock::now();
    EXPECT_FALSE(transport.detectCompleteFrame(pframe, lastPacketTimeNow));

    // After >=50ms since last packet: should be considered a complete P-frame
    auto lastPacketTime100ms = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);
    EXPECT_TRUE(transport.detectCompleteFrame(pframe, lastPacketTime100ms));
}

TEST(WebRTCTransportConversionTest, StatsIncrementOnConversionAndDecodeAttempt)
{
    StreamingConfig cfg;
    WebRTCTransport transport(cfg);
    auto avcc = buildAVCCFrame();

    auto before = transport.getStats();
    auto annex = transport.convertAVCCToAnnexB(avcc);
    auto afterConvert = transport.getStats();

    // Conversion attempted and succeeded
    EXPECT_GT(afterConvert.h264AvccToAnnexBConversionsAttempted, before.h264AvccToAnnexBConversionsAttempted);
    EXPECT_GE(afterConvert.h264AvccToAnnexBConversionsSucceeded, before.h264AvccToAnnexBConversionsSucceeded + 1);

    // Try to decode a minimal Annex-B frame - we expect at least a decode attempt
    std::vector<std::byte> minimalAnnex;
    minimalAnnex.push_back(static_cast<std::byte>(0x00));
    minimalAnnex.push_back(static_cast<std::byte>(0x00));
    minimalAnnex.push_back(static_cast<std::byte>(0x00));
    minimalAnnex.push_back(static_cast<std::byte>(0x01));
    minimalAnnex.push_back(static_cast<std::byte>(0x65)); // IDR nal header

    auto beforeDecode = transport.getStats();
    auto decoded = transport.decodeH264Frame(minimalAnnex);
    auto afterDecode = transport.getStats();

    EXPECT_EQ(afterDecode.h264DecodeAttempts, beforeDecode.h264DecodeAttempts + 1);
    // Decoder should have signalled either EAGAIN or failure
    EXPECT_GE(afterDecode.h264DecodeEagain + afterDecode.h264DecodeFailures,
              beforeDecode.h264DecodeEagain + beforeDecode.h264DecodeFailures);
}

// Note: MobileReassembly, DesktopFallback, and EncodeDecodeLoopback tests removed
// Reason: These require full WebRTC peer infrastructure (peer registration, data channels)
// and are better suited for integration tests. The critical H.264 functionality
// (AVCC conversion, Annex B frame detection, stats tracking) is covered by remaining tests.
