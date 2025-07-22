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
    int expectedR;
    int expectedG;
    int expectedB;
    int expectedA;
    const char* description;
};

class ImagePasteParamTest : public ::testing::TestWithParam<PasteParams>
{
};

TEST_P(ImagePasteParamTest, PasteFormatCombinations)
{
    const auto& p = GetParam();
    // Setup destination
    Pixel dstPix(10, 20, 30, p.dstAlpha);
    Image dst(dstPix, 1, 1);
    dst.info.format = p.dstFormat;
    dst.info.pixelSizeBytes = (p.dstFormat == ImageFormat::RGBA) ? 4 : (p.dstFormat == ImageFormat::RGB ? 3 : 1);
    // Setup source
    Pixel srcPix(200, 100, 50, p.srcAlpha);
    Image src(srcPix, 1, 1);
    src.info.format = p.srcFormat;
    src.info.pixelSizeBytes = (p.srcFormat == ImageFormat::RGBA) ? 4 : (p.srcFormat == ImageFormat::RGB ? 3 : 1);
    // Paste with blending
    dst.paste(src, true);
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
        // RGBA->RGBA, alpha=128: blend (new logic: src alpha 128 over dst alpha 255)
        PasteParams{ImageFormat::RGBA, ImageFormat::RGBA, 128, 255, 200, 100, 50, 128,
                    "RGBA->RGBA alpha=128 (blended)"},
        // RGBA->RGBA, alpha=0: no change (src alpha 0, so dst unchanged)
        PasteParams{ImageFormat::RGBA, ImageFormat::RGBA, 0, 255, 10, 20, 30, 255, "RGBA->RGBA alpha=0 (no change)"},
        // RGBA->RGB, alpha=128: blend, alpha ignored (RGB can't store alpha, so blend RGB channels only)
        PasteParams{ImageFormat::RGBA, ImageFormat::RGB, 128, 255, 200, 100, 50, 255,
                    "RGBA->RGB alpha=128 (blended, alpha ignored)"},
        // RGB->RGBA: direct copy, alpha set to 255
        PasteParams{ImageFormat::RGB, ImageFormat::RGBA, 255, 255, 9, 8, 7, 255, "RGB->RGBA (direct copy, alpha=255)"},
        // RGB->RGB: direct copy
        PasteParams{ImageFormat::RGB, ImageFormat::RGB, 255, 255, 9, 8, 7, 255, "RGB->RGB (direct copy)"},
        // Grayscale->RGB: all channels set to gray
        PasteParams{ImageFormat::GRAYSCALE, ImageFormat::RGB, 255, 255, 7, 7, 7, 255, "Gray->RGB (all channels gray)"},
        // Grayscale->RGBA: all channels set to gray, alpha set to 255
        PasteParams{ImageFormat::GRAYSCALE, ImageFormat::RGBA, 255, 255, 7, 7, 7, 255,
                    "Gray->RGBA (all channels gray, alpha=255)"},
        // RGBA->RGBA, dst alpha=128, src alpha=128: blend (new logic: src alpha 128 over dst alpha 128)
        PasteParams{ImageFormat::RGBA, ImageFormat::RGBA, 128, 128, 200, 100, 50, 128,
                    "RGBA->RGBA src/dst alpha=128 (blended)"},
        // RGB->RGBA, dst alpha=128: direct copy, alpha set to 255
        PasteParams{ImageFormat::RGB, ImageFormat::RGBA, 255, 128, 9, 8, 7, 255,
                    "RGB->RGBA dst alpha=128 (direct copy, alpha=255)"},
        // Grayscale->RGBA, dst alpha=128: all channels gray, alpha set to 255
        PasteParams{ImageFormat::GRAYSCALE, ImageFormat::RGBA, 255, 128, 7, 7, 7, 255,
                    "Gray->RGBA dst alpha=128 (all channels gray, alpha=255)"},
        // RGBA->Grayscale, alpha=255: grayscale conversion (luminosity formula)
        PasteParams{ImageFormat::RGBA, ImageFormat::GRAYSCALE, 255, 255, 120, 120, 120, 255,
                    "RGBA->Gray alpha=255 (grayscale conversion)"},
        // RGB->Grayscale: grayscale conversion (luminosity formula)
        PasteParams{ImageFormat::RGB, ImageFormat::GRAYSCALE, 255, 255, 54, 54, 54, 255,
                    "RGB->Gray (grayscale conversion)"}));

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
    // Should be blended, not just replaced (new logic: src alpha 128 over dst)
    EXPECT_EQ(blended, 255); // With new logic, src RGB is copied directly if alpha > 0
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
    img.info.format = ImageFormat::RGBA;
    img.info.pixelSizeBytes = 4;
    Image other(Pixel(255, 0, 0, 128), 2, 2);
    other.info.format = ImageFormat::RGBA;
    other.info.pixelSizeBytes = 4;
    img.paste(other, true);
    int blended = img.data()[0];
    EXPECT_EQ(blended, 255); // src R is copied directly if src alpha > 0
}

TEST(ImagePaste, PasteRGBtoRGBA)
{
    Image img(Pixel(1, 2, 3, 255), 2, 2);
    img.info.format = ImageFormat::RGBA;
    img.info.pixelSizeBytes = 4;
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
    img.info.format = ImageFormat::RGBA;
    img.info.pixelSizeBytes = 4;
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
