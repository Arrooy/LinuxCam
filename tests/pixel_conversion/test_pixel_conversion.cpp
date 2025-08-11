#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include "LinuxFace/Image/pixel_conversion.h"

using namespace linuxface::pixel_conversion;

// Test fixture class for pixel conversion tests
class PixelConversionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Initialize test data
        rgba_pixel[0] = 128; // R
        rgba_pixel[1] = 64;  // G
        rgba_pixel[2] = 192; // B
        rgba_pixel[3] = 255; // A

        rgb_pixel[0] = 128; // R
        rgb_pixel[1] = 64;  // G
        rgb_pixel[2] = 192; // B

        gray_pixel[0] = 100; // Grayscale value
    }

    unsigned char rgba_pixel[4];
    unsigned char rgb_pixel[3];
    unsigned char gray_pixel[1];
    unsigned char output[4]; // Buffer for output
};

// Tests for getConversionType function
class ConversionTypeTest : public ::testing::Test {};

TEST_F(ConversionTypeTest, DirectCopy_SamePixelSizes)
{
    EXPECT_EQ(getConversionType(3, 3), ConversionType::DIRECT_COPY);
    EXPECT_EQ(getConversionType(4, 4), ConversionType::DIRECT_COPY);
    EXPECT_EQ(getConversionType(1, 1), ConversionType::DIRECT_COPY);
}

TEST_F(ConversionTypeTest, RGBA_Conversions)
{
    EXPECT_EQ(getConversionType(4, 3), ConversionType::RGBA_TO_RGB);
    EXPECT_EQ(getConversionType(4, 1), ConversionType::RGBA_TO_GRAY);
}

TEST_F(ConversionTypeTest, RGB_Conversions)
{
    EXPECT_EQ(getConversionType(3, 4), ConversionType::RGB_TO_RGBA);
    EXPECT_EQ(getConversionType(3, 1), ConversionType::RGB_TO_GRAY);
}

TEST_F(ConversionTypeTest, Grayscale_Conversions)
{
    EXPECT_EQ(getConversionType(1, 3), ConversionType::GRAY_TO_RGB);
    EXPECT_EQ(getConversionType(1, 4), ConversionType::GRAY_TO_RGBA);
}

TEST_F(ConversionTypeTest, Fallback_UnusualSizes)
{
    EXPECT_EQ(getConversionType(2, 3), ConversionType::FALLBACK);
    EXPECT_EQ(getConversionType(5, 2), ConversionType::FALLBACK);
    EXPECT_EQ(getConversionType(0, 1), ConversionType::FALLBACK);
}

// Tests for helper functions
class HelperFunctionTest : public ::testing::Test {};

TEST_F(HelperFunctionTest, RgbToGrayscale_KnownValues)
{
    // Test with pure colors
    EXPECT_EQ(rgbToGrayscale(255, 0, 0), static_cast<unsigned char>(0.299f * 255)); // Red
    EXPECT_EQ(rgbToGrayscale(0, 255, 0), static_cast<unsigned char>(0.587f * 255)); // Green  
    EXPECT_EQ(rgbToGrayscale(0, 0, 255), static_cast<unsigned char>(0.114f * 255)); // Blue
    
    // Test with white and black
    EXPECT_EQ(rgbToGrayscale(255, 255, 255), 255); // White
    EXPECT_EQ(rgbToGrayscale(0, 0, 0), 0);         // Black
    
    // Test with gray (should remain approximately the same)
    const unsigned char gray_input = 128;
    const unsigned char result = rgbToGrayscale(gray_input, gray_input, gray_input);
    EXPECT_NEAR(result, gray_input, 1); // Allow small rounding error
}

TEST_F(HelperFunctionTest, RgbaToRgb_DropsAlpha)
{
    const unsigned char rgba[4] = {128, 64, 192, 100}; // Alpha should be ignored
    unsigned char rgb[3] = {0};
    
    rgbaToRgb(rgba, rgb);
    
    EXPECT_EQ(rgb[0], 128);
    EXPECT_EQ(rgb[1], 64);
    EXPECT_EQ(rgb[2], 192);
}

TEST_F(HelperFunctionTest, RgbToRgba_AddsFullAlpha)
{
    const unsigned char rgb[3] = {128, 64, 192};
    unsigned char rgba[4] = {0};
    
    rgbToRgba(rgb, rgba);
    
    EXPECT_EQ(rgba[0], 128);
    EXPECT_EQ(rgba[1], 64);
    EXPECT_EQ(rgba[2], 192);
    EXPECT_EQ(rgba[3], 255);
}

TEST_F(HelperFunctionTest, GrayscaleToRgb_ReplicatesChannels)
{
    const unsigned char gray = 100;
    unsigned char rgb[3] = {0};
    
    grayscaleToRgb(gray, rgb);
    
    EXPECT_EQ(rgb[0], 100);
    EXPECT_EQ(rgb[1], 100);
    EXPECT_EQ(rgb[2], 100);
}

TEST_F(HelperFunctionTest, GrayscaleToRgba_ReplicatesChannelsWithAlpha)
{
    const unsigned char gray = 100;
    unsigned char rgba[4] = {0};
    
    grayscaleToRgba(gray, rgba);
    
    EXPECT_EQ(rgba[0], 100);
    EXPECT_EQ(rgba[1], 100);
    EXPECT_EQ(rgba[2], 100);
    EXPECT_EQ(rgba[3], 255);
}

// Tests for convertPixel function
TEST_F(PixelConversionTest, ConvertPixel_DirectCopy)
{
    std::memset(output, 0, sizeof(output));
    
    convertPixel(rgba_pixel, output, ConversionType::DIRECT_COPY);
    
    EXPECT_EQ(output[0], rgba_pixel[0]);
    EXPECT_EQ(output[1], rgba_pixel[1]);
    EXPECT_EQ(output[2], rgba_pixel[2]);
    EXPECT_EQ(output[3], rgba_pixel[3]);
}

TEST_F(PixelConversionTest, ConvertPixel_RGBA_TO_RGB)
{
    std::memset(output, 0, sizeof(output));
    
    convertPixel(rgba_pixel, output, ConversionType::RGBA_TO_RGB);
    
    EXPECT_EQ(output[0], rgba_pixel[0]);
    EXPECT_EQ(output[1], rgba_pixel[1]);
    EXPECT_EQ(output[2], rgba_pixel[2]);
}

TEST_F(PixelConversionTest, ConvertPixel_RGB_TO_RGBA)
{
    std::memset(output, 0, sizeof(output));
    
    convertPixel(rgb_pixel, output, ConversionType::RGB_TO_RGBA);
    
    EXPECT_EQ(output[0], rgb_pixel[0]);
    EXPECT_EQ(output[1], rgb_pixel[1]);
    EXPECT_EQ(output[2], rgb_pixel[2]);
    EXPECT_EQ(output[3], 255);
}

TEST_F(PixelConversionTest, ConvertPixel_RGB_TO_RGB)
{
    std::memset(output, 0, sizeof(output));
    
    convertPixel(rgb_pixel, output, ConversionType::RGB_TO_RGB);
    
    EXPECT_EQ(output[0], rgb_pixel[0]);
    EXPECT_EQ(output[1], rgb_pixel[1]);
    EXPECT_EQ(output[2], rgb_pixel[2]);
}

TEST_F(PixelConversionTest, ConvertPixel_GRAY_TO_RGB)
{
    std::memset(output, 0, sizeof(output));
    
    convertPixel(gray_pixel, output, ConversionType::GRAY_TO_RGB);
    
    EXPECT_EQ(output[0], gray_pixel[0]);
    EXPECT_EQ(output[1], gray_pixel[0]);
    EXPECT_EQ(output[2], gray_pixel[0]);
}

TEST_F(PixelConversionTest, ConvertPixel_GRAY_TO_RGBA)
{
    std::memset(output, 0, sizeof(output));
    
    convertPixel(gray_pixel, output, ConversionType::GRAY_TO_RGBA);
    
    EXPECT_EQ(output[0], gray_pixel[0]);
    EXPECT_EQ(output[1], gray_pixel[0]);
    EXPECT_EQ(output[2], gray_pixel[0]);
    EXPECT_EQ(output[3], 255);
}

TEST_F(PixelConversionTest, ConvertPixel_RGB_TO_GRAY)
{
    std::memset(output, 0, sizeof(output));
    
    convertPixel(rgb_pixel, output, ConversionType::RGB_TO_GRAY);
    
    const unsigned char expected = rgbToGrayscale(rgb_pixel[0], rgb_pixel[1], rgb_pixel[2]);
    EXPECT_EQ(output[0], expected);
}

TEST_F(PixelConversionTest, ConvertPixel_RGBA_TO_GRAY)
{
    std::memset(output, 0, sizeof(output));
    
    convertPixel(rgba_pixel, output, ConversionType::RGBA_TO_GRAY);
    
    const unsigned char expected = rgbToGrayscale(rgba_pixel[0], rgba_pixel[1], rgba_pixel[2]);
    EXPECT_EQ(output[0], expected);
}

TEST_F(PixelConversionTest, ConvertPixel_GRAY_TO_GRAY)
{
    std::memset(output, 0, sizeof(output));
    
    convertPixel(gray_pixel, output, ConversionType::GRAY_TO_GRAY);
    
    EXPECT_EQ(output[0], gray_pixel[0]);
}

TEST_F(PixelConversionTest, ConvertPixel_Fallback)
{
    std::memset(output, 0, sizeof(output));
    
    convertPixel(rgba_pixel, output, ConversionType::FALLBACK);
    
    // Fallback should copy available channels
    EXPECT_EQ(output[0], rgba_pixel[0]);
    EXPECT_EQ(output[1], rgba_pixel[1]);
    EXPECT_EQ(output[2], rgba_pixel[2]);
    EXPECT_EQ(output[3], rgba_pixel[3]);
}

// Tests for blending functionality
TEST_F(PixelConversionTest, ConvertPixel_DirectCopyWithBlending)
{
    // Setup destination with existing values
    unsigned char dst[4] = {255, 255, 255, 255}; // White background
    unsigned char src[4] = {0, 0, 0, 128};       // 50% transparent black
    
    convertPixel(src, dst, ConversionType::DIRECT_COPY, 128, true);
    
    // Should blend 50% black with 50% white = 127.5 ≈ 127-128
    EXPECT_NEAR(dst[0], 127, 1);
    EXPECT_NEAR(dst[1], 127, 1);
    EXPECT_NEAR(dst[2], 127, 1);
    EXPECT_EQ(dst[3], 128); // Alpha should be preserved
}

TEST_F(PixelConversionTest, ConvertPixel_DirectCopyWithoutBlending)
{
    unsigned char dst[4] = {255, 255, 255, 255}; // White background
    unsigned char src[4] = {0, 0, 0, 128};       // 50% transparent black
    
    convertPixel(src, dst, ConversionType::DIRECT_COPY, 128, false);
    
    // Should copy directly without blending
    EXPECT_EQ(dst[0], 0);
    EXPECT_EQ(dst[1], 0);
    EXPECT_EQ(dst[2], 0);
    EXPECT_EQ(dst[3], 128);
}

// Tests for copyPixelBlock function
class PixelBlockTest : public ::testing::Test {};

TEST_F(PixelBlockTest, CopyPixelBlock_BasicCopy)
{
    unsigned char src[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}; // 4 RGB pixels
    unsigned char dst[12] = {0};
    
    copyPixelBlock(src, dst, 0, 0, 4, 3); // Copy 4 pixels, 3 bytes each
    
    for (int i = 0; i < 12; ++i)
    {
        EXPECT_EQ(dst[i], src[i]);
    }
}

TEST_F(PixelBlockTest, CopyPixelBlock_WithOffsets)
{
    unsigned char src[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}; // 4 RGBA pixels
    unsigned char dst[16] = {0};
    
    // Copy 2 pixels starting from offset 4 to offset 8
    copyPixelBlock(src, dst, 4, 8, 2, 4);
    
    EXPECT_EQ(dst[8], src[4]);   // First copied pixel
    EXPECT_EQ(dst[9], src[5]);
    EXPECT_EQ(dst[10], src[6]);
    EXPECT_EQ(dst[11], src[7]);
    EXPECT_EQ(dst[12], src[8]);  // Second copied pixel
    EXPECT_EQ(dst[13], src[9]);
    EXPECT_EQ(dst[14], src[10]);
    EXPECT_EQ(dst[15], src[11]);
}

// Tests for convertPixelRow function
class PixelRowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Initialize test data for 3 pixels
        // RGBA data: 3 pixels
        rgba_row[0] = 255; rgba_row[1] = 0;   rgba_row[2] = 0;   rgba_row[3] = 255; // Red
        rgba_row[4] = 0;   rgba_row[5] = 255; rgba_row[6] = 0;   rgba_row[7] = 255; // Green  
        rgba_row[8] = 0;   rgba_row[9] = 0;   rgba_row[10] = 255; rgba_row[11] = 255; // Blue
        
        // RGB data: 3 pixels
        rgb_row[0] = 255; rgb_row[1] = 0;   rgb_row[2] = 0;   // Red
        rgb_row[3] = 0;   rgb_row[4] = 255; rgb_row[5] = 0;   // Green
        rgb_row[6] = 0;   rgb_row[7] = 0;   rgb_row[8] = 255; // Blue
        
        // Grayscale data: 3 pixels
        gray_row[0] = 100;
        gray_row[1] = 150;
        gray_row[2] = 200;
    }
    
    static const size_t WIDTH = 3;
    unsigned char rgba_row[WIDTH * 4];
    unsigned char rgb_row[WIDTH * 3];
    unsigned char gray_row[WIDTH];
    unsigned char output_row[WIDTH * 4]; // Max size buffer
};

TEST_F(PixelRowTest, ConvertPixelRow_RGBA_TO_RGB)
{
    std::memset(output_row, 0, sizeof(output_row));
    
    convertPixelRow(rgba_row, output_row, WIDTH, ConversionType::RGBA_TO_RGB);
    
    // Check first pixel (red)
    EXPECT_EQ(output_row[0], 255); // R
    EXPECT_EQ(output_row[1], 0);   // G
    EXPECT_EQ(output_row[2], 0);   // B
    
    // Check second pixel (green)
    EXPECT_EQ(output_row[3], 0);   // R
    EXPECT_EQ(output_row[4], 255); // G
    EXPECT_EQ(output_row[5], 0);   // B
    
    // Check third pixel (blue)
    EXPECT_EQ(output_row[6], 0);   // R
    EXPECT_EQ(output_row[7], 0);   // G
    EXPECT_EQ(output_row[8], 255); // B
}

TEST_F(PixelRowTest, ConvertPixelRow_RGB_TO_RGBA)
{
    std::memset(output_row, 0, sizeof(output_row));
    
    convertPixelRow(rgb_row, output_row, WIDTH, ConversionType::RGB_TO_RGBA);
    
    // Check first pixel (red)
    EXPECT_EQ(output_row[0], 255); // R
    EXPECT_EQ(output_row[1], 0);   // G
    EXPECT_EQ(output_row[2], 0);   // B
    EXPECT_EQ(output_row[3], 255); // A
    
    // Check second pixel (green)
    EXPECT_EQ(output_row[4], 0);   // R
    EXPECT_EQ(output_row[5], 255); // G
    EXPECT_EQ(output_row[6], 0);   // B
    EXPECT_EQ(output_row[7], 255); // A
    
    // Check third pixel (blue)
    EXPECT_EQ(output_row[8], 0);   // R
    EXPECT_EQ(output_row[9], 0);   // G
    EXPECT_EQ(output_row[10], 255); // B
    EXPECT_EQ(output_row[11], 255); // A
}

TEST_F(PixelRowTest, ConvertPixelRow_GRAY_TO_RGB)
{
    std::memset(output_row, 0, sizeof(output_row));
    
    convertPixelRow(gray_row, output_row, WIDTH, ConversionType::GRAY_TO_RGB);
    
    // Check first pixel
    EXPECT_EQ(output_row[0], 100); // R
    EXPECT_EQ(output_row[1], 100); // G
    EXPECT_EQ(output_row[2], 100); // B
    
    // Check second pixel
    EXPECT_EQ(output_row[3], 150); // R
    EXPECT_EQ(output_row[4], 150); // G
    EXPECT_EQ(output_row[5], 150); // B
    
    // Check third pixel
    EXPECT_EQ(output_row[6], 200); // R
    EXPECT_EQ(output_row[7], 200); // G
    EXPECT_EQ(output_row[8], 200); // B
}

TEST_F(PixelRowTest, ConvertPixelRow_GRAY_TO_RGBA)
{
    std::memset(output_row, 0, sizeof(output_row));
    
    convertPixelRow(gray_row, output_row, WIDTH, ConversionType::GRAY_TO_RGBA);
    
    // Check first pixel
    EXPECT_EQ(output_row[0], 100); // R
    EXPECT_EQ(output_row[1], 100); // G
    EXPECT_EQ(output_row[2], 100); // B
    EXPECT_EQ(output_row[3], 255); // A
    
    // Check second pixel
    EXPECT_EQ(output_row[4], 150); // R
    EXPECT_EQ(output_row[5], 150); // G
    EXPECT_EQ(output_row[6], 150); // B
    EXPECT_EQ(output_row[7], 255); // A
}

TEST_F(PixelRowTest, ConvertPixelRow_RGB_TO_GRAY)
{
    std::memset(output_row, 0, sizeof(output_row));
    
    convertPixelRow(rgb_row, output_row, WIDTH, ConversionType::RGB_TO_GRAY);
    
    // Check first pixel (pure red)
    const unsigned char expected1 = rgbToGrayscale(255, 0, 0);
    EXPECT_EQ(output_row[0], expected1);
    
    // Check second pixel (pure green)
    const unsigned char expected2 = rgbToGrayscale(0, 255, 0);
    EXPECT_EQ(output_row[1], expected2);
    
    // Check third pixel (pure blue)
    const unsigned char expected3 = rgbToGrayscale(0, 0, 255);
    EXPECT_EQ(output_row[2], expected3);
}

TEST_F(PixelRowTest, ConvertPixelRow_RGBA_TO_GRAY)
{
    std::memset(output_row, 0, sizeof(output_row));
    
    convertPixelRow(rgba_row, output_row, WIDTH, ConversionType::RGBA_TO_GRAY);
    
    // Check first pixel (pure red)
    const unsigned char expected1 = rgbToGrayscale(255, 0, 0);
    EXPECT_EQ(output_row[0], expected1);
    
    // Check second pixel (pure green)
    const unsigned char expected2 = rgbToGrayscale(0, 255, 0);
    EXPECT_EQ(output_row[1], expected2);
    
    // Check third pixel (pure blue)
    const unsigned char expected3 = rgbToGrayscale(0, 0, 255);
    EXPECT_EQ(output_row[2], expected3);
}

TEST_F(PixelRowTest, ConvertPixelRow_RGB_TO_RGB)
{
    std::memset(output_row, 0, sizeof(output_row));
    
    convertPixelRow(rgb_row, output_row, WIDTH, ConversionType::RGB_TO_RGB);
    
    // Should be exact copy
    for (size_t i = 0; i < WIDTH * 3; ++i)
    {
        EXPECT_EQ(output_row[i], rgb_row[i]);
    }
}

TEST_F(PixelRowTest, ConvertPixelRow_GRAY_TO_GRAY)
{
    std::memset(output_row, 0, sizeof(output_row));
    
    convertPixelRow(gray_row, output_row, WIDTH, ConversionType::GRAY_TO_GRAY);
    
    // Should be exact copy
    for (size_t i = 0; i < WIDTH; ++i)
    {
        EXPECT_EQ(output_row[i], gray_row[i]);
    }
}

TEST_F(PixelRowTest, ConvertPixelRow_FallbackCase)
{
    std::memset(output_row, 0, sizeof(output_row));
    
    // Test fallback behavior
    convertPixelRow(rgba_row, output_row, WIDTH, ConversionType::FALLBACK);
    
    // Fallback should do pixel-by-pixel conversion
    // The exact behavior depends on the implementation but should not crash
    // and should produce some reasonable output
    bool hasNonZeroOutput = false;
    for (size_t i = 0; i < WIDTH * 4; ++i)
    {
        if (output_row[i] != 0)
        {
            hasNonZeroOutput = true;
            break;
        }
    }
    EXPECT_TRUE(hasNonZeroOutput);
}

// Edge case tests
class EdgeCaseTest : public ::testing::Test {};

TEST_F(EdgeCaseTest, ConvertPixelRow_ZeroWidth)
{
    unsigned char src[1] = {255};
    unsigned char dst[1] = {0};
    
    // Should not crash with zero width
    convertPixelRow(src, dst, 0, ConversionType::RGB_TO_RGBA);
    
    // Output should remain unchanged
    EXPECT_EQ(dst[0], 0);
}

TEST_F(EdgeCaseTest, ConvertPixel_TransparentPixel)
{
    unsigned char src[4] = {255, 255, 255, 0}; // White but fully transparent
    unsigned char dst[4] = {0, 0, 0, 255};     // Black opaque
    
    convertPixel(src, dst, ConversionType::DIRECT_COPY, 0, true);
    
    // With alpha=0, destination should remain unchanged when blending
    EXPECT_EQ(dst[0], 0);
    EXPECT_EQ(dst[1], 0);
    EXPECT_EQ(dst[2], 0);
}

TEST_F(EdgeCaseTest, ConvertPixel_FullyOpaquePixel)
{
    unsigned char src[4] = {128, 64, 192, 255}; // Fully opaque
    unsigned char dst[4] = {0, 0, 0, 0};        // Transparent black
    
    convertPixel(src, dst, ConversionType::DIRECT_COPY, 255, true);
    
    // Should replace destination completely
    EXPECT_EQ(dst[0], 128);
    EXPECT_EQ(dst[1], 64);
    EXPECT_EQ(dst[2], 192);
    EXPECT_EQ(dst[3], 255);
}

// Performance/stress tests
class StressTest : public ::testing::Test {};

TEST_F(StressTest, ConvertPixelRow_LargeWidth)
{
    const size_t large_width = 1920; // HD width
    std::vector<unsigned char> src_row(large_width * 3, 128); // RGB gray
    std::vector<unsigned char> dst_row(large_width * 4, 0);   // RGBA output
    
    // Should not crash or take excessive time
    convertPixelRow(src_row.data(), dst_row.data(), large_width, ConversionType::RGB_TO_RGBA);
    
    // Verify a few pixels
    EXPECT_EQ(dst_row[0], 128);   // First pixel R
    EXPECT_EQ(dst_row[1], 128);   // First pixel G
    EXPECT_EQ(dst_row[2], 128);   // First pixel B
    EXPECT_EQ(dst_row[3], 255);   // First pixel A
    
    EXPECT_EQ(dst_row[(large_width - 1) * 4 + 0], 128); // Last pixel R
    EXPECT_EQ(dst_row[(large_width - 1) * 4 + 3], 255); // Last pixel A
}

TEST_F(StressTest, MultipleConversions_Consistency)
{
    unsigned char original[3] = {128, 64, 192}; // RGB
    unsigned char temp[4] = {0};                 // RGBA temp
    unsigned char result[3] = {0};               // RGB result
    
    // RGB -> RGBA -> RGB should preserve original values
    convertPixel(original, temp, ConversionType::RGB_TO_RGBA);
    convertPixel(temp, result, ConversionType::RGBA_TO_RGB);
    
    EXPECT_EQ(result[0], original[0]);
    EXPECT_EQ(result[1], original[1]);
    EXPECT_EQ(result[2], original[2]);
}
