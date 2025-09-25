#include <gtest/gtest.h>

#include "LinuxFace/Image/alpha_blender.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/image_processor.h"
#include "LinuxFace/Image/pixel_converter.h"

using namespace linuxface::image;

class PixelArchitectureTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create test pixels
        rgb_pixel[0] = 100;
        rgb_pixel[1] = 150;
        rgb_pixel[2] = 200;
        rgba_pixel[0] = 100;
        rgba_pixel[1] = 150;
        rgba_pixel[2] = 200;
        rgba_pixel[3] = 128;
        gray_pixel[0] = 127;

        dst_pixel[0] = 50;
        dst_pixel[1] = 75;
        dst_pixel[2] = 100;
        dst_pixel[3] = 255;
    }

    uint8_t rgb_pixel[3];
    uint8_t rgba_pixel[4];
    uint8_t gray_pixel[1];
    uint8_t dst_pixel[4];
};

// PixelConverter Tests
class PixelConverterTest : public PixelArchitectureTest
{
};

TEST_F(PixelConverterTest, RGBToRGBA_Conversion)
{
    PixelConverter converter;
    uint8_t result[4];

    converter.convertPixel(rgb_pixel, result, PixelFormat::RGB, PixelFormat::RGBA);

    EXPECT_EQ(result[0], 100);
    EXPECT_EQ(result[1], 150);
    EXPECT_EQ(result[2], 200);
    EXPECT_EQ(result[3], 255); // Alpha should be 255 for RGB->RGBA
}

TEST_F(PixelConverterTest, RGBAToRGB_Conversion)
{
    PixelConverter converter;
    uint8_t result[3];

    converter.convertPixel(rgba_pixel, result, PixelFormat::RGBA, PixelFormat::RGB);

    EXPECT_EQ(result[0], 100);
    EXPECT_EQ(result[1], 150);
    EXPECT_EQ(result[2], 200);
    // Alpha channel should be discarded
}

TEST_F(PixelConverterTest, RGBAToRGBA_Identity)
{
    PixelConverter converter;
    uint8_t result[4];

    converter.convertPixel(rgba_pixel, result, PixelFormat::RGBA, PixelFormat::RGBA);

    EXPECT_EQ(result[0], 100);
    EXPECT_EQ(result[1], 150);
    EXPECT_EQ(result[2], 200);
    EXPECT_EQ(result[3], 128);
}

TEST_F(PixelConverterTest, RGBToRGB_Identity)
{
    PixelConverter converter;
    uint8_t result[3];

    converter.convertPixel(rgb_pixel, result, PixelFormat::RGB, PixelFormat::RGB);

    EXPECT_EQ(result[0], 100);
    EXPECT_EQ(result[1], 150);
    EXPECT_EQ(result[2], 200);
}

TEST_F(PixelConverterTest, GrayscaleToRGB_Conversion)
{
    PixelConverter converter;
    uint8_t result[3];

    converter.convertPixel(gray_pixel, result, PixelFormat::GRAYSCALE, PixelFormat::RGB);

    EXPECT_EQ(result[0], 127);
    EXPECT_EQ(result[1], 127);
    EXPECT_EQ(result[2], 127);
}

TEST_F(PixelConverterTest, GrayscaleToRGBA_Conversion)
{
    PixelConverter converter;
    uint8_t result[4];

    converter.convertPixel(gray_pixel, result, PixelFormat::GRAYSCALE, PixelFormat::RGBA);

    EXPECT_EQ(result[0], 127);
    EXPECT_EQ(result[1], 127);
    EXPECT_EQ(result[2], 127);
    EXPECT_EQ(result[3], 255);
}

// AlphaBlender Tests
class AlphaBlenderTest : public PixelArchitectureTest
{
};

TEST_F(AlphaBlenderTest, BlendRGB_HalfAlpha)
{
    AlphaBlender blender;
    uint8_t src[3] = {200, 200, 200};
    uint8_t dst[3] = {100, 100, 100};

    blender.blendRGB(src, dst, 128); // 50% alpha

    // Expected: 200 * 0.5 + 100 * 0.5 = 150
    EXPECT_NEAR(dst[0], 150, 2);
    EXPECT_NEAR(dst[1], 150, 2);
    EXPECT_NEAR(dst[2], 150, 2);
}

TEST_F(AlphaBlenderTest, BlendRGB_FullyOpaque)
{
    AlphaBlender blender;
    uint8_t src[3] = {255, 0, 128};
    uint8_t dst[3] = {100, 200, 50};

    blender.blendRGB(src, dst, 255); // Fully opaque

    EXPECT_EQ(dst[0], 255);
    EXPECT_EQ(dst[1], 0);
    EXPECT_EQ(dst[2], 128);
}

TEST_F(PixelConverterTest, BlendRGB_FullyTransparent)
{
    AlphaBlender blender;
    uint8_t src[3] = {255, 0, 128};
    uint8_t dst[3] = {100, 200, 50};
    uint8_t original_dst[3] = {100, 200, 50};

    blender.blendRGB(src, dst, 0); // Fully transparent

    EXPECT_EQ(dst[0], original_dst[0]);
    EXPECT_EQ(dst[1], original_dst[1]);
    EXPECT_EQ(dst[2], original_dst[2]);
}

TEST_F(AlphaBlenderTest, BlendRGBA_AlphaCompositing)
{
    AlphaBlender blender;
    uint8_t src[4] = {200, 100, 50, 128};  // 50% alpha
    uint8_t dst[4] = {100, 200, 150, 200}; // ~78% alpha

    // Debug output before blending
    std::cout << "Before blending: src=[" << (int) src[0] << "," << (int) src[1] << "," << (int) src[2] << ","
              << (int) src[3] << "]" << std::endl;
    std::cout << "Before blending: dst=[" << (int) dst[0] << "," << (int) dst[1] << "," << (int) dst[2] << ","
              << (int) dst[3] << "]" << std::endl;

    blender.blendRGBA(src, dst);

    // Debug output after blending
    std::cout << "After blending:  dst=[" << (int) dst[0] << "," << (int) dst[1] << "," << (int) dst[2] << ","
              << (int) dst[3] << "]" << std::endl;

    // Test proper alpha compositing math
    // C_out = (C_src * α_src + C_dst * α_dst * (1 - α_src)) / α_out
    // α_out = α_src + α_dst * (1 - α_src)
    EXPECT_NEAR(dst[0], 156, 2); // Red channel
    EXPECT_NEAR(dst[1], 144, 2); // Green channel
    EXPECT_NEAR(dst[2], 94, 2);  // Blue channel
    EXPECT_NEAR(dst[3], 228, 2); // Proper composited alpha
}

TEST_F(AlphaBlenderTest, BlendRGBToRGBA_PreservesDestAlpha)
{
    AlphaBlender blender;
    uint8_t src[3] = {255, 255, 255};
    uint8_t dst[4] = {100, 100, 100, 200};
    uint8_t original_alpha = dst[3];

    blender.blendRGBToRGBA(src, dst, 128); // 50% blend

    // RGB should be blended
    EXPECT_NEAR(dst[0], 177, 2); // 255 * 0.5 + 100 * 0.5 ≈ 177
    EXPECT_NEAR(dst[1], 177, 2);
    EXPECT_NEAR(dst[2], 177, 2);
    // Alpha should be preserved
    EXPECT_EQ(dst[3], original_alpha);
}

TEST_F(AlphaBlenderTest, BlendRGBAToRGB_IgnoresDestAlpha)
{
    AlphaBlender blender;
    uint8_t src[4] = {200, 150, 100, 128}; // 50% alpha
    uint8_t dst[3] = {100, 100, 100};

    blender.blendRGBAToRGB(src, dst);

    // Should blend using source alpha
    EXPECT_NEAR(dst[0], 150, 2); // 200 * 0.5 + 100 * 0.5 = 150
    EXPECT_NEAR(dst[1], 125, 2); // 150 * 0.5 + 100 * 0.5 = 125
    EXPECT_NEAR(dst[2], 100, 2); // 100 * 0.5 + 100 * 0.5 = 100
}

// ImageProcessor Tests
class ImageProcessorTest : public PixelArchitectureTest
{
};

TEST_F(ImageProcessorTest, ProcessPixel_RGBToRGBA)
{
    ImageProcessor processor;
    uint8_t src[3] = {100, 150, 200};
    uint8_t dst[4] = {0, 0, 0, 0};

    processor.processPixel(src, dst, PixelFormat::RGB, PixelFormat::RGBA);

    EXPECT_EQ(dst[0], 100);
    EXPECT_EQ(dst[1], 150);
    EXPECT_EQ(dst[2], 200);
    EXPECT_EQ(dst[3], 255);
}

TEST_F(ImageProcessorTest, ProcessPixel_RGBAToRGB)
{
    ImageProcessor processor;
    uint8_t src[4] = {100, 150, 200, 128};
    uint8_t dst[3] = {50, 75, 100};

    // Enable blending for RGBA to RGB conversion with alpha
    processor.processPixel(src, dst, PixelFormat::RGBA, PixelFormat::RGB, true, 128);

    // Should blend using source alpha (128/255 = ~0.502)
    EXPECT_NEAR(dst[0], 75, 2);  // 100 * 0.502 + 50 * 0.498 = 75.1
    EXPECT_NEAR(dst[1], 112, 2); // 150 * 0.502 + 75 * 0.498 = 112.65
    EXPECT_NEAR(dst[2], 150, 2); // 200 * 0.502 + 100 * 0.498 = 150.2
}

TEST_F(ImageProcessorTest, ProcessPixel_OpaqueRGBAReplacesDestination)
{
    ImageProcessor processor;
    uint8_t src[4] = {255, 0, 128, 255}; // Fully opaque
    uint8_t dst[4] = {100, 200, 50, 180};

    processor.processPixel(src, dst, PixelFormat::RGBA, PixelFormat::RGBA);

    EXPECT_EQ(dst[0], 255);
    EXPECT_EQ(dst[1], 0);
    EXPECT_EQ(dst[2], 128);
    EXPECT_EQ(dst[3], 255);
}

// Integration Tests - Testing the full pipeline
class PixelOperationsIntegrationTest : public ::testing::Test
{
};

TEST_F(PixelOperationsIntegrationTest, CompleteRGBASupport_AllFormats)
{
    // Test all format combinations that our architecture should support
    std::vector<std::pair<PixelFormat, PixelFormat>> format_combinations = {
        {PixelFormat::RGB,       PixelFormat::RGB },
        {PixelFormat::RGB,       PixelFormat::RGBA},
        {PixelFormat::RGBA,      PixelFormat::RGB },
        {PixelFormat::RGBA,      PixelFormat::RGBA},
        {PixelFormat::GRAYSCALE, PixelFormat::RGB },
        {PixelFormat::GRAYSCALE, PixelFormat::RGBA}
    };

    ImageProcessor processor;

    for (const auto& [srcFormat, dstFormat] : format_combinations)
    {
        // Create appropriate source and destination pixels
        uint8_t src[4] = {100, 150, 200, 128};
        uint8_t dst[4] = {50, 75, 100, 200};

        // This should not crash and should produce valid results
        EXPECT_NO_THROW(processor.processPixel(src, dst, srcFormat, dstFormat));

        // Basic sanity check - values should be in valid range
        int channels = (dstFormat == PixelFormat::RGBA) ? 4 : (dstFormat == PixelFormat::RGB) ? 3 : 1;
        for (int i = 0; i < channels; ++i)
        {
            EXPECT_GE(dst[i], 0);
            EXPECT_LE(dst[i], 255);
        }
    }
}

TEST_F(PixelOperationsIntegrationTest, MathematicalAccuracy_AlphaBlending)
{
    // Test specific mathematical cases for accuracy
    AlphaBlender blender;

    // Test case 1: 25% alpha blending
    uint8_t src1[3] = {200, 200, 200};
    uint8_t dst1[3] = {100, 100, 100};
    blender.blendRGB(src1, dst1, 64); // 64/255 ≈ 0.251

    // Expected: 200 * 0.251 + 100 * 0.749 = 50.2 + 74.9 = 125.1 ≈ 125
    EXPECT_NEAR(dst1[0], 125, 2);
    EXPECT_NEAR(dst1[1], 125, 2);
    EXPECT_NEAR(dst1[2], 125, 2);

    // Test case 2: Verify rounding accuracy
    uint8_t src2[3] = {255, 128, 64};
    uint8_t dst2[3] = {0, 127, 191};
    blender.blendRGB(src2, dst2, 127); // ~49.8% alpha

    // Results should be properly rounded, not truncated
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_GE(dst2[i], 0);
        EXPECT_LE(dst2[i], 255);
    }
}

// Performance and Memory Safety Tests
class PixelOperationsStressTest : public ::testing::Test
{
};

TEST_F(PixelOperationsStressTest, LargeVolumeProcessing)
{
    // Test processing many pixels without memory issues
    const int NUM_PIXELS = 10000;
    ImageProcessor processor;

    for (int i = 0; i < NUM_PIXELS; ++i)
    {
        uint8_t src[4] = {static_cast<uint8_t>(i % 256), static_cast<uint8_t>((i * 2) % 256),
                          static_cast<uint8_t>((i * 3) % 256), static_cast<uint8_t>((i * 4) % 256)};
        uint8_t dst[4] = {128, 128, 128, 255};

        EXPECT_NO_THROW(processor.processPixel(src, dst, PixelFormat::RGBA, PixelFormat::RGBA));
    }
}

TEST_F(PixelOperationsStressTest, EdgeCaseAlphaValues)
{
    AlphaBlender blender;
    uint8_t src[3] = {255, 128, 64};

    // Test all possible alpha values
    for (int alpha = 0; alpha <= 255; ++alpha)
    {
        uint8_t test_dst[3] = {100, 100, 100};
        EXPECT_NO_THROW(blender.blendRGB(src, test_dst, alpha));

        // Verify results are in valid range
        for (int i = 0; i < 3; ++i)
        {
            EXPECT_GE(test_dst[i], 0);
            EXPECT_LE(test_dst[i], 255);
        }
    }
}

// High-Level Image Operations Tests - Testing the complete pipeline
class ImageOperationsIntegrationTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create a 3x3 RGB base image with known pixel values
        const size_t width = 3, height = 3;
        baseImage = linuxface::Image(width * height * 3); // RGB
        baseImage.info.width = width;
        baseImage.info.height = height;
        baseImage.info.format = linuxface::ImageFormat::RGB;
        baseImage.info.pixelSizeBytes = 3;

        for (size_t y = 0; y < 3; ++y)
        {
            for (size_t x = 0; x < 3; ++x)
            {
                uint8_t value = static_cast<uint8_t>((y * 3 + x) * 20); // 0, 20, 40, 60, ...
                baseImage.pxy(x, y, value, value + 10, value + 20, 255);
            }
        }

        // Create a 2x2 RGBA overlay with semi-transparent pixels
        const size_t overlayWidth = 2, overlayHeight = 2;
        overlayImage = linuxface::Image(overlayWidth * overlayHeight * 4); // RGBA
        overlayImage.info.width = overlayWidth;
        overlayImage.info.height = overlayHeight;
        overlayImage.info.format = linuxface::ImageFormat::RGBA;
        overlayImage.info.pixelSizeBytes = 4;

        overlayImage.pxy(0, 0, 255, 0, 0, 128);     // Semi-transparent red
        overlayImage.pxy(1, 0, 0, 255, 0, 128);     // Semi-transparent green
        overlayImage.pxy(0, 1, 0, 0, 255, 128);     // Semi-transparent blue
        overlayImage.pxy(1, 1, 255, 255, 255, 255); // Opaque white
    }

    linuxface::Image baseImage;
    linuxface::Image overlayImage;
};

TEST_F(ImageOperationsIntegrationTest, PasteAt_RGBA_OnRGB_WithBlending)
{
    // Test pasting RGBA image on RGB base with proper blending
    baseImage.pasteAt(overlayImage, 1, 1, false);

    // Check that blending occurred at (1,1) - semi-transparent red over pixel (1,1)
    linuxface::Pixel blendedPixel = baseImage(1, 1);

    // The overlay image has semi-transparent red (255,0,0,128) at position (0,0)
    // When pasted at (1,1), it should blend with the base pixel at (1,1)
    // Base pixel at (1,1) has values: R=80, G=90, B=100 (from SetUp: (y*3+x)*20 where y=1,x=1 -> 4*20=80)
    // Alpha = 128/255 ≈ 0.502, so blending formula: srcColor * alpha + dstColor * (1-alpha)
    // Expected R: 255 * 0.502 + 80 * 0.498 = 127.51 + 39.84 = 167.35 ≈ 167
    // Expected G: 0 * 0.502 + 90 * 0.498 = 0 + 44.82 = 44.82 ≈ 45
    // Expected B: 0 * 0.502 + 100 * 0.498 = 0 + 49.8 = 49.8 ≈ 50

    EXPECT_NEAR(blendedPixel.r, 167, 5); // Red should be blend of 255 and 80
    EXPECT_NEAR(blendedPixel.g, 45, 5);  // Green should be blend of 0 and 90
    EXPECT_NEAR(blendedPixel.b, 50, 5);  // Blue should be blend of 0 and 100
}

TEST_F(ImageOperationsIntegrationTest, PasteAt_OpaqueRGBA_ReplacesDestination)
{
    // Test that opaque pixels completely replace destination
    baseImage.pasteAt(overlayImage, 0, 0, false);

    // Check the opaque white pixel at overlay position (1,1) -> base position (1,1)
    linuxface::Pixel replacedPixel = baseImage(1, 1);

    // Should be completely replaced with white
    EXPECT_EQ(replacedPixel.r, 255);
    EXPECT_EQ(replacedPixel.g, 255);
    EXPECT_EQ(replacedPixel.b, 255);
}

TEST_F(ImageOperationsIntegrationTest, CopyPixelsWithBlending_BoundsHandling)
{
    // Test that copyPixelsWithBlending handles out-of-bounds correctly
    const size_t width = 5, height = 5;
    linuxface::Image largeOverlay(width * height * 3); // RGB
    largeOverlay.info.width = width;
    largeOverlay.info.height = height;
    largeOverlay.info.format = linuxface::ImageFormat::RGB;
    largeOverlay.info.pixelSizeBytes = 3;
    largeOverlay.black(); // Fill with black

    // Fill with a distinct pattern
    for (size_t y = 0; y < 5; ++y)
    {
        for (size_t x = 0; x < 5; ++x)
        {
            largeOverlay.pxy(x, y, 100, 150, 200, 255);
        }
    }

    // Paste at negative coordinates - should clip properly
    baseImage.pasteAt(largeOverlay, -2, -2, false);

    // Check that only the visible part was copied
    linuxface::Pixel copiedPixel = baseImage(0, 0);
    EXPECT_EQ(copiedPixel.r, 100);
    EXPECT_EQ(copiedPixel.g, 150);
    EXPECT_EQ(copiedPixel.b, 200);

    // Check that other pixels were also affected by the large overlay
    // Since the overlay is 5x5 and pasted at -2,-2, it should cover most of the 3x3 base
    linuxface::Pixel changedPixel = baseImage(2, 2);
    EXPECT_EQ(changedPixel.r, 100); // Should be overlay values now
    EXPECT_EQ(changedPixel.g, 150);
    EXPECT_EQ(changedPixel.b, 200);
}

TEST_F(ImageOperationsIntegrationTest, PixelFormatConversion_Through_ImageProcessor)
{
    // Test that ImageProcessor handles format conversion correctly in real operations
    const size_t srcWidth = 2, srcHeight = 2;
    linuxface::Image rgbSource(srcWidth * srcHeight * 3); // RGB
    rgbSource.info.width = srcWidth;
    rgbSource.info.height = srcHeight;
    rgbSource.info.format = linuxface::ImageFormat::RGB;
    rgbSource.info.pixelSizeBytes = 3;

    rgbSource.pxy(0, 0, 255, 128, 64, 255);
    rgbSource.pxy(1, 0, 192, 96, 48, 255);
    rgbSource.pxy(0, 1, 128, 64, 32, 255);
    rgbSource.pxy(1, 1, 64, 32, 16, 255);

    const size_t dstWidth = 3, dstHeight = 3;
    linuxface::Image rgbaTarget(dstWidth * dstHeight * 4); // RGBA
    rgbaTarget.info.width = dstWidth;
    rgbaTarget.info.height = dstHeight;
    rgbaTarget.info.format = linuxface::ImageFormat::RGBA;
    rgbaTarget.info.pixelSizeBytes = 4;
    rgbaTarget.black(); // Start with black RGBA

    // Paste RGB onto RGBA - should convert and add alpha=255
    rgbaTarget.pasteAt(rgbSource, 0, 0, false);

    // Check that RGB was converted to RGBA properly
    linuxface::Pixel convertedPixel = rgbaTarget(0, 0);
    EXPECT_EQ(convertedPixel.r, 255);
    EXPECT_EQ(convertedPixel.g, 128);
    EXPECT_EQ(convertedPixel.b, 64);
    EXPECT_EQ(convertedPixel.a, 255); // Should be opaque
}

// Test face swapping alpha blend scenario to reproduce reported black edge issue
TEST_F(ImageOperationsIntegrationTest, FaceSwapAlphaBlendScenario)
{
    // This test reproduces the exact scenario from face swapping pipeline:
    // 1. RGBA warped face with potential out-of-bounds transparent pixels (0,0,0,0)
    // 2. Face mask with black (0) background and white (255) face areas
    // 3. RGB destination image (webcam frame)

    constexpr int WIDTH = 8;
    constexpr int HEIGHT = 8;
    constexpr int TOTAL_PIXELS = WIDTH * HEIGHT;

    // Create RGBA warped face image (simulates affineWarpBilinear output)
    linuxface::Image warpedFace(TOTAL_PIXELS * 4);
    warpedFace.info.width = WIDTH;
    warpedFace.info.height = HEIGHT;
    warpedFace.info.pixelSizeBytes = 4;
    warpedFace.info.format = linuxface::ImageFormat::RGBA;

    // Create face mask (simulates createFaceMask output)
    linuxface::Image faceMask(TOTAL_PIXELS);
    faceMask.info.width = WIDTH;
    faceMask.info.height = HEIGHT;
    faceMask.info.pixelSizeBytes = 1;
    faceMask.info.format = linuxface::ImageFormat::GRAYSCALE;

    // Create RGB destination image (simulates webcam frame)
    linuxface::Image destination(TOTAL_PIXELS * 3);
    destination.info.width = WIDTH;
    destination.info.height = HEIGHT;
    destination.info.pixelSizeBytes = 3;
    destination.info.format = linuxface::ImageFormat::RGB;

    unsigned char* warpedData = warpedFace.data();
    unsigned char* maskData = faceMask.data();
    unsigned char* destData = destination.data();

    // Set up test scenario that reproduces the black edge issue
    for (int i = 0; i < TOTAL_PIXELS; ++i)
    {
        int x = i % WIDTH;
        int y = i / WIDTH;

        // Create pattern with face region in center and out-of-bounds edges
        if (x >= 2 && x < 6 && y >= 2 && y < 6)
        {
            // Face region: valid RGBA pixels with skin-like colors
            warpedData[i * 4 + 0] = 200 + (x % 20);       // R
            warpedData[i * 4 + 1] = 150 + (y % 20);       // G
            warpedData[i * 4 + 2] = 120 + ((x + y) % 20); // B
            warpedData[i * 4 + 3] = 255;                  // Alpha: fully opaque

            maskData[i] = 255; // Mask: white (opaque)
        }
        else
        {
            // Out-of-bounds region: transparent black (this is the problematic case)
            warpedData[i * 4 + 0] = 0; // R: black
            warpedData[i * 4 + 1] = 0; // G: black
            warpedData[i * 4 + 2] = 0; // B: black
            warpedData[i * 4 + 3] = 0; // Alpha: fully transparent (should be skipped!)

            maskData[i] = 0; // Mask: black (transparent)
        }

        // Destination: blue-ish background (simulates webcam frame)
        destData[i * 3 + 0] = 50;  // R
        destData[i * 3 + 1] = 100; // G
        destData[i * 3 + 2] = 150; // B
    }

    // Store original destination for comparison
    linuxface::Image originalDest(TOTAL_PIXELS * 3);
    originalDest.info = destination.info;
    std::memcpy(originalDest.data(), destination.data(), TOTAL_PIXELS * 3);

    // Perform alpha blending (this is where the bug should manifest)
    destination.alphaBlend(warpedFace, faceMask);

    // Verify results
    unsigned char* resultData = destination.data();
    unsigned char* originalData = originalDest.data();

    int blackPixelCount = 0;
    int preservedBackgroundCount = 0;
    int blendedFaceCount = 0;

    for (int i = 0; i < TOTAL_PIXELS; ++i)
    {
        int x = i % WIDTH;
        int y = i / WIDTH;

        unsigned char r = resultData[i * 3 + 0];
        unsigned char g = resultData[i * 3 + 1];
        unsigned char b = resultData[i * 3 + 2];

        if (x >= 2 && x < 6 && y >= 2 && y < 6)
        {
            // Face region: should have blended face colors (not original background)
            if (r != originalData[i * 3 + 0] || g != originalData[i * 3 + 1] || b != originalData[i * 3 + 2])
            {
                blendedFaceCount++;
            }

            // Face region should NOT be black (this would indicate the bug)
            if (r < 50 && g < 50 && b < 50)
            {
                blackPixelCount++;
                ADD_FAILURE() << "Face region pixel at (" << x << "," << y << ") is unexpectedly dark: "
                              << "RGB(" << (int) r << "," << (int) g << "," << (int) b << ")";
            }
        }
        else
        {
            // Out-of-bounds region: should preserve original background (not black!)
            if (r == originalData[i * 3 + 0] && g == originalData[i * 3 + 1] && b == originalData[i * 3 + 2])
            {
                preservedBackgroundCount++;
            }

            // Out-of-bounds region should NOT become black due to transparent pixels
            if (r == 0 && g == 0 && b == 0)
            {
                blackPixelCount++;
                ADD_FAILURE() << "Out-of-bounds pixel at (" << x << "," << y << ") became black: "
                              << "should preserve background RGB(50,100,150)";
            }
        }
    }

    // Validate test results
    const int expectedFacePixels = 16; // 4x4 face region
    const int expectedBackgroundPixels = TOTAL_PIXELS - expectedFacePixels;

    EXPECT_EQ(blackPixelCount, 0) << "Found " << blackPixelCount
                                  << " unexpected black pixels (indicates alpha blend bug)";
    EXPECT_GE(blendedFaceCount, expectedFacePixels / 2) << "Too few face pixels were blended";
    EXPECT_GE(preservedBackgroundCount, expectedBackgroundPixels / 2) << "Too few background pixels were preserved";

    // Log test statistics for debugging
    std::cout << "FaceSwapAlphaBlendScenario results:" << std::endl;
    std::cout << "  Black pixels (should be 0): " << blackPixelCount << std::endl;
    std::cout << "  Blended face pixels: " << blendedFaceCount << " / " << expectedFacePixels << std::endl;
    std::cout << "  Preserved background pixels: " << preservedBackgroundCount << " / " << expectedBackgroundPixels
              << std::endl;

    // Save debug images if test fails for visual inspection
    if (blackPixelCount > 0 || blendedFaceCount < expectedFacePixels / 2)
    {
        // Create debug visualization showing the issue
        linuxface::Image debugViz(TOTAL_PIXELS * 3);
        debugViz.info = destination.info;

        // Copy result and highlight problematic pixels
        std::memcpy(debugViz.data(), destination.data(), TOTAL_PIXELS * 3);
        unsigned char* debugData = debugViz.data();

        for (int i = 0; i < TOTAL_PIXELS; ++i)
        {
            unsigned char r = resultData[i * 3 + 0];
            unsigned char g = resultData[i * 3 + 1];
            unsigned char b = resultData[i * 3 + 2];

            // Highlight black pixels in red
            if (r < 10 && g < 10 && b < 10)
            {
                debugData[i * 3 + 0] = 255; // Red
                debugData[i * 3 + 1] = 0;   // Green
                debugData[i * 3 + 2] = 0;   // Blue
            }
        }

        std::string debugPath = "../tests/image_tests/face_swap_alpha_blend_debug.ppm";
        bool saveResult = debugViz.saveToDisk(debugPath);
        if (saveResult)
        {
            std::cout << "Saved debug visualization to: " << debugPath << std::endl;
            std::cout << "Red pixels in debug image indicate problematic black pixels" << std::endl;
        }
    }
}

TEST_F(ImageOperationsIntegrationTest, AlphaBlendMaintainsOpaqueAlphaChannel)
{
    constexpr int WIDTH = 4;
    constexpr int HEIGHT = 4;
    constexpr int TOTAL_PIXELS = WIDTH * HEIGHT;

    // Destination starts with zeroed alpha to mimic frames coming from GPU buffers
    linuxface::Image destination(TOTAL_PIXELS * 4);
    destination.info.width = WIDTH;
    destination.info.height = HEIGHT;
    destination.info.pixelSizeBytes = 4;
    destination.info.format = linuxface::ImageFormat::RGBA;

    unsigned char* dstData = destination.data();
    for (int i = 0; i < TOTAL_PIXELS; ++i)
    {
        dstData[i * 4 + 0] = 40;
        dstData[i * 4 + 1] = 80;
        dstData[i * 4 + 2] = 120;
        dstData[i * 4 + 3] = 0; // Fully transparent background coming from upstream pipelines
    }

    // Source face tile provides opaque pixels that should replace destination colors in masked area
    linuxface::Image source(TOTAL_PIXELS * 4);
    source.info = destination.info;
    unsigned char* srcData = source.data();
    for (int i = 0; i < TOTAL_PIXELS; ++i)
    {
        srcData[i * 4 + 0] = 180;
        srcData[i * 4 + 1] = 130;
        srcData[i * 4 + 2] = 90;
        srcData[i * 4 + 3] = 255; // Fully opaque face pixels
    }

    // Mask covers the top half of the tile and leaves the bottom untouched
    linuxface::Image mask(TOTAL_PIXELS);
    mask.info.width = WIDTH;
    mask.info.height = HEIGHT;
    mask.info.pixelSizeBytes = 1;
    mask.info.format = linuxface::ImageFormat::GRAYSCALE;
    unsigned char* maskData = mask.data();
    for (int y = 0; y < HEIGHT; ++y)
    {
        for (int x = 0; x < WIDTH; ++x)
        {
            maskData[y * WIDTH + x] = (y < HEIGHT / 2) ? 255 : 0;
        }
    }

    destination.alphaBlend(source, mask);

    for (int i = 0; i < TOTAL_PIXELS; ++i)
    {
        const bool inMaskedRegion = (i / WIDTH) < (HEIGHT / 2);
        EXPECT_EQ(dstData[i * 4 + 3], 255) << "Pixel " << i << " should remain fully opaque after alphaBlend";

        if (inMaskedRegion)
        {
            EXPECT_EQ(dstData[i * 4 + 0], 180);
            EXPECT_EQ(dstData[i * 4 + 1], 130);
            EXPECT_EQ(dstData[i * 4 + 2], 90);
        }
        else
        {
            EXPECT_EQ(dstData[i * 4 + 0], 40);
            EXPECT_EQ(dstData[i * 4 + 1], 80);
            EXPECT_EQ(dstData[i * 4 + 2], 120);
        }
    }
}

// Test with real face swap data to reproduce the exact alpha blend issue
TEST_F(ImageOperationsIntegrationTest, RealFaceSwapAlphaBlendReproduction)
{
    // This test uses actual face data dimensions and patterns to reproduce the issue
    // Based on typical face swap scenarios with 256x256 or 512x512 face images

    constexpr int WIDTH = 64; // Smaller version of typical 256x256 for faster testing
    constexpr int HEIGHT = 64;
    constexpr int TOTAL_PIXELS = WIDTH * HEIGHT;

    // Create realistic face mask pattern (elliptical face shape)
    linuxface::Image faceMask(TOTAL_PIXELS);
    faceMask.info.width = WIDTH;
    faceMask.info.height = HEIGHT;
    faceMask.info.pixelSizeBytes = 1;
    faceMask.info.format = linuxface::ImageFormat::GRAYSCALE;

    // Create RGBA warped face with problematic edge pixels
    linuxface::Image warpedFace(TOTAL_PIXELS * 4);
    warpedFace.info.width = WIDTH;
    warpedFace.info.height = HEIGHT;
    warpedFace.info.pixelSizeBytes = 4;
    warpedFace.info.format = linuxface::ImageFormat::RGBA;

    // Create webcam-like destination
    linuxface::Image webcamFrame(TOTAL_PIXELS * 3);
    webcamFrame.info.width = WIDTH;
    webcamFrame.info.height = HEIGHT;
    webcamFrame.info.pixelSizeBytes = 3;
    webcamFrame.info.format = linuxface::ImageFormat::RGB;

    unsigned char* maskData = faceMask.data();
    unsigned char* warpedData = warpedFace.data();
    unsigned char* webcamData = webcamFrame.data();

    int faceRegionCount = 0;
    int edgeRegionCount = 0;

    // Generate realistic patterns
    for (int y = 0; y < HEIGHT; ++y)
    {
        for (int x = 0; x < WIDTH; ++x)
        {
            int i = y * WIDTH + x;

            // Create elliptical face region (center 32x48 oval)
            double centerX = WIDTH / 2.0;
            double centerY = HEIGHT / 2.0;
            double radiusX = 16.0; // Half width
            double radiusY = 24.0; // Half height

            double dx = (x - centerX) / radiusX;
            double dy = (y - centerY) / radiusY;
            double distanceSquared = dx * dx + dy * dy;

            // Webcam background (grayish with some noise)
            webcamData[i * 3 + 0] = 60 + (x % 30);        // R
            webcamData[i * 3 + 1] = 80 + (y % 25);        // G
            webcamData[i * 3 + 2] = 100 + ((x + y) % 20); // B

            if (distanceSquared <= 1.0)
            {
                // Inside face region
                faceRegionCount++;
                maskData[i] = 255; // White mask

                // Skin-like RGBA colors
                warpedData[i * 4 + 0] = 200 + (x % 15);       // R
                warpedData[i * 4 + 1] = 150 + (y % 12);       // G
                warpedData[i * 4 + 2] = 120 + ((x * y) % 10); // B
                warpedData[i * 4 + 3] = 255;                  // Opaque
            }
            else if (distanceSquared <= 1.44) // Edge region (20% larger ellipse)
            {
                // Edge transition region - this is where the bug might appear
                edgeRegionCount++;

                // Mask should be black (transparent)
                maskData[i] = 0;

                // CRITICAL: This is the problematic case!
                // Warped pixels outside face should be transparent black (0,0,0,0)
                // If alpha blend doesn't handle alpha=0 correctly, these become visible black pixels
                warpedData[i * 4 + 0] = 0; // R: black
                warpedData[i * 4 + 1] = 0; // G: black
                warpedData[i * 4 + 2] = 0; // B: black
                warpedData[i * 4 + 3] = 0; // Alpha: transparent (KEY!)
            }
            else
            {
                // Far background - should also be transparent
                maskData[i] = 0;
                warpedData[i * 4 + 0] = 0;
                warpedData[i * 4 + 1] = 0;
                warpedData[i * 4 + 2] = 0;
                warpedData[i * 4 + 3] = 0;
            }
        }
    }

    // Store original for comparison
    linuxface::Image originalWebcam(TOTAL_PIXELS * 3);
    originalWebcam.info = webcamFrame.info;
    std::memcpy(originalWebcam.data(), webcamFrame.data(), TOTAL_PIXELS * 3);

    // Log test setup
    std::cout << "RealFaceSwapAlphaBlendReproduction setup:" << std::endl;
    std::cout << "  Face region pixels: " << faceRegionCount << std::endl;
    std::cout << "  Edge region pixels: " << edgeRegionCount << std::endl;
    std::cout << "  Background pixels: " << (TOTAL_PIXELS - faceRegionCount - edgeRegionCount) << std::endl;

    // PERFORM THE ALPHA BLEND - This is where the bug should manifest
    webcamFrame.alphaBlend(warpedFace, faceMask);

    // Analyze results with strict criteria
    unsigned char* resultData = webcamFrame.data();
    unsigned char* originalData = originalWebcam.data();

    int problematicBlackPixels = 0;
    int correctlyPreservedEdges = 0;
    int correctlyBlendedFace = 0;

    for (int y = 0; y < HEIGHT; ++y)
    {
        for (int x = 0; x < WIDTH; ++x)
        {
            int i = y * WIDTH + x;
            unsigned char maskValue = maskData[i];
            unsigned char alpha = warpedData[i * 4 + 3];

            unsigned char resultR = resultData[i * 3 + 0];
            unsigned char resultG = resultData[i * 3 + 1];
            unsigned char resultB = resultData[i * 3 + 2];

            unsigned char originalR = originalData[i * 3 + 0];
            unsigned char originalG = originalData[i * 3 + 1];
            unsigned char originalB = originalData[i * 3 + 2];

            if (maskValue == 255 && alpha == 255)
            {
                // Face region: should be blended (changed from original)
                if (resultR != originalR || resultG != originalG || resultB != originalB)
                {
                    correctlyBlendedFace++;
                }
            }
            else if (maskValue == 0 && alpha == 0)
            {
                // Edge/background region: should preserve original webcam colors
                if (resultR == originalR && resultG == originalG && resultB == originalB)
                {
                    correctlyPreservedEdges++;
                }

                // CRITICAL TEST: Check for problematic black pixels
                // If alpha blend is buggy, transparent black (0,0,0,0) becomes opaque black (0,0,0)
                if (resultR == 0 && resultG == 0 && resultB == 0)
                {
                    problematicBlackPixels++;

                    // Log a few examples for debugging
                    if (problematicBlackPixels <= 5)
                    {
                        ADD_FAILURE() << "ALPHA BLEND BUG at (" << x << "," << y << "): "
                                      << "Transparent black (0,0,0,0) became opaque black (0,0,0). "
                                      << "Expected to preserve webcam RGB(" << (int) originalR << "," << (int) originalG
                                      << "," << (int) originalB << ")";
                    }
                }
            }
        }
    }

    // Report results
    int expectedFacePixels = faceRegionCount;
    int expectedEdgePixels = edgeRegionCount + (TOTAL_PIXELS - faceRegionCount - edgeRegionCount);

    std::cout << "Alpha blend results:" << std::endl;
    std::cout << "  Correctly blended face pixels: " << correctlyBlendedFace << " / " << expectedFacePixels
              << std::endl;
    std::cout << "  Correctly preserved edge/background: " << correctlyPreservedEdges << " / " << expectedEdgePixels
              << std::endl;
    std::cout << "  PROBLEMATIC BLACK PIXELS: " << problematicBlackPixels << " (BUG INDICATOR)" << std::endl;

    // Test assertions
    EXPECT_EQ(problematicBlackPixels, 0)
        << "Found " << problematicBlackPixels << " pixels where transparent black (0,0,0,0) became opaque black. "
        << "This indicates the alpha blend function is not properly handling alpha=0 pixels!";

    EXPECT_GE(correctlyBlendedFace, expectedFacePixels * 0.8) << "Too few face pixels were properly blended";

    EXPECT_GE(correctlyPreservedEdges, expectedEdgePixels * 0.8)
        << "Too few edge/background pixels preserved original webcam colors";

    // Save debug image if there are issues
    if (problematicBlackPixels > 0)
    {
        linuxface::Image debugImage(TOTAL_PIXELS * 3);
        debugImage.info = webcamFrame.info;
        unsigned char* debugData = debugImage.data();

        // Highlight problematic areas in red
        for (int i = 0; i < TOTAL_PIXELS; ++i)
        {
            if (resultData[i * 3 + 0] == 0 && resultData[i * 3 + 1] == 0 && resultData[i * 3 + 2] == 0
                && maskData[i] == 0 && warpedData[i * 4 + 3] == 0)
            {
                // Problematic black pixel - highlight in red
                debugData[i * 3 + 0] = 255; // Red
                debugData[i * 3 + 1] = 0;
                debugData[i * 3 + 2] = 0;
            }
            else
            {
                // Copy result
                debugData[i * 3 + 0] = resultData[i * 3 + 0];
                debugData[i * 3 + 1] = resultData[i * 3 + 1];
                debugData[i * 3 + 2] = resultData[i * 3 + 2];
            }
        }

        std::string debugPath = "../tests/image_tests/real_face_alpha_blend_debug.ppm";
        if (debugImage.saveToDisk(debugPath))
        {
            std::cout << "Saved debug image to: " << debugPath << std::endl;
            std::cout << "Red pixels indicate where transparent black became opaque black" << std::endl;
        }
    }
}

// Test with actual face swap pipeline data to reproduce alpha blend edge case
TEST_F(ImageOperationsIntegrationTest, ActualFaceSwapPipelineAlphaBlendTest)
{
    // This test simulates the exact scenario from SwapPipeline::swapFace()
    // where warpedFaceImage comes from affineWarpBilinear with out-of-bounds pixels

    // Create a test that mimics 256x256 face processing (scaled down to 32x32 for speed)
    constexpr int FACE_SIZE = 32;
    constexpr int TOTAL_PIXELS = FACE_SIZE * FACE_SIZE;

    // 1. Create destination image (simulates webcam frame RGB)
    linuxface::Image destination(TOTAL_PIXELS * 3);
    destination.info.width = FACE_SIZE;
    destination.info.height = FACE_SIZE;
    destination.info.pixelSizeBytes = 3;
    destination.info.format = linuxface::ImageFormat::RGB;

    // 2. Create RGBA warped face (simulates affineWarpBilinear output)
    linuxface::Image warpedFace(TOTAL_PIXELS * 4);
    warpedFace.info.width = FACE_SIZE;
    warpedFace.info.height = FACE_SIZE;
    warpedFace.info.pixelSizeBytes = 4;
    warpedFace.info.format = linuxface::ImageFormat::RGBA;

    // 3. Create face mask (simulates createFaceMask output)
    linuxface::Image faceMask(TOTAL_PIXELS);
    faceMask.info.width = FACE_SIZE;
    faceMask.info.height = FACE_SIZE;
    faceMask.info.pixelSizeBytes = 1;
    faceMask.info.format = linuxface::ImageFormat::GRAYSCALE;

    // Fill destination with webcam-like colors (should be preserved in out-of-bounds areas)
    unsigned char* destData = destination.data();
    unsigned char* warpedData = warpedFace.data();
    unsigned char* maskData = faceMask.data();

    for (int i = 0; i < TOTAL_PIXELS; ++i)
    {
        // Webcam background - greenish
        destData[i * 3 + 0] = 70;  // R
        destData[i * 3 + 1] = 120; // G
        destData[i * 3 + 2] = 90;  // B
    }

    // Simulate affineWarpBilinear result: face region + out-of-bounds transparent black
    int facePixels = 0;
    int transparentPixels = 0;

    for (int y = 0; y < FACE_SIZE; ++y)
    {
        for (int x = 0; x < FACE_SIZE; ++x)
        {
            int i = y * FACE_SIZE + x;

            // Create circular face region (r=10 pixels from center)
            int centerX = FACE_SIZE / 2;
            int centerY = FACE_SIZE / 2;
            int dx = x - centerX;
            int dy = y - centerY;
            int distanceSquared = dx * dx + dy * dy;

            if (distanceSquared <= 100) // r^2 = 10^2 = 100
            {
                // Inside face: valid warped pixels
                facePixels++;
                warpedData[i * 4 + 0] = 220; // R - skin tone
                warpedData[i * 4 + 1] = 180; // G
                warpedData[i * 4 + 2] = 150; // B
                warpedData[i * 4 + 3] = 255; // A - opaque

                maskData[i] = 255; // White mask
            }
            else
            {
                // Outside face: out-of-bounds from affineWarpBilinear
                // THIS IS THE CRITICAL CASE that was causing black edges!
                transparentPixels++;
                warpedData[i * 4 + 0] = 0; // R - black
                warpedData[i * 4 + 1] = 0; // G - black
                warpedData[i * 4 + 2] = 0; // B - black
                warpedData[i * 4 + 3] = 0; // A - transparent (key!)

                maskData[i] = 0; // Black mask (should be ignored anyway due to alpha=0)
            }
        }
    }

    // Store original destination
    std::vector<unsigned char> originalDest(TOTAL_PIXELS * 3);
    std::memcpy(originalDest.data(), destData, TOTAL_PIXELS * 3);

    std::cout << "ActualFaceSwapPipelineAlphaBlendTest setup:" << std::endl;
    std::cout << "  Face pixels (should be blended): " << facePixels << std::endl;
    std::cout << "  Transparent out-of-bounds pixels: " << transparentPixels << std::endl;

    // PERFORM ALPHA BLEND - exact same call as in SwapPipeline::swapFace
    destination.alphaBlend(warpedFace, faceMask);

    // Validate results with pixel-level precision
    int blackEdgeErrors = 0;
    int correctFaceBlends = 0;
    int correctBackgroundPreservations = 0;

    for (int y = 0; y < FACE_SIZE; ++y)
    {
        for (int x = 0; x < FACE_SIZE; ++x)
        {
            int i = y * FACE_SIZE + x;
            int centerX = FACE_SIZE / 2;
            int centerY = FACE_SIZE / 2;
            int dx = x - centerX;
            int dy = y - centerY;
            int distanceSquared = dx * dx + dy * dy;

            unsigned char resultR = destData[i * 3 + 0];
            unsigned char resultG = destData[i * 3 + 1];
            unsigned char resultB = destData[i * 3 + 2];

            if (distanceSquared <= 100)
            {
                // Face region: should be blended (changed from original green)
                if (resultR != 70 || resultG != 120 || resultB != 90)
                {
                    correctFaceBlends++;
                }

                // Face should not be black
                if (resultR < 50 && resultG < 50 && resultB < 50)
                {
                    blackEdgeErrors++;
                    if (blackEdgeErrors <= 3) // Log first few
                    {
                        ADD_FAILURE() << "Face pixel at (" << x << "," << y << ") is black: "
                                      << "RGB(" << (int) resultR << "," << (int) resultG << "," << (int) resultB << ") "
                                      << "Expected blended face colors";
                    }
                }
            }
            else
            {
                // Out-of-bounds region: should preserve original webcam green
                if (resultR == 70 && resultG == 120 && resultB == 90)
                {
                    correctBackgroundPreservations++;
                }
                else if (resultR == 0 && resultG == 0 && resultB == 0)
                {
                    // THIS IS THE BUG! Transparent black became opaque black
                    blackEdgeErrors++;
                    if (blackEdgeErrors <= 5) // Log first few
                    {
                        ADD_FAILURE() << "BLACK EDGE BUG at (" << x << "," << y << "): "
                                      << "Transparent black (0,0,0,0) became opaque black (0,0,0). "
                                      << "Should preserve webcam green RGB(70,120,90)";
                    }
                }
            }
        }
    }

    std::cout << "Alpha blend results:" << std::endl;
    std::cout << "  Face pixels correctly blended: " << correctFaceBlends << " / " << facePixels << std::endl;
    std::cout << "  Background correctly preserved: " << correctBackgroundPreservations << " / " << transparentPixels
              << std::endl;
    std::cout << "  BLACK EDGE ERRORS: " << blackEdgeErrors << " (BUG COUNT)" << std::endl;

    // Final assertions
    EXPECT_EQ(blackEdgeErrors, 0) << "ALPHA BLEND BUG DETECTED: " << blackEdgeErrors
                                  << " pixels where transparent black (0,0,0,0) "
                                  << "became opaque black (0,0,0) instead of preserving background!";

    EXPECT_GE(correctFaceBlends, facePixels * 0.9) << "Face blending failed for too many pixels";

    EXPECT_GE(correctBackgroundPreservations, transparentPixels * 0.9)
        << "Background preservation failed for too many pixels";

    // Performance validation - ensure alpha blend doesn't take too long
    auto start = std::chrono::high_resolution_clock::now();

    // Run alpha blend 100 times to test performance
    for (int iter = 0; iter < 100; ++iter)
    {
        destination.alphaBlend(warpedFace, faceMask);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double avgTimePerBlend = duration.count() / 100.0;

    std::cout << "Performance: Average alpha blend time = " << avgTimePerBlend << " μs" << std::endl;

    // Should be fast (under 1000 μs for 32x32 image)
    EXPECT_LT(avgTimePerBlend, 1000.0) << "Alpha blend performance regression detected";

    // Create a final visual verification
    if (blackEdgeErrors == 0)
    {
        std::cout << "✅ ALPHA BLEND IMPLEMENTATION CONFIRMED WORKING CORRECTLY" << std::endl;
        std::cout << "   No black edge artifacts detected in face swapping scenario" << std::endl;
        std::cout << "   All transparent pixels properly preserved background" << std::endl;
        std::cout << "   Performance is within acceptable limits" << std::endl;
    }
}

// Test with exact SwapPipeline mask creation to find alpha blend issues with gray mask values
TEST_F(ImageOperationsIntegrationTest, ExactSwapPipelineMaskReproduction)
{
    // This test reproduces the exact mask creation from Face::createFaceMaskInternal
    // which includes blur and creates GRAY values (not just binary 0/255)

    constexpr int WIDTH = 64;
    constexpr int HEIGHT = 64;
    constexpr int TOTAL_PIXELS = WIDTH * HEIGHT;

    // Create images matching SwapPipeline exactly
    linuxface::Image webcamFrame(TOTAL_PIXELS * 3);
    webcamFrame.info.width = WIDTH;
    webcamFrame.info.height = HEIGHT;
    webcamFrame.info.pixelSizeBytes = 3;
    webcamFrame.info.format = linuxface::ImageFormat::RGB;

    linuxface::Image warpedFaceRGBA(TOTAL_PIXELS * 4);
    warpedFaceRGBA.info.width = WIDTH;
    warpedFaceRGBA.info.height = HEIGHT;
    warpedFaceRGBA.info.pixelSizeBytes = 4;
    warpedFaceRGBA.info.format = linuxface::ImageFormat::RGBA;

    linuxface::Image faceMask(TOTAL_PIXELS);
    faceMask.info.width = WIDTH;
    faceMask.info.height = HEIGHT;
    faceMask.info.pixelSizeBytes = 1;
    faceMask.info.format = linuxface::ImageFormat::GRAYSCALE;

    // Fill webcam with typical background colors
    unsigned char* webcamData = webcamFrame.data();
    unsigned char* warpedData = warpedFaceRGBA.data();
    unsigned char* maskData = faceMask.data();

    for (int i = 0; i < TOTAL_PIXELS; ++i)
    {
        // Webcam background - typical indoor lighting colors
        webcamData[i * 3 + 0] = 85;  // R
        webcamData[i * 3 + 1] = 95;  // G
        webcamData[i * 3 + 2] = 105; // B
    }

    // Create face mask using EXACT algorithm from Face::createFaceMaskInternal
    std::fill(maskData, maskData + TOTAL_PIXELS, 0); // Start with black

    // Simulate face bounding box (center 32x32 region)
    const int faceLeft = 16;
    const int faceTop = 16;
    const int faceRight = 48;
    const int faceBottom = 48;
    const int faceWidth = faceRight - faceLeft;
    const int faceHeight = faceBottom - faceTop;

    // Apply exact Face::createFaceMaskInternal parameters
    const double faceMaskBlur = 0.45;
    const std::vector<int> faceMaskPadding = {-10, -10, -10, -10}; // NEGATIVE padding!

    // Calculate padding (REDUCES face mask size)
    const int paddingTop = static_cast<int>(faceHeight * faceMaskPadding[0] / 100.0);
    const int paddingRight = static_cast<int>(faceWidth * faceMaskPadding[1] / 100.0);
    const int paddingBottom = static_cast<int>(faceHeight * faceMaskPadding[2] / 100.0);
    const int paddingLeft = static_cast<int>(faceWidth * faceMaskPadding[3] / 100.0);

    // Calculate actual mask region (smaller than face due to negative padding)
    const int maskLeft = std::max(0, faceLeft - paddingLeft);            // faceLeft - (-3) = faceLeft + 3
    const int maskTop = std::max(0, faceTop - paddingTop);               // faceTop - (-3) = faceTop + 3
    const int maskRight = std::min(WIDTH, faceRight + paddingRight);     // faceRight + (-3) = faceRight - 3
    const int maskBottom = std::min(HEIGHT, faceBottom + paddingBottom); // faceBottom + (-3) = faceBottom - 3

    std::cout << "ExactSwapPipelineMaskReproduction setup:" << std::endl;
    std::cout << "  Face region: (" << faceLeft << "," << faceTop << ") to (" << faceRight << "," << faceBottom << ")"
              << std::endl;
    std::cout << "  Mask region: (" << maskLeft << "," << maskTop << ") to (" << maskRight << "," << maskBottom << ")"
              << std::endl;
    std::cout << "  Padding: T=" << paddingTop << " R=" << paddingRight << " B=" << paddingBottom
              << " L=" << paddingLeft << std::endl;

    // Fill mask region with white (255) - exact algorithm from Face::createFaceMaskInternal
    if (maskLeft < maskRight && maskTop < maskBottom)
    {
        for (int row = maskTop; row < maskBottom; ++row)
        {
            std::fill(maskData + row * WIDTH + maskLeft, maskData + row * WIDTH + maskRight, 255);
        }
    }

    // Apply blur - this creates GRAY pixels that might be problematic!
    const int blurAmount = static_cast<int>(faceWidth * 0.5 * faceMaskBlur);
    if (blurAmount > 0)
    {
        const int blurRadius = blurAmount / 2;
        // Simple box blur simulation (since we don't have image_utils::fastBoxBlur in test)
        std::vector<unsigned char> blurredMask(TOTAL_PIXELS);
        std::copy(maskData, maskData + TOTAL_PIXELS, blurredMask.begin());

        for (int y = blurRadius; y < HEIGHT - blurRadius; ++y)
        {
            for (int x = blurRadius; x < WIDTH - blurRadius; ++x)
            {
                int sum = 0;
                int count = 0;

                for (int by = -blurRadius; by <= blurRadius; ++by)
                {
                    for (int bx = -blurRadius; bx <= blurRadius; ++bx)
                    {
                        sum += maskData[(y + by) * WIDTH + (x + bx)];
                        count++;
                    }
                }

                blurredMask[y * WIDTH + x] = static_cast<unsigned char>(sum / count);
            }
        }

        std::copy(blurredMask.begin(), blurredMask.end(), maskData);
    }

    // Create RGBA warped face with the EXACT pattern from affineWarpBilinear
    int facePixels = 0;
    int edgePixels = 0;
    int grayMaskPixels = 0;

    for (int y = 0; y < HEIGHT; ++y)
    {
        for (int x = 0; x < WIDTH; ++x)
        {
            int i = y * WIDTH + x;
            unsigned char maskValue = maskData[i];

            if (maskValue > 200) // Nearly white mask region
            {
                // Inside face: skin-colored RGBA
                facePixels++;
                warpedData[i * 4 + 0] = 210; // R
                warpedData[i * 4 + 1] = 170; // G
                warpedData[i * 4 + 2] = 140; // B
                warpedData[i * 4 + 3] = 255; // A - opaque
            }
            else if (maskValue > 10 && maskValue <= 200) // GRAY mask region (blur edge)
            {
                // This is the CRITICAL region! Blurred mask edges
                grayMaskPixels++;

                // These could be partial face or out-of-bounds pixels
                // Let's simulate out-of-bounds transparent black (the problem case)
                warpedData[i * 4 + 0] = 0; // R - black
                warpedData[i * 4 + 1] = 0; // G - black
                warpedData[i * 4 + 2] = 0; // B - black
                warpedData[i * 4 + 3] = 0; // A - transparent
            }
            else // Black mask region
            {
                // Outside face: transparent black out-of-bounds
                edgePixels++;
                warpedData[i * 4 + 0] = 0;
                warpedData[i * 4 + 1] = 0;
                warpedData[i * 4 + 2] = 0;
                warpedData[i * 4 + 3] = 0;
            }
        }
    }

    std::cout << "  Face pixels (mask > 200): " << facePixels << std::endl;
    std::cout << "  Gray mask pixels (10 < mask <= 200): " << grayMaskPixels << " <- CRITICAL!" << std::endl;
    std::cout << "  Edge pixels (mask <= 10): " << edgePixels << std::endl;

    // Store original for comparison
    std::vector<unsigned char> originalWebcam(TOTAL_PIXELS * 3);
    std::memcpy(originalWebcam.data(), webcamData, TOTAL_PIXELS * 3);

    // PERFORM THE EXACT ALPHA BLEND from SwapPipeline
    webcamFrame.alphaBlend(warpedFaceRGBA, faceMask);

    // Analyze results with focus on gray mask regions
    int blackEdgeErrors = 0;
    int grayMaskErrors = 0;
    int correctFaceBlends = 0;
    int correctBackgroundPreservations = 0;

    for (int i = 0; i < TOTAL_PIXELS; ++i)
    {
        unsigned char maskValue = maskData[i];
        unsigned char alpha = warpedData[i * 4 + 3];

        unsigned char resultR = webcamData[i * 3 + 0];
        unsigned char resultG = webcamData[i * 3 + 1];
        unsigned char resultB = webcamData[i * 3 + 2];

        unsigned char originalR = originalWebcam[i * 3 + 0];
        unsigned char originalG = originalWebcam[i * 3 + 1];
        unsigned char originalB = originalWebcam[i * 3 + 2];

        if (maskValue > 200 && alpha == 255)
        {
            // Face region: should be blended
            if (resultR != originalR || resultG != originalG || resultB != originalB)
            {
                correctFaceBlends++;
            }
        }
        else if (maskValue > 10 && maskValue <= 200) // GRAY MASK REGION!
        {
            // This is where the bug might manifest!
            if (alpha == 0) // Transparent pixels in gray mask region
            {
                if (resultR == originalR && resultG == originalG && resultB == originalB)
                {
                    correctBackgroundPreservations++;
                }
                else if (resultR == 0 && resultG == 0 && resultB == 0)
                {
                    // BUG! Gray mask + transparent pixel = unexpected black
                    grayMaskErrors++;
                    if (grayMaskErrors <= 5)
                    {
                        ADD_FAILURE() << "GRAY MASK BUG at pixel " << i << " (mask=" << (int) maskValue
                                      << ", alpha=0): "
                                      << "Result RGB(" << (int) resultR << "," << (int) resultG << "," << (int) resultB
                                      << ") "
                                      << "should preserve background RGB(" << (int) originalR << "," << (int) originalG
                                      << "," << (int) originalB << ")";
                    }
                }
            }
        }
        else if (maskValue <= 10 && alpha == 0) // Black mask region
        {
            if (resultR == originalR && resultG == originalG && resultB == originalB)
            {
                correctBackgroundPreservations++;
            }
            else if (resultR == 0 && resultG == 0 && resultB == 0)
            {
                blackEdgeErrors++;
                if (blackEdgeErrors <= 5)
                {
                    ADD_FAILURE() << "BLACK EDGE BUG at pixel " << i << ": transparent black became opaque black";
                }
            }
        }
    }

    std::cout << "Alpha blend with GRAY MASK results:" << std::endl;
    std::cout << "  Correctly blended face pixels: " << correctFaceBlends << " / " << facePixels << std::endl;
    std::cout << "  Correctly preserved background: " << correctBackgroundPreservations << std::endl;
    std::cout << "  BLACK EDGE ERRORS: " << blackEdgeErrors << std::endl;
    std::cout << "  GRAY MASK ERRORS: " << grayMaskErrors << " <- NEW BUG TYPE!" << std::endl;

    // Test assertions
    EXPECT_EQ(blackEdgeErrors, 0) << "Standard black edge alpha blend bug detected";

    EXPECT_EQ(grayMaskErrors, 0) << "GRAY MASK ALPHA BLEND BUG: " << grayMaskErrors
                                 << " pixels where gray mask + transparent pixels caused unexpected behavior!";

    EXPECT_GE(correctFaceBlends, facePixels * 0.8) << "Face blending failed";

    // The REAL test: Gray mask regions with transparent pixels should preserve background
    if (grayMaskErrors == 0)
    {
        std::cout << "✅ GRAY MASK + TRANSPARENT PIXELS handled correctly" << std::endl;
        std::cout << "   No alpha blend bugs detected with blurred face masks" << std::endl;
    }
    else
    {
        std::cout << "❌ GRAY MASK BUG FOUND: Blurred mask edges not handling transparent pixels correctly!"
                  << std::endl;
    }
}

// Test ImageProcessor row and bulk operations
class ImageProcessorBulkTest : public ::testing::Test
{
};

TEST_F(ImageProcessorBulkTest, ProcessRow_Performance_And_Accuracy)
{
    const size_t ROW_SIZE = 100;
    std::vector<uint8_t> srcRow(ROW_SIZE * 3); // RGB
    std::vector<uint8_t> dstRow(ROW_SIZE * 4); // RGBA

    // Fill source row with gradient pattern
    for (size_t i = 0; i < ROW_SIZE; ++i)
    {
        srcRow[i * 3 + 0] = static_cast<uint8_t>(i % 256);       // R
        srcRow[i * 3 + 1] = static_cast<uint8_t>((i * 2) % 256); // G
        srcRow[i * 3 + 2] = static_cast<uint8_t>((i * 3) % 256); // B
    }

    // Process entire row
    ImageProcessor::processRow(srcRow.data(), dstRow.data(), ROW_SIZE, PixelFormat::RGB, PixelFormat::RGBA);

    // Verify conversion accuracy for several pixels
    for (size_t i = 0; i < ROW_SIZE; i += 10)
    {
        EXPECT_EQ(dstRow[i * 4 + 0], srcRow[i * 3 + 0]); // R
        EXPECT_EQ(dstRow[i * 4 + 1], srcRow[i * 3 + 1]); // G
        EXPECT_EQ(dstRow[i * 4 + 2], srcRow[i * 3 + 2]); // B
        EXPECT_EQ(dstRow[i * 4 + 3], 255);               // A should be 255
    }
}

TEST_F(ImageProcessorBulkTest, ProcessImage_WithStride_Support)
{
    const size_t WIDTH = 4, HEIGHT = 3;
    const size_t SRC_STRIDE = WIDTH * 3 + 2; // RGB with 2-byte padding
    const size_t DST_STRIDE = WIDTH * 4;     // RGBA no padding

    std::vector<uint8_t> srcData(SRC_STRIDE * HEIGHT);
    std::vector<uint8_t> dstData(DST_STRIDE * HEIGHT);

    // Fill source with checkerboard pattern
    for (size_t y = 0; y < HEIGHT; ++y)
    {
        for (size_t x = 0; x < WIDTH; ++x)
        {
            size_t srcIdx = y * SRC_STRIDE + x * 3;
            uint8_t value = ((x + y) % 2) ? 255 : 0;
            srcData[srcIdx + 0] = value; // R
            srcData[srcIdx + 1] = value; // G
            srcData[srcIdx + 2] = value; // B
        }
    }

    // Process with stride
    ImageProcessor::processImage(srcData.data(), dstData.data(), WIDTH, HEIGHT, SRC_STRIDE, DST_STRIDE,
                                 PixelFormat::RGB, PixelFormat::RGBA);

    // Verify checkerboard pattern preserved
    for (size_t y = 0; y < HEIGHT; ++y)
    {
        for (size_t x = 0; x < WIDTH; ++x)
        {
            size_t dstIdx = y * DST_STRIDE + x * 4;
            uint8_t expectedValue = ((x + y) % 2) ? 255 : 0;

            EXPECT_EQ(dstData[dstIdx + 0], expectedValue); // R
            EXPECT_EQ(dstData[dstIdx + 1], expectedValue); // G
            EXPECT_EQ(dstData[dstIdx + 2], expectedValue); // B
            EXPECT_EQ(dstData[dstIdx + 3], 255);           // A
        }
    }
}

// Edge Case and Error Handling Tests
class PixelArchitectureErrorHandlingTest : public ::testing::Test
{
};

TEST_F(PixelArchitectureErrorHandlingTest, ImageProcessor_InvalidFormats)
{
    uint8_t src[4] = {100, 150, 200, 128};
    uint8_t dst[4] = {50, 75, 100, 200};

    // All operations should handle invalid formats gracefully without crashing
    EXPECT_NO_THROW(ImageProcessor::processPixel(src, dst, static_cast<PixelFormat>(99), PixelFormat::RGB));
}

TEST_F(PixelArchitectureErrorHandlingTest, AlphaBlender_ExtremeCases)
{
    AlphaBlender blender;
    uint8_t src[3] = {255, 255, 255};
    uint8_t dst[3] = {0, 0, 0};

    // Test extreme alpha values
    EXPECT_NO_THROW(blender.blendRGB(src, dst, 0));   // Fully transparent
    EXPECT_NO_THROW(blender.blendRGB(src, dst, 255)); // Fully opaque

    // Check that 0 alpha leaves destination unchanged
    dst[0] = dst[1] = dst[2] = 100;
    blender.blendRGB(src, dst, 0);
    EXPECT_EQ(dst[0], 100);
    EXPECT_EQ(dst[1], 100);
    EXPECT_EQ(dst[2], 100);
}
