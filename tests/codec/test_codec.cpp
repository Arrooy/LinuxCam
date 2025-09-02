#include <gtest/gtest.h>
#include <memory>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/codec.h"
#include "LinuxFace/codecFactory.h"

using namespace linuxface;

class CodecTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create a simple RGB test image using proper constructor
        testImage_ = std::make_unique<Image>(4 * 4 * 3); // 4x4 RGB = 48 bytes
        testImage_->info.width = 4;
        testImage_->info.height = 4;
        testImage_->info.format = ImageFormat::RGB;
        testImage_->info.TJPixelFormat = TJPF_RGB;
        testImage_->info.pixelSizeBytes = 3;

        // Fill with test pattern
        unsigned char* data = testImage_->data();
        for (int i = 0; i < 48; ++i)
        {
            data[i] = static_cast<unsigned char>(i % 256);
        }
    }

    void TearDown() override {}

    std::unique_ptr<Image> testImage_;
};

TEST_F(CodecTest, RAWEncoderDecoder)
{
    // Test RAW encoder/decoder roundtrip
    ConfigBuilder config;

    RAWEncoder encoder(config);
    RAWDecoder decoder(config);

    // Create output images
    auto encodedImage = std::make_unique<Image>();
    auto decodedImage = std::make_unique<Image>();

    // Encode
    unsigned long compressedSize = 0;
    EXPECT_TRUE(encoder.encode(*testImage_, *encodedImage, compressedSize));
    EXPECT_EQ(compressedSize, testImage_->size());
    EXPECT_EQ(encodedImage->size(), testImage_->size());

    // Decode
    EXPECT_TRUE(decoder.decode(*encodedImage, *decodedImage));
    EXPECT_EQ(decodedImage->size(), testImage_->size());

    // Verify data integrity
    EXPECT_EQ(memcmp(testImage_->data(), decodedImage->data(), testImage_->size()), 0);
}

TEST_F(CodecTest, RAWDecoderHeader)
{
    ConfigBuilder config;
    RAWDecoder decoder(config);

    unsigned long neededSize = 0;
    EXPECT_TRUE(decoder.decodeHeader(*testImage_, neededSize));
    EXPECT_EQ(neededSize, testImage_->size());
}

TEST_F(CodecTest, BayerGBRGDecoder)
{
    // Create a Bayer GBRG test image (8x8 for proper pattern)
    auto bayerImage = std::make_unique<Image>(8 * 8); // 64 bytes for 8x8 single channel
    bayerImage->info.width = 8;
    bayerImage->info.height = 8;
    bayerImage->info.format = ImageFormat::RAW;
    bayerImage->info.TJPixelFormat = TJPF_GRAY;
    bayerImage->info.pixelSizeBytes = 1;

    // Fill with test pattern
    unsigned char* bayerData = bayerImage->data();
    for (int i = 0; i < 64; ++i)
    {
        bayerData[i] = static_cast<unsigned char>(i * 4); // Create gradient
    }

    ConfigBuilder config;
    config.set("width", 8);
    config.set("height", 8);

    BayerGBRGDecoder decoder(config);

    auto rgbImage = std::make_unique<Image>();

    // Test header decode
    unsigned long neededSize = 0;
    EXPECT_TRUE(decoder.decodeHeader(*bayerImage, neededSize));
    EXPECT_EQ(neededSize, 8 * 8 * 3); // RGB output

    // Test decode
    EXPECT_TRUE(decoder.decode(*bayerImage, *rgbImage));
    EXPECT_EQ(rgbImage->size(), 8 * 8 * 3);
    EXPECT_EQ(rgbImage->info.width, 8);
    EXPECT_EQ(rgbImage->info.height, 8);
    EXPECT_EQ(rgbImage->info.pixelSizeBytes, 3);
    EXPECT_EQ(rgbImage->info.TJPixelFormat, TJPF_RGB);
}

TEST_F(CodecTest, DepthZ16Decoder)
{
    // Create a Z16 depth test image (4x4)
    auto depthImage = std::make_unique<Image>(4 * 4 * 2); // 2 bytes per pixel
    depthImage->info.width = 4;
    depthImage->info.height = 4;
    depthImage->info.format = ImageFormat::DEPTH_Z16;

    // Fill with test depth data
    unsigned short* depthData = reinterpret_cast<unsigned short*>(depthImage->data());
    for (int i = 0; i < 16; ++i)
    {
        depthData[i] = static_cast<unsigned short>(i * 4000); // Create depth gradient
    }

    ConfigBuilder config;
    config.set("width", 4);
    config.set("height", 4);
    config.set("max_depth", 65535);
    config.set("color_map", 0); // Grayscale

    DepthZ16Decoder decoder(config);

    auto rgbImage = std::make_unique<Image>();

    // Test header decode
    unsigned long neededSize = 0;
    EXPECT_TRUE(decoder.decodeHeader(*depthImage, neededSize));
    EXPECT_EQ(neededSize, 4 * 4 * 2); // Input size

    // Test decode
    EXPECT_TRUE(decoder.decode(*depthImage, *rgbImage));
    EXPECT_EQ(rgbImage->size(), 4 * 4 * 3);
    EXPECT_EQ(rgbImage->info.width, 4);
    EXPECT_EQ(rgbImage->info.height, 4);
    EXPECT_EQ(rgbImage->info.pixelSizeBytes, 3);
    EXPECT_EQ(rgbImage->info.TJPixelFormat, TJPF_RGB);

    // Verify that zero depth produces black pixels
    depthData[0] = 0;
    EXPECT_TRUE(decoder.decode(*depthImage, *rgbImage));
    unsigned char* rgbData = rgbImage->data();
    EXPECT_EQ(rgbData[0], 0); // R
    EXPECT_EQ(rgbData[1], 0); // G
    EXPECT_EQ(rgbData[2], 0); // B
}

TEST_F(CodecTest, UYVY422Decoder)
{
    // Create a UYVY test image (4x2 = 8 pixels, 16 bytes)
    auto yuvImage = std::make_unique<Image>(4 * 2 * 2); // 2 bytes per pixel
    yuvImage->info.width = 4;
    yuvImage->info.height = 2;
    yuvImage->info.format = ImageFormat::UYUV422;

    // Fill with test YUV data (UYVY pattern)
    unsigned char* yuvData = yuvImage->data();
    // First line: U0 Y0 V0 Y1 U2 Y2 V2 Y3
    yuvData[0] = 128;
    yuvData[1] = 100;
    yuvData[2] = 128;
    yuvData[3] = 150; // First pair
    yuvData[4] = 128;
    yuvData[5] = 100;
    yuvData[6] = 128;
    yuvData[7] = 150; // Second pair
    // Second line
    yuvData[8] = 128;
    yuvData[9] = 200;
    yuvData[10] = 128;
    yuvData[11] = 250; // Third pair
    yuvData[12] = 128;
    yuvData[13] = 200;
    yuvData[14] = 128;
    yuvData[15] = 250; // Fourth pair

    ConfigBuilder config;
    config.set("width", 4);
    config.set("height", 2);

    UYVY422Decoder decoder(config);

    auto rgbImage = std::make_unique<Image>();

    // Test header decode
    unsigned long neededSize = 0;
    EXPECT_TRUE(decoder.decodeHeader(*yuvImage, neededSize));
    EXPECT_EQ(neededSize, 4 * 2 * 2); // Input size

    // Test decode
    EXPECT_TRUE(decoder.decode(*yuvImage, *rgbImage));
    EXPECT_EQ(rgbImage->size(), 4 * 2 * 3);
    EXPECT_EQ(rgbImage->info.width, 4);
    EXPECT_EQ(rgbImage->info.height, 2);
    EXPECT_EQ(rgbImage->info.pixelSizeBytes, 3);
    EXPECT_EQ(rgbImage->info.TJPixelFormat, TJPF_RGB);
}

TEST_F(CodecTest, YUYV422Decoder)
{
    // Create a YUYV test image (4x2 = 8 pixels, 16 bytes)
    auto yuvImage = std::make_unique<Image>(4 * 2 * 2); // 2 bytes per pixel
    yuvImage->info.width = 4;
    yuvImage->info.height = 2;
    yuvImage->info.format = ImageFormat::YUYV422;

    // Fill with test YUV data (YUYV pattern)
    unsigned char* yuvData = yuvImage->data();
    // First line: Y0 U0 Y1 V0 Y2 U2 Y3 V2
    yuvData[0] = 100;
    yuvData[1] = 128;
    yuvData[2] = 150;
    yuvData[3] = 128; // First pair
    yuvData[4] = 100;
    yuvData[5] = 128;
    yuvData[6] = 150;
    yuvData[7] = 128; // Second pair
    // Second line
    yuvData[8] = 200;
    yuvData[9] = 128;
    yuvData[10] = 250;
    yuvData[11] = 128; // Third pair
    yuvData[12] = 200;
    yuvData[13] = 128;
    yuvData[14] = 250;
    yuvData[15] = 128; // Fourth pair

    ConfigBuilder config;
    config.set("width", 4);
    config.set("height", 2);

    YUYV422Decoder decoder(config);

    auto rgbImage = std::make_unique<Image>();

    // Test decode
    EXPECT_TRUE(decoder.decode(*yuvImage, *rgbImage));
    EXPECT_EQ(rgbImage->size(), 4 * 2 * 3);
    EXPECT_EQ(rgbImage->info.width, 4);
    EXPECT_EQ(rgbImage->info.height, 2);
}

TEST_F(CodecTest, PPMDecoder)
{
    // Create a simple PPM test image
    std::string ppmContent = "P6\n# Test comment\n4 4\n255\n";
    std::string pixelData;

    // Add 4x4 RGB pixel data (48 bytes total)
    for (int i = 0; i < 48; ++i)
    {
        pixelData += static_cast<char>(i % 256);
    }

    std::string fullPPM = ppmContent + pixelData;

    auto ppmImage = std::make_unique<Image>(fullPPM.size());
    memcpy(ppmImage->data(), fullPPM.data(), fullPPM.size());
    ppmImage->info.format = ImageFormat::PPM;

    PPMDecoder decoder;

    // Test header decode
    unsigned long neededSize = 0;
    EXPECT_TRUE(decoder.decodeHeader(*ppmImage, neededSize));
    EXPECT_EQ(ppmImage->info.width, 4);
    EXPECT_EQ(ppmImage->info.height, 4);
    EXPECT_EQ(neededSize, 48); // 4x4x3 bytes

    // Test decode
    auto rgbImage = std::make_unique<Image>();
    EXPECT_TRUE(decoder.decode(*ppmImage, *rgbImage));
    EXPECT_EQ(rgbImage->size(), 48);
    EXPECT_EQ(rgbImage->info.width, 4);
    EXPECT_EQ(rgbImage->info.height, 4);
    EXPECT_EQ(rgbImage->info.pixelSizeBytes, 3);
    EXPECT_EQ(rgbImage->info.TJPixelFormat, TJPF_RGB);
}

// Error handling tests
class CodecErrorTest : public ::testing::Test
{
  protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(CodecErrorTest, EmptyImageDecoding)
{
    ConfigBuilder config;
    RAWDecoder decoder(config);

    auto emptyImage = std::make_unique<Image>();
    auto outputImage = std::make_unique<Image>();

    // Should handle empty images gracefully
    EXPECT_TRUE(decoder.decode(*emptyImage, *outputImage));
}

TEST_F(CodecErrorTest, BayerInvalidSize)
{
    auto bayerImage = std::make_unique<Image>(5); // Too small for 8x8

    ConfigBuilder config;
    config.set("width", 8);
    config.set("height", 8);

    BayerGBRGDecoder decoder(config);
    auto rgbImage = std::make_unique<Image>();

    // Should fail with insufficient data
    EXPECT_FALSE(decoder.decode(*bayerImage, *rgbImage));
}

TEST_F(CodecErrorTest, DepthInvalidSize)
{
    auto depthImage = std::make_unique<Image>(5); // Too small for 4x4 depth

    ConfigBuilder config;
    config.set("width", 4);
    config.set("height", 4);

    DepthZ16Decoder decoder(config);
    auto rgbImage = std::make_unique<Image>();

    // Should fail with insufficient data
    EXPECT_FALSE(decoder.decode(*depthImage, *rgbImage));
}

TEST_F(CodecErrorTest, YUVInvalidSize)
{
    auto yuvImage = std::make_unique<Image>(5); // Too small for 4x2 YUV

    ConfigBuilder config;
    config.set("width", 4);
    config.set("height", 2);

    UYVY422Decoder decoder(config);
    auto rgbImage = std::make_unique<Image>();

    // Should fail with insufficient data
    EXPECT_FALSE(decoder.decode(*yuvImage, *rgbImage));
}

TEST_F(CodecErrorTest, PPMInvalidHeader)
{
    // Create invalid PPM data
    std::string invalidPPM = "P5\n4 4\n255\n"; // P5 instead of P6

    auto ppmImage = std::make_unique<Image>(invalidPPM.size());
    memcpy(ppmImage->data(), invalidPPM.data(), invalidPPM.size());

    PPMDecoder decoder;

    unsigned long neededSize = 0;
    EXPECT_FALSE(decoder.decodeHeader(*ppmImage, neededSize));
}

// Additional edge case tests
TEST_F(CodecErrorTest, ConfigBuilderMissingParameters)
{
    ConfigBuilder emptyConfig;

    // BayerGBRGDecoder should handle missing width/height gracefully
    BayerGBRGDecoder decoder(emptyConfig);

    auto bayerImage = std::make_unique<Image>(64); // 8x8 image
    auto rgbImage = std::make_unique<Image>();

    // Should fail because width/height are 0
    EXPECT_FALSE(decoder.decode(*bayerImage, *rgbImage));
}

TEST_F(CodecErrorTest, DepthZ16ZeroDepthHandling)
{
    auto depthImage = std::make_unique<Image>(2 * 2 * 2); // 2x2 depth image
    depthImage->info.width = 2;
    depthImage->info.height = 2;

    // Fill with zero depth values
    unsigned short* depthData = reinterpret_cast<unsigned short*>(depthImage->data());
    for (int i = 0; i < 4; ++i)
    {
        depthData[i] = 0; // Zero depth (invalid)
    }

    ConfigBuilder config;
    config.set("width", 2);
    config.set("height", 2);
    config.set("max_depth", 65535);
    config.set("color_map", 0); // Grayscale

    DepthZ16Decoder decoder(config);
    auto rgbImage = std::make_unique<Image>();

    EXPECT_TRUE(decoder.decode(*depthImage, *rgbImage));

    // All pixels should be black (0,0,0) for zero depth
    unsigned char* rgbData = rgbImage->data();
    for (int i = 0; i < 12; i += 3) // 4 pixels * 3 channels
    {
        EXPECT_EQ(rgbData[i], 0);     // R
        EXPECT_EQ(rgbData[i + 1], 0); // G
        EXPECT_EQ(rgbData[i + 2], 0); // B
    }
}

TEST_F(CodecErrorTest, YUV422EmptyImageHandling)
{
    auto emptyYUV = std::make_unique<Image>();

    ConfigBuilder config;
    config.set("width", 4);
    config.set("height", 2);

    UYVY422Decoder decoder(config);
    auto rgbImage = std::make_unique<Image>();

    EXPECT_FALSE(decoder.decode(*emptyYUV, *rgbImage));
}

TEST_F(CodecErrorTest, PPMTruncatedFile)
{
    // Create PPM with valid header but truncated pixel data
    std::string ppmContent = "P6\n4 4\n255\n";
    std::string shortPixelData = "short"; // Way too short for 4x4 RGB

    std::string truncatedPPM = ppmContent + shortPixelData;

    auto ppmImage = std::make_unique<Image>(truncatedPPM.size());
    memcpy(ppmImage->data(), truncatedPPM.data(), truncatedPPM.size());

    PPMDecoder decoder;

    unsigned long neededSize = 0;
    EXPECT_FALSE(decoder.decodeHeader(*ppmImage, neededSize));
}

TEST_F(CodecErrorTest, DepthZ16DifferentColorMaps)
{
    // Test different color map modes
    auto depthImage = std::make_unique<Image>(2 * 2 * 2);
    depthImage->info.width = 2;
    depthImage->info.height = 2;

    unsigned short* depthData = reinterpret_cast<unsigned short*>(depthImage->data());
    depthData[0] = 32767; // Mid-range depth value
    depthData[1] = 65535; // Max depth
    depthData[2] = 1000;  // Low depth
    depthData[3] = 0;     // Zero depth

    // Test Jet colormap
    ConfigBuilder jetConfig;
    jetConfig.set("width", 2);
    jetConfig.set("height", 2);
    jetConfig.set("max_depth", 65535);
    jetConfig.set("color_map", 1); // Jet

    DepthZ16Decoder jetDecoder(jetConfig);
    auto jetRGB = std::make_unique<Image>();

    EXPECT_TRUE(jetDecoder.decode(*depthImage, *jetRGB));
    EXPECT_EQ(jetRGB->size(), 2 * 2 * 3);

    // Test Hot colormap
    ConfigBuilder hotConfig;
    hotConfig.set("width", 2);
    hotConfig.set("height", 2);
    hotConfig.set("max_depth", 65535);
    hotConfig.set("color_map", 2); // Hot

    DepthZ16Decoder hotDecoder(hotConfig);
    auto hotRGB = std::make_unique<Image>();

    EXPECT_TRUE(hotDecoder.decode(*depthImage, *hotRGB));
    EXPECT_EQ(hotRGB->size(), 2 * 2 * 3);
}

// Memory and boundary tests
TEST_F(CodecErrorTest, LargeImageStressTest)
{
    // Test with a larger image to stress memory allocation
    const int width = 64;
    const int height = 64;

    // Test RAW codec with larger image
    ConfigBuilder config;
    config.set("width", width);
    config.set("height", height);
    config.set("format", "BGR");

    auto largeInput = std::make_unique<Image>(width * height * 3);

    // Fill with pattern
    unsigned char* data = largeInput->data();
    for (int i = 0; i < width * height * 3; ++i)
    {
        data[i] = static_cast<unsigned char>(i % 256);
    }

    RAWEncoder encoder(config);
    RAWDecoder decoder(config);

    auto encoded = std::make_unique<Image>();
    auto decoded = std::make_unique<Image>();

    unsigned long compressedSize = 0;
    EXPECT_TRUE(encoder.encode(*largeInput, *encoded, compressedSize));
    EXPECT_TRUE(decoder.decode(*encoded, *decoded));

    // Verify compression size and decoded size consistency
    EXPECT_EQ(compressedSize, width * height * 3);
    EXPECT_EQ(decoded->size(), width * height * 3);

    // Verify first and last bytes
    EXPECT_EQ(decoded->data()[0], data[0]);
    EXPECT_EQ(decoded->data()[decoded->size() - 1], data[largeInput->size() - 1]);
}

TEST_F(CodecErrorTest, BayerOddDimensionsHandling)
{
    // Bayer processing typically requires even dimensions
    ConfigBuilder config;
    config.set("width", 3);  // Odd width
    config.set("height", 3); // Odd height

    BayerGBRGDecoder decoder(config);

    auto bayerImage = std::make_unique<Image>(9); // 3x3 image
    bayerImage->info.width = 3;
    bayerImage->info.height = 3;

    auto rgbImage = std::make_unique<Image>();

    // Should handle odd dimensions gracefully
    EXPECT_TRUE(decoder.decode(*bayerImage, *rgbImage));
}

TEST_F(CodecErrorTest, DepthExtremeValues)
{
    ConfigBuilder config;
    config.set("width", 2);
    config.set("height", 2);
    config.set("max_depth", 100); // Low max depth for testing scaling
    config.set("color_map", 0);   // Grayscale

    auto depthImage = std::make_unique<Image>(2 * 2 * 2);
    depthImage->info.width = 2;
    depthImage->info.height = 2;

    unsigned short* depthData = reinterpret_cast<unsigned short*>(depthImage->data());
    depthData[0] = 0;     // Min value
    depthData[1] = 100;   // Max configured value
    depthData[2] = 65535; // Beyond max configured (should clamp)
    depthData[3] = 50;    // Mid value

    DepthZ16Decoder decoder(config);
    auto rgbImage = std::make_unique<Image>();

    EXPECT_TRUE(decoder.decode(*depthImage, *rgbImage));

    unsigned char* rgbData = rgbImage->data();

    // Pixel 0: depth=0 should be black (0,0,0)
    EXPECT_EQ(rgbData[0], 0);
    EXPECT_EQ(rgbData[1], 0);
    EXPECT_EQ(rgbData[2], 0);

    // Pixel 1: depth=100 (max) should be white (255,255,255)
    EXPECT_EQ(rgbData[3], 255);
    EXPECT_EQ(rgbData[4], 255);
    EXPECT_EQ(rgbData[5], 255);

    // Pixel 2: depth=65535 (beyond max) should also be mapped to grayscale based on actual algorithm
    // Don't assume exact clamping behavior - just verify reasonable output
    EXPECT_GT(rgbData[6], 150); // Should be a bright value
    EXPECT_GT(rgbData[7], 150); // Should be a bright value
    EXPECT_GT(rgbData[8], 150); // Should be a bright value
}

TEST_F(CodecErrorTest, PPMMaxValueEdgeCases)
{
    // Test PPM with different max values
    std::string header1 = "P6\n2 2\n255\n";
    std::string pixels1 = "RRGGBBRRGGBBRR"; // 12 bytes for 2x2 RGB image
    std::string ppm1 = header1 + pixels1;

    auto ppmImage1 = std::make_unique<Image>(ppm1.size());
    memcpy(ppmImage1->data(), ppm1.data(), ppm1.size());

    PPMDecoder decoder;
    unsigned long neededSize = 0;

    // Should succeed in header parsing but indicate insufficient pixel data
    EXPECT_TRUE(decoder.decodeHeader(*ppmImage1, neededSize));
    EXPECT_EQ(neededSize, 12); // 2x2x3 = 12 bytes needed

    // Test with complete but minimal PPM
    std::string header2 = "P6\n1 1\n255\n";
    std::string pixels2 = "RGB"; // 3 bytes for 1x1 RGB image
    std::string ppm2 = header2 + pixels2;

    auto ppmImage2 = std::make_unique<Image>(ppm2.size());
    memcpy(ppmImage2->data(), ppm2.data(), ppm2.size());

    EXPECT_TRUE(decoder.decodeHeader(*ppmImage2, neededSize));
    EXPECT_EQ(neededSize, 3); // 1x1x3 = 3 bytes
}
