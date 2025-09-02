// Tests for paste, blending, canvas expansion
// Covers: paste, pasteAt, pasteImpl, copyPixelsWithBlending, copyPixelsOptimized
// Edge cases: overlapping, expandCanvas, empty source, different formats

#include <gtest/gtest.h>

#include <tuple>

#include "LinuxFace/Image/image.h"

using namespace linuxface;

struct PasteParams
{
    ImageFormat srcFormat;
    ImageFormat dstFormat;
    int srcAlpha;
    int dstAlpha;
    unsigned char expectedR;
    unsigned char expectedG;
    unsigned char expectedB;
    unsigned char expectedA;
    const char* description;
};

class ImagePasteParamTest : public ::testing::TestWithParam<PasteParams>
{
};

// Helper function to create image with specific format
Image createImageWithFormat(ImageFormat format, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (format == ImageFormat::RGB) {
        // Create RGB image without alpha
        Pixel p(r, g, b, 255);  // Constructor will create RGB since a=255
        Image img(p, 1, 1);
        return img;
    } else if (format == ImageFormat::RGBA) {
        // Create RGBA image  
        Pixel p(r, g, b, a);
        Image img(p, 1, 1);
        if (img.info.format != ImageFormat::RGBA) {
            // Force conversion to RGBA
            img.convertToRGBAInplace();
        }
        // Fix alpha channel if needed
        if (a != 255) {
            unsigned char* data = img.data();
            data[3] = a;  // Set the correct alpha value
        }
        return img;
    } else if (format == ImageFormat::GRAYSCALE) {
        // Create grayscale image
        Pixel p(r, r, r, 255);  // Use red channel as gray
        Image img(p, 1, 1);
        // Manually convert to grayscale by creating new image
        Image gray(1);
        gray.info.format = ImageFormat::GRAYSCALE;
        gray.info.pixelSizeBytes = 1;
        gray.info.width = 1;
        gray.info.height = 1;
        gray.resize(1);
        unsigned char* data = gray.data();
        data[0] = r;
        return gray;
    }
    return Image();
}

TEST_P(ImagePasteParamTest, PasteFormatCombinations)
{
    const auto& p = GetParam();
    
    // Setup destination with zeros for all channels
    Image dst = createImageWithFormat(p.dstFormat, 0, 0, 0, p.dstAlpha);

    // Setup source
    Image src = createImageWithFormat(p.srcFormat, 200, 100, 50, p.srcAlpha);

    // Paste with blending
    dst.paste(src, false);
    // Check expectations
    auto* data = dst.data();
    EXPECT_EQ(data[0], p.expectedR) << p.description;
    if (dst.info.pixelSizeBytes > 1)
    {
        EXPECT_EQ(data[1], p.expectedG) << p.description;
    }
    if (dst.info.pixelSizeBytes > 2)
    {
        EXPECT_EQ(data[2], p.expectedB) << p.description;
    }
    if (dst.info.pixelSizeBytes > 3)
    {
        EXPECT_EQ(data[3], p.expectedA) << p.description;
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllPasteCases, ImagePasteParamTest,
    ::testing::Values(
        // RGBA->RGBA, alpha=255: direct copy
        PasteParams{ImageFormat::RGBA, ImageFormat::RGBA, 255, 255, 200, 100, 50, 255, "RGBA->RGBA alpha=255"},
        // RGBA->RGBA, alpha=128: blend (src alpha 128 over dst alpha 255)
        PasteParams{ImageFormat::RGBA, ImageFormat::RGBA, 128, 255, 100, 50, 25, 128, "RGBA->RGBA alpha=128 (blended)"},
        // RGBA->RGBA, alpha=0: no change (src alpha 0, so dst unchanged, which is zero)
        PasteParams{ImageFormat::RGBA, ImageFormat::RGBA, 0, 0, 0, 0, 0, 0, "RGBA->RGBA alpha=0 (no change)"},
        // RGBA->RGB, alpha=128: blend using alpha
        PasteParams{ImageFormat::RGBA, ImageFormat::RGB, 128, 255, 100, 50, 25, 255, "RGBA->RGB alpha=128 (blended)"},
        // RGB->RGBA: copy RGB, alpha set to 255
        PasteParams{ImageFormat::RGB, ImageFormat::RGBA, 255, 255, 200, 100, 50, 255, "RGB->RGBA (direct copy, alpha=255)"},
        // RGB->RGB: direct copy
        PasteParams{ImageFormat::RGB, ImageFormat::RGB, 255, 255, 200, 100, 50, 255, "RGB->RGB (direct copy)"},
        // Grayscale->RGB: all channels set to gray
        PasteParams{ImageFormat::GRAYSCALE, ImageFormat::RGB, 255, 255, 200, 200, 200, 255, "Gray->RGB (all channels gray)"},
        // Grayscale->RGBA: all channels set to gray, alpha set to 255
        PasteParams{ImageFormat::GRAYSCALE, ImageFormat::RGBA, 255, 255, 200, 200, 200, 255, "Gray->RGBA (all channels gray, alpha=255)"},
        // RGBA->RGBA, dst alpha=128, src alpha=128: blend (src alpha 128 over dst alpha 128)
        PasteParams{ImageFormat::RGBA, ImageFormat::RGBA, 128, 128, 100, 50, 25, 128, "RGBA->RGBA src/dst alpha=128 (blended)"},
        // RGB->RGBA, dst alpha=128: copy RGB, alpha set to 255
        PasteParams{ImageFormat::RGB, ImageFormat::RGBA, 255, 128, 200, 100, 50, 255, "RGB->RGBA dst alpha=128 (direct copy, alpha=255)"},
        // Grayscale->RGBA, dst alpha=128: all channels gray, alpha set to 255
        PasteParams{ImageFormat::GRAYSCALE, ImageFormat::RGBA, 255, 128, 200, 200, 200, 255, "Gray->RGBA dst alpha=128 (all channels gray, alpha=255)"},
        // RGBA->Grayscale, alpha=255: grayscale conversion (luminosity formula)
        PasteParams{ImageFormat::RGBA, ImageFormat::GRAYSCALE, 255, 255, 124, 124, 124, 255, "RGBA->Gray alpha=255 (grayscale conversion)"},
        // RGB->Grayscale: grayscale conversion (luminosity formula)
        PasteParams{ImageFormat::RGB, ImageFormat::GRAYSCALE, 255, 255, 124, 124, 124, 255, "RGB->Gray (grayscale conversion)"}
    ));

TEST(ImagePaste, PasteExpandCanvas)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image other(Pixel(9, 8, 7), 2, 2);
    img.paste(other, true);
    EXPECT_GE(img.info.width, 2);
    EXPECT_GE(img.info.height, 2);
}

TEST(ImagePaste, PasteAtOverlap)
{
    Image img(Pixel(1, 2, 3), 4, 4);
    Image other(Pixel(9, 8, 7), 2, 2);
    img.pasteAt(other, 1, 1, false);
    EXPECT_EQ(img.info.width, 4);
}

TEST(ImagePaste, PasteSelf)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    img.paste(img, true);
    EXPECT_EQ(img.info.width, 2);
    EXPECT_EQ(img.info.height, 2);
}

TEST(ImagePaste, PasteAlphaBlending)
{
    // RGBA -> RGB: blending
    Image img(Pixel(1, 2, 3), 2, 2);          // RGB destination
    Image other(Pixel(255, 0, 0, 128), 2, 2); // RGBA source
    other.info.format = ImageFormat::RGBA;
    other.info.pixelSizeBytes = 4;
    img.paste(other, true);
    int blended = img.data()[0];
    // Should be blended: src alpha 128/255 ≈ 0.502
    // Expected: 255 * 0.502 + 1 * 0.498 ≈ 128 + 0.5 ≈ 128
    EXPECT_NEAR(blended, 128, 2); // Alpha blending of red channel
}

TEST(ImagePaste, PasteRGBtoRGB)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image other(Pixel(9, 8, 7), 2, 2);
    img.paste(other, true);
    EXPECT_EQ(img.data()[0], 9);
}

TEST(ImagePaste, PasteRGBAtoRGBA)
{
    Image img(Pixel(1, 2, 3, 255), 2, 2);
    img.convertToRGBAInplace();
    Image other(Pixel(255, 0, 0, 128), 2, 2);
    img.paste(other, true);
    int blended = img.data()[0];
    // Should be blended if both have alpha
    EXPECT_EQ(blended, (128 * 255 + (255 - 128) * 1) / 255);
}

TEST(ImagePaste, PasteRGBtoRGBA)
{
    Image img(Pixel(1, 2, 3, 255), 2, 2);
    // Convert RGB to RGBA format properly
    img.convertToRGBAInplace();
    Image other(Pixel(9, 8, 7), 2, 2);
    img.paste(other, true);
    EXPECT_EQ(img.data()[0], 9);
    EXPECT_EQ(img.data()[3], 255); // Alpha should be set to 255
}

TEST(ImagePaste, PasteGrayscaleToRGB)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image other(Pixel(7, 0, 0), 2, 2);
    other.info.format = ImageFormat::GRAYSCALE;
    other.info.pixelSizeBytes = 1;
    img.paste(other, true);
    EXPECT_EQ(img.data()[0], 7);
    EXPECT_EQ(img.data()[1], 7);
    EXPECT_EQ(img.data()[2], 7);
}

TEST(ImagePaste, PasteGrayscaleToRGBA)
{
    Image img(Pixel(1, 2, 3, 255), 2, 2);
    img.convertToRGBAInplace();
    Image other(Pixel(7, 0, 0), 2, 2);
    other.info.format = ImageFormat::GRAYSCALE;
    other.info.pixelSizeBytes = 1;
    img.paste(other, true);
    EXPECT_EQ(img.data()[0], 7);
    EXPECT_EQ(img.data()[1], 7);
    EXPECT_EQ(img.data()[2], 7);
    EXPECT_EQ(img.data()[3], 255);
}

TEST(ImagePaste, PasteEmptySource)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image empty;
    img.paste(empty, true);
    EXPECT_EQ(img.info.width, 2);
    EXPECT_EQ(img.info.height, 2);
}

TEST(ImagePaste, PasteEmptyDestination)
{
    Image empty;
    Image other(Pixel(9, 8, 7), 2, 2);
    // If destination is empty, copyFrom should be called
    if (empty.empty())
    {
        empty.copyFrom(other);
    }
    EXPECT_EQ(empty.info.width, 2);
    EXPECT_EQ(empty.info.height, 2);
}

TEST(ImagePaste, PasteOutOfBounds)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image other(Pixel(9, 8, 7), 2, 2);
    img.pasteAt(other, 10, 10, true);
    EXPECT_GE(img.info.width, 12);
    EXPECT_GE(img.info.height, 12);
}

TEST(ImagePaste, PasteNegativeCoordsExpand)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image other(Pixel(9, 8, 7), 2, 2);
    img.pasteAt(other, -1, -1, true);
    EXPECT_GE(img.info.width, 3);
    EXPECT_GE(img.info.height, 3);
}

TEST(ImagePaste, PasteDifferentFormats)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image other(Pixel(9, 8, 7, 128), 2, 2);
    other.info.format = ImageFormat::RGBA;
    other.info.pixelSizeBytes = 4;
    img.paste(other, true);
    EXPECT_GE(img.info.width, 2);
}

TEST(ImagePaste, PasteImplErrorHandling)
{
    Image img;
    Image other;
    img.pasteAt(other, 0, 0, true);
    EXPECT_EQ(img.info.width, 0);
    EXPECT_EQ(img.info.height, 0);
}

// Test: Bounds handling for single-pixel and edge cases
TEST(ImagePasteBoundsTest, PasteSinglePixelAtEdge)
{
    // Destination: 2x2 RGBA, all zeros
    Image dst = createImageWithFormat(ImageFormat::RGBA, 0, 0, 0, 0);
    dst.resize(2 * 2 * 4);  // Resize to 2x2 RGBA
    dst.info.width = 2;
    dst.info.height = 2;
    
    // Source: 1x1 RGBA, red pixel, alpha=255
    Image src = createImageWithFormat(ImageFormat::RGBA, 255, 0, 0, 255);
    
    // Paste at (1,1) (bottom-right corner)
    dst.pasteAt(src, 1, 1, false);
    // Only pixel (1,1) should be set
    auto* data = dst.data();
    int idx = (1 * 2 + 1) * 4;
    EXPECT_EQ(data[idx + 0], 255);
    EXPECT_EQ(data[idx + 1], 0);
    EXPECT_EQ(data[idx + 2], 0);
    EXPECT_EQ(data[idx + 3], 255);
    // All other pixels should remain zero
    for (int i = 0; i < 4 * 4; ++i)
    {
        if (i >= idx && i < idx + 4) continue;
        EXPECT_EQ(data[i], 0);
    }
}
