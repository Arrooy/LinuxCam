#include <gtest/gtest.h>

#include <memory>
#include <thread>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/codec.h"
#include "LinuxFace/codecFactory.h"
#include "LinuxFace/common.h"

using namespace linuxface;

// Test fixture for CodecFactory tests
class CodecFactoryTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Initialize common test data
        testWidth = 640;
        testHeight = 480;
        testImageSize = testWidth * testHeight * 3; // RGB

        // Create test image data
        testImageData = std::make_unique<unsigned char[]>(testImageSize);
        for (size_t i = 0; i < testImageSize; i += 3)
        {
            testImageData[i] = static_cast<unsigned char>(i % 256);           // R
            testImageData[i + 1] = static_cast<unsigned char>((i + 1) % 256); // G
            testImageData[i + 2] = static_cast<unsigned char>((i + 2) % 256); // B
        }
    }

    void TearDown() override
    {
        // Clean up is automatic with smart pointers
    }

    unsigned int testWidth;
    unsigned int testHeight;
    size_t testImageSize;
    std::unique_ptr<unsigned char[]> testImageData;
};

// Test ConfigBuilder functionality
TEST_F(CodecFactoryTest, ConfigBuilderBasicProperties)
{
    ConfigBuilder config;

    // Test method chaining
    config.width(testWidth)
        .height(testHeight)
        .quality(85)
        .imageFormat(ImageFormat::JPEG)
        .pixelFormat(TJPF_RGB)
        .chrominance_subsampling(TJSAMP_420);

    // Test property retrieval (note: width/height are stored as int, not unsigned int)
    int width, height;
    int quality;
    ImageFormat format;
    TJPF pixelFormat;
    TJSAMP subsampling;

    EXPECT_TRUE(config.get("width", width));
    EXPECT_TRUE(config.get("height", height));
    EXPECT_TRUE(config.get("quality", quality));
    EXPECT_TRUE(config.get("imageFormat", format));
    EXPECT_TRUE(config.get("pixelFormat", pixelFormat));
    EXPECT_TRUE(config.get("chrominance_subsampling", subsampling));

    EXPECT_EQ(width, static_cast<int>(testWidth));
    EXPECT_EQ(height, static_cast<int>(testHeight));
    EXPECT_EQ(quality, 85);
    EXPECT_EQ(format, ImageFormat::JPEG);
    EXPECT_EQ(pixelFormat, TJPF_RGB);
    EXPECT_EQ(subsampling, TJSAMP_420);
}

TEST_F(CodecFactoryTest, ConfigBuilderGenericProperties)
{
    ConfigBuilder config;

    // Test generic property setter
    config.set("custom_int", 42).set("custom_string", std::string("test")).set("custom_bool", true);

    int intVal;
    std::string stringVal;
    bool boolVal;

    EXPECT_TRUE(config.get("custom_int", intVal));
    EXPECT_TRUE(config.get("custom_string", stringVal));
    EXPECT_TRUE(config.get("custom_bool", boolVal));

    EXPECT_EQ(intVal, 42);
    EXPECT_EQ(stringVal, "test");
    EXPECT_TRUE(boolVal);
}

TEST_F(CodecFactoryTest, ConfigBuilderMissingProperty)
{
    ConfigBuilder config;

    int missingValue;
    EXPECT_FALSE(config.get("nonexistent", missingValue));

    // Test has() method
    EXPECT_FALSE(config.has("nonexistent"));

    config.set("existing", 123);
    EXPECT_TRUE(config.has("existing"));
}

TEST_F(CodecFactoryTest, ConfigBuilderTypeMismatch)
{
    ConfigBuilder config;

    config.set("test_value", 42);

    // Try to get as wrong type
    std::string wrongType;
    EXPECT_FALSE(config.get("test_value", wrongType));
}

// Test Decoder creation
TEST_F(CodecFactoryTest, CreateJPEGDecoder)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::JPEG);

    auto decoder = CodecFactory::create<Decoder>(config);
    EXPECT_NE(decoder, nullptr);

    // Test that it's actually a JPEGDecoder by checking type
    JPEGDecoder* jpegDecoder = dynamic_cast<JPEGDecoder*>(decoder.get());
    EXPECT_NE(jpegDecoder, nullptr);
}

TEST_F(CodecFactoryTest, CreateRAWDecoder)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::RAW).width(testWidth).height(testHeight);

    auto decoder = CodecFactory::create<Decoder>(config);
    EXPECT_NE(decoder, nullptr);

    RAWDecoder* rawDecoder = dynamic_cast<RAWDecoder*>(decoder.get());
    EXPECT_NE(rawDecoder, nullptr);
}

TEST_F(CodecFactoryTest, CreateBayerGBRGDecoder)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::SGBRG8).width(testWidth).height(testHeight);

    auto decoder = CodecFactory::create<Decoder>(config);
    EXPECT_NE(decoder, nullptr);

    BayerGBRGDecoder* bayerDecoder = dynamic_cast<BayerGBRGDecoder*>(decoder.get());
    EXPECT_NE(bayerDecoder, nullptr);
}

TEST_F(CodecFactoryTest, CreateDepthZ16Decoder)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::DEPTH_Z16).width(testWidth).height(testHeight);

    auto decoder = CodecFactory::create<Decoder>(config);
    EXPECT_NE(decoder, nullptr);

    DepthZ16Decoder* depthDecoder = dynamic_cast<DepthZ16Decoder*>(decoder.get());
    EXPECT_NE(depthDecoder, nullptr);
}

TEST_F(CodecFactoryTest, CreateUYVY422Decoder)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::UYUV422).width(testWidth).height(testHeight);

    auto decoder = CodecFactory::create<Decoder>(config);
    EXPECT_NE(decoder, nullptr);

    UYVY422Decoder* yuvDecoder = dynamic_cast<UYVY422Decoder*>(decoder.get());
    EXPECT_NE(yuvDecoder, nullptr);
}

TEST_F(CodecFactoryTest, CreateYUYV422Decoder)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::YUYV422).width(testWidth).height(testHeight);

    auto decoder = CodecFactory::create<Decoder>(config);
    EXPECT_NE(decoder, nullptr);

    YUYV422Decoder* yuvDecoder = dynamic_cast<YUYV422Decoder*>(decoder.get());
    EXPECT_NE(yuvDecoder, nullptr);
}

// Test Encoder creation
TEST_F(CodecFactoryTest, CreateJPEGEncoder)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::JPEG)
        .width(testWidth)
        .height(testHeight)
        .quality(85)
        .pixelFormat(TJPF_RGB)
        .chrominance_subsampling(TJSAMP_420);

    auto encoder = CodecFactory::create<Encoder>(config);
    EXPECT_NE(encoder, nullptr);

    JPEGEncoder* jpegEncoder = dynamic_cast<JPEGEncoder*>(encoder.get());
    EXPECT_NE(jpegEncoder, nullptr);
}

TEST_F(CodecFactoryTest, CreateRAWEncoder)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::RAW);

    auto encoder = CodecFactory::create<Encoder>(config);
    EXPECT_NE(encoder, nullptr);

    RAWEncoder* rawEncoder = dynamic_cast<RAWEncoder*>(encoder.get());
    EXPECT_NE(rawEncoder, nullptr);
}

// Test error cases
TEST_F(CodecFactoryTest, CreateDecoderWithoutImageFormat)
{
    ConfigBuilder config;
    // Missing imageFormat

    auto decoder = CodecFactory::create<Decoder>(config);
    EXPECT_EQ(decoder, nullptr);
}

TEST_F(CodecFactoryTest, CreateEncoderWithoutImageFormat)
{
    ConfigBuilder config;
    // Missing imageFormat

    auto encoder = CodecFactory::create<Encoder>(config);
    EXPECT_EQ(encoder, nullptr);
}

TEST_F(CodecFactoryTest, CreateDecoderWithUnknownFormat)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::UNKNOWN);

    auto decoder = CodecFactory::create<Decoder>(config);
    EXPECT_EQ(decoder, nullptr);
}

TEST_F(CodecFactoryTest, CreateEncoderWithUnknownFormat)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::UNKNOWN);

    auto encoder = CodecFactory::create<Encoder>(config);
    EXPECT_EQ(encoder, nullptr);
}

TEST_F(CodecFactoryTest, CreateDecoderWithUnsupportedFormat)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::PNG); // Not supported by factory yet

    auto decoder = CodecFactory::create<Decoder>(config);
    EXPECT_EQ(decoder, nullptr);
}

TEST_F(CodecFactoryTest, CreateEncoderWithUnsupportedFormat)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::PNG); // Not supported by factory yet

    auto encoder = CodecFactory::create<Encoder>(config);
    EXPECT_EQ(encoder, nullptr);
}

// Test functional encoding/decoding with created codecs
TEST_F(CodecFactoryTest, JPEGEncodeDecodeRoundTrip)
{
    // Create test image
    Image srcImage(testImageSize);
    srcImage.info.width = testWidth;
    srcImage.info.height = testHeight;
    srcImage.info.pixelSizeBytes = 3;
    srcImage.info.format = ImageFormat::RGB;
    srcImage.info.TJPixelFormat = TJPF_RGB;
    std::memcpy(srcImage.data(), testImageData.get(), testImageSize);

    // Create encoder
    ConfigBuilder encoderConfig;
    encoderConfig.imageFormat(ImageFormat::JPEG)
        .width(testWidth)
        .height(testHeight)
        .quality(90)
        .pixelFormat(TJPF_RGB)
        .chrominance_subsampling(TJSAMP_420);

    auto encoder = CodecFactory::create<Encoder>(encoderConfig);
    ASSERT_NE(encoder, nullptr);

    // Encode image
    size_t maxCompressedSize = encoder->encodeSizeInBytes();
    Image compressedImage(maxCompressedSize);
    unsigned long actualCompressedSize;

    bool encodeResult = encoder->encode(srcImage, compressedImage, actualCompressedSize);
    EXPECT_TRUE(encodeResult);
    EXPECT_GT(actualCompressedSize, 0);
    EXPECT_LE(actualCompressedSize, maxCompressedSize);

    // Create decoder
    ConfigBuilder decoderConfig;
    decoderConfig.imageFormat(ImageFormat::JPEG);

    auto decoder = CodecFactory::create<Decoder>(decoderConfig);
    ASSERT_NE(decoder, nullptr);

    // Prepare compressed image for decoding
    compressedImage.info.width = testWidth;
    compressedImage.info.height = testHeight;
    compressedImage.info.TJPixelFormat = TJPF_RGB;
    compressedImage.resize(actualCompressedSize);

    // Decode header
    unsigned long rawSize;
    bool headerResult = decoder->decodeHeader(compressedImage, rawSize);
    EXPECT_TRUE(headerResult);

    // Decode image
    Image decodedImage(rawSize);
    bool decodeResult = decoder->decode(compressedImage, decodedImage);
    EXPECT_TRUE(decodeResult);

    // Check decoded image properties
    EXPECT_EQ(decodedImage.info.width, testWidth);
    EXPECT_EQ(decodedImage.info.height, testHeight);
    EXPECT_EQ(decodedImage.info.pixelSizeBytes, 3);
}

TEST_F(CodecFactoryTest, RAWEncodeDecodeRoundTrip)
{
    // Create test image
    Image srcImage(testImageSize);
    srcImage.info.width = testWidth;
    srcImage.info.height = testHeight;
    srcImage.info.pixelSizeBytes = 3;
    srcImage.info.format = ImageFormat::RGB;
    std::memcpy(srcImage.data(), testImageData.get(), testImageSize);

    // Create encoder
    ConfigBuilder encoderConfig;
    encoderConfig.imageFormat(ImageFormat::RAW);

    auto encoder = CodecFactory::create<Encoder>(encoderConfig);
    ASSERT_NE(encoder, nullptr);

    // Encode (RAW just copies data)
    Image encodedImage(testImageSize);
    unsigned long compressedSize;
    bool encodeResult = encoder->encode(srcImage, encodedImage, compressedSize);
    EXPECT_TRUE(encodeResult);
    EXPECT_EQ(compressedSize, testImageSize);

    // Create decoder
    ConfigBuilder decoderConfig;
    decoderConfig.imageFormat(ImageFormat::RAW).width(testWidth).height(testHeight);

    auto decoder = CodecFactory::create<Decoder>(decoderConfig);
    ASSERT_NE(decoder, nullptr);

    // Decode header
    unsigned long rawSize;
    bool headerResult = decoder->decodeHeader(encodedImage, rawSize);
    EXPECT_TRUE(headerResult);

    // Decode image
    Image decodedImage(rawSize);
    bool decodeResult = decoder->decode(encodedImage, decodedImage);
    EXPECT_TRUE(decodeResult);

    // RAW codec should preserve data exactly
    EXPECT_EQ(decodedImage.size(), srcImage.size());
    EXPECT_EQ(std::memcmp(decodedImage.data(), srcImage.data(), testImageSize), 0);
}

// Test edge cases and additional validation
TEST_F(CodecFactoryTest, ConfigBuilderEdgeCases)
{
    ConfigBuilder config;

    // Test setting zero/negative dimensions
    config.width(0).height(0);
    int width, height;
    EXPECT_TRUE(config.get("width", width));
    EXPECT_TRUE(config.get("height", height));
    EXPECT_EQ(width, 0);
    EXPECT_EQ(height, 0);

    // Test quality bounds
    config.quality(-1);
    int quality;
    EXPECT_TRUE(config.get("quality", quality));
    EXPECT_EQ(quality, -1);

    config.quality(200);
    EXPECT_TRUE(config.get("quality", quality));
    EXPECT_EQ(quality, 200);
}

TEST_F(CodecFactoryTest, ConfigBuilderCopyConstructor)
{
    ConfigBuilder config1;
    config1.width(testWidth).height(testHeight).quality(85);

    // Test that configuration is properly copied
    ConfigBuilder config2 = config1;

    int width, height, quality;
    EXPECT_TRUE(config2.get("width", width));
    EXPECT_TRUE(config2.get("height", height));
    EXPECT_TRUE(config2.get("quality", quality));

    EXPECT_EQ(width, static_cast<int>(testWidth));
    EXPECT_EQ(height, static_cast<int>(testHeight));
    EXPECT_EQ(quality, 85);
}

TEST_F(CodecFactoryTest, ConfigBuilderOverwriteValues)
{
    ConfigBuilder config;

    // Set value, then overwrite
    config.width(100).width(200);

    int width;
    EXPECT_TRUE(config.get("width", width));
    EXPECT_EQ(width, 200);
}

TEST_F(CodecFactoryTest, CreateDecoderWithMissingDimensions)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::RAW);
    // Missing width and height for RAW format

    auto decoder = CodecFactory::create<Decoder>(config);
    // RAW decoder might still be created but fail during decode
    // This tests the factory's behavior with incomplete config
}

TEST_F(CodecFactoryTest, JPEGEncoderWithDifferentQualitySettings)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::JPEG)
        .width(testWidth)
        .height(testHeight)
        .pixelFormat(TJPF_RGB)
        .chrominance_subsampling(TJSAMP_420);

    // Test different quality settings
    std::vector<int> qualities = {1, 25, 50, 75, 95, 100};

    for (int quality : qualities)
    {
        config.quality(quality);
        auto encoder = CodecFactory::create<Encoder>(config);
        ASSERT_NE(encoder, nullptr) << "Failed to create encoder with quality " << quality;

        // Verify encoder can estimate size
        size_t estimatedSize = encoder->encodeSizeInBytes();
        EXPECT_GT(estimatedSize, 0) << "Invalid size estimate for quality " << quality;
    }
}

TEST_F(CodecFactoryTest, JPEGEncoderWithDifferentSubsamplingModes)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::JPEG).width(testWidth).height(testHeight).quality(85).pixelFormat(TJPF_RGB);

    // Test different subsampling modes
    std::vector<TJSAMP> subsamplings = {TJSAMP_444, TJSAMP_422, TJSAMP_420, TJSAMP_GRAY};

    for (TJSAMP subsampling : subsamplings)
    {
        config.chrominance_subsampling(subsampling);
        auto encoder = CodecFactory::create<Encoder>(config);
        ASSERT_NE(encoder, nullptr) << "Failed to create encoder with subsampling " << subsampling;
    }
}

TEST_F(CodecFactoryTest, JPEGEncoderWithDifferentPixelFormats)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::JPEG)
        .width(testWidth)
        .height(testHeight)
        .quality(85)
        .chrominance_subsampling(TJSAMP_420);

    // Test different pixel formats
    std::vector<TJPF> pixelFormats = {TJPF_RGB, TJPF_BGR, TJPF_RGBX, TJPF_BGRX, TJPF_GRAY};

    for (TJPF pixelFormat : pixelFormats)
    {
        config.pixelFormat(pixelFormat);
        auto encoder = CodecFactory::create<Encoder>(config);
        ASSERT_NE(encoder, nullptr) << "Failed to create encoder with pixel format " << pixelFormat;
    }
}

// Test codec creation with template specialization
TEST_F(CodecFactoryTest, CreateCodecWithTemplateSpecialization)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::JPEG);

    // Test explicit template instantiation
    auto decoder = CodecFactory::create<Decoder>(config);
    auto encoder = CodecFactory::create<Encoder>(config.width(testWidth).height(testHeight).quality(85));

    EXPECT_NE(decoder, nullptr);
    EXPECT_NE(encoder, nullptr);

    // Verify types are correct
    EXPECT_NE(dynamic_cast<JPEGDecoder*>(decoder.get()), nullptr);
    EXPECT_NE(dynamic_cast<JPEGEncoder*>(encoder.get()), nullptr);
}

// Test error handling for invalid configurations
TEST_F(CodecFactoryTest, InvalidConfigurationHandling)
{
    ConfigBuilder config;

    // Test creating decoder/encoder with invalid enum values
    config.imageFormat(static_cast<ImageFormat>(-1));

    auto decoder = CodecFactory::create<Decoder>(config);
    auto encoder = CodecFactory::create<Encoder>(config);

    EXPECT_EQ(decoder, nullptr);
    EXPECT_EQ(encoder, nullptr);
}

// Test memory management and cleanup
TEST_F(CodecFactoryTest, CodecMemoryManagement)
{
    ConfigBuilder config;
    config.imageFormat(ImageFormat::JPEG).width(testWidth).height(testHeight).quality(85);

    // Create multiple codecs to test memory management
    std::vector<std::unique_ptr<Encoder>> encoders;
    std::vector<std::unique_ptr<Decoder>> decoders;

    for (int i = 0; i < 10; ++i)
    {
        encoders.push_back(CodecFactory::create<Encoder>(config));
        decoders.push_back(CodecFactory::create<Decoder>(config));

        ASSERT_NE(encoders.back(), nullptr);
        ASSERT_NE(decoders.back(), nullptr);
    }

    // Test that all codecs are functional
    for (auto& encoder : encoders)
    {
        EXPECT_GT(encoder->encodeSizeInBytes(), 0);
    }

    // Cleanup happens automatically when vectors go out of scope
}

// Test thread safety (if applicable)
TEST_F(CodecFactoryTest, ConfigBuilderThreadSafety)
{
    // Create configs from different threads and verify no data corruption
    std::vector<std::thread> threads;
    std::vector<ConfigBuilder> configs(5);

    for (int i = 0; i < 5; ++i)
    {
        threads.emplace_back(
            [&configs, i, this]()
            { configs[i].width(testWidth + i).height(testHeight + i).quality(80 + i).imageFormat(ImageFormat::JPEG); });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    // Verify each config has correct values
    for (int i = 0; i < 5; ++i)
    {
        int width, height, quality;
        EXPECT_TRUE(configs[i].get("width", width));
        EXPECT_TRUE(configs[i].get("height", height));
        EXPECT_TRUE(configs[i].get("quality", quality));

        EXPECT_EQ(width, static_cast<int>(testWidth + i));
        EXPECT_EQ(height, static_cast<int>(testHeight + i));
        EXPECT_EQ(quality, 80 + i);
    }
}
