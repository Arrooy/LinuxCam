#include <gtest/gtest.h>
#include "LinuxFace/codec.h"
#include "LinuxFace/codecFactory.h"

using namespace linuxface;

class H264CodecTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        width_ = 64;
        height_ = 48;
    }

    unsigned int width_;
    unsigned int height_;
};

TEST_F(H264CodecTest, EncoderInitialization)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::H264)
          .width(width_)
          .height(height_);

    auto encoder = CodecFactory::create<Encoder>(config);
    ASSERT_NE(encoder, nullptr);
}

TEST_F(H264CodecTest, DecoderInitialization)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::H264);

    auto decoder = CodecFactory::create<Decoder>(config);
    ASSERT_NE(decoder, nullptr);
}

TEST_F(H264CodecTest, EncodeSingleFrame)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::H264)
          .width(width_)
          .height(height_);

    auto encoder = CodecFactory::create<Encoder>(config);
    ASSERT_NE(encoder, nullptr);

    // Create RGB test frame
    Image srcImage(width_ * height_ * 3);
    srcImage.info.width = width_;
    srcImage.info.height = height_;
    srcImage.info.pixelSizeBytes = 3;
    srcImage.info.format = ImageFormat::RGB;
    srcImage.info.TJPixelFormat = TJPF_RGB;

    // Fill with gradient
    for (unsigned int y = 0; y < height_; ++y)
    {
        for (unsigned int x = 0; x < width_; ++x)
        {
            size_t idx = (y * width_ + x) * 3;
            srcImage.data()[idx + 0] = static_cast<uint8_t>((x * 255) / width_);
            srcImage.data()[idx + 1] = static_cast<uint8_t>((y * 255) / height_);
            srcImage.data()[idx + 2] = static_cast<uint8_t>((x + y) % 256);
        }
    }

    Image outImage(width_ * height_ * 2);
    unsigned long compressedSize = 0;

    ASSERT_TRUE(encoder->encode(srcImage, outImage, compressedSize));
    EXPECT_GT(compressedSize, 0);
    EXPECT_EQ(outImage.info.format, ImageFormat::H264);
}

TEST_F(H264CodecTest, EncodeDecodeLoopback)
{
    // Setup encoder
    ConfigBuilder encoderConfig;
    encoderConfig.imageFormat(ImageFormat::H264)
                 .width(width_)
                 .height(height_);

    auto encoder = CodecFactory::create<Encoder>(encoderConfig);
    ASSERT_NE(encoder, nullptr);

    // Setup decoder
    ConfigBuilder decoderConfig;
    decoderConfig.imageFormat(ImageFormat::H264);

    auto decoder = CodecFactory::create<Decoder>(decoderConfig);
    ASSERT_NE(decoder, nullptr);

    // Create source RGB frame
    Image srcImage(width_ * height_ * 3);
    srcImage.info.width = width_;
    srcImage.info.height = height_;
    srcImage.info.pixelSizeBytes = 3;
    srcImage.info.format = ImageFormat::RGB;
    srcImage.info.TJPixelFormat = TJPF_RGB;

    // Fill with gradient pattern
    for (unsigned int y = 0; y < height_; ++y)
    {
        for (unsigned int x = 0; x < width_; ++x)
        {
            size_t idx = (y * width_ + x) * 3;
            srcImage.data()[idx + 0] = static_cast<uint8_t>((x * 255) / width_);
            srcImage.data()[idx + 1] = static_cast<uint8_t>((y * 255) / height_);
            srcImage.data()[idx + 2] = static_cast<uint8_t>((x + y) % 256);
        }
    }

    // Encode
    Image encodedImage(width_ * height_ * 2);
    unsigned long compressedSize = 0;
    ASSERT_TRUE(encoder->encode(srcImage, encodedImage, compressedSize));
    ASSERT_GT(compressedSize, 0);

    // Decode
    Image decodedImage;
    ASSERT_TRUE(decoder->decode(encodedImage, decodedImage));
    EXPECT_EQ(decodedImage.info.width, width_);
    EXPECT_EQ(decodedImage.info.height, height_);
    EXPECT_EQ(decodedImage.info.pixelSizeBytes, 3);
    EXPECT_EQ(decodedImage.info.format, ImageFormat::RGB);
}

TEST_F(H264CodecTest, MultipleFramesEncode)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::H264)
          .width(width_)
          .height(height_);

    auto encoder = CodecFactory::create<Encoder>(config);
    ASSERT_NE(encoder, nullptr);

    // Encode multiple frames
    const int numFrames = 5;
    for (int frame = 0; frame < numFrames; ++frame)
    {
        Image srcImage(width_ * height_ * 3);
        srcImage.info.width = width_;
        srcImage.info.height = height_;
        srcImage.info.pixelSizeBytes = 3;
        srcImage.info.format = ImageFormat::RGB;
        srcImage.info.TJPixelFormat = TJPF_RGB;

        // Fill with different color per frame
        uint8_t color = static_cast<uint8_t>(frame * 50);
        std::fill(srcImage.data(), srcImage.data() + srcImage.size(), color);

        Image outImage(width_ * height_ * 2);
        unsigned long compressedSize = 0;

        ASSERT_TRUE(encoder->encode(srcImage, outImage, compressedSize));
        EXPECT_GT(compressedSize, 0);
    }
}

TEST_F(H264CodecTest, EncoderRejectsBadDimensions)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::H264)
          .width(width_)
          .height(height_);

    auto encoder = CodecFactory::create<Encoder>(config);
    ASSERT_NE(encoder, nullptr);

    // Create image with wrong dimensions
    Image srcImage((width_ + 10) * height_ * 3);
    srcImage.info.width = width_ + 10;
    srcImage.info.height = height_;
    srcImage.info.pixelSizeBytes = 3;
    srcImage.info.format = ImageFormat::RGB;
    srcImage.info.TJPixelFormat = TJPF_RGB;

    Image outImage(width_ * height_ * 2);
    unsigned long compressedSize = 0;

    EXPECT_FALSE(encoder->encode(srcImage, outImage, compressedSize));
}

TEST_F(H264CodecTest, EncoderRejectsBadPixelFormat)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::H264)
          .width(width_)
          .height(height_);

    auto encoder = CodecFactory::create<Encoder>(config);
    ASSERT_NE(encoder, nullptr);

    // Create image with wrong pixel size (RGBA instead of RGB)
    Image srcImage(width_ * height_ * 4);
    srcImage.info.width = width_;
    srcImage.info.height = height_;
    srcImage.info.pixelSizeBytes = 4;
    srcImage.info.format = ImageFormat::RGBA;

    Image outImage(width_ * height_ * 2);
    unsigned long compressedSize = 0;

    EXPECT_FALSE(encoder->encode(srcImage, outImage, compressedSize));
}
