#include <gtest/gtest.h>

#include "LinuxFace/Image/image.h"

/**
 * Comprehensive test suite for Image coordinate mapping and clipping functionality
 *
 * Tests the copyPixelsWithBlending method through the public pasteAt interface,
 * focusing on coordinate mapping, clipping logic, and edge cases.
 */
class ImageCoordinateMappingTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create source image with distinctive pattern for easy verification
        createSourceImage();

        // Create destination image filled with black for contrast
        createDestinationImage();
    }

    void createSourceImage()
    {
        // 3x2 RGB source image with unique values per pixel
        sourceImage = linuxface::Image(3 * 2 * 3);
        sourceImage.info.width = 3;
        sourceImage.info.height = 2;
        sourceImage.info.format = linuxface::ImageFormat::RGB;
        sourceImage.info.pixelSizeBytes = 3;

        // Use a pattern where each pixel has (x, y*10, 50) for easy identification
        sourceImage.pxy(0, 0, 0, 0, 50, 255);   // Src(0,0) = (0,0,50)
        sourceImage.pxy(1, 0, 1, 10, 50, 255);  // Src(1,0) = (1,10,50)
        sourceImage.pxy(2, 0, 2, 20, 50, 255);  // Src(2,0) = (2,20,50)
        sourceImage.pxy(0, 1, 10, 1, 50, 255);  // Src(0,1) = (10,1,50)
        sourceImage.pxy(1, 1, 11, 11, 51, 255); // Src(1,1) = (11,11,51)
        sourceImage.pxy(2, 1, 12, 21, 52, 255); // Src(2,1) = (12,21,52)
    }

    void createDestinationImage()
    {
        // 6x5 RGB destination image filled with black
        destImage = linuxface::Image(6 * 5 * 3);
        destImage.info.width = 6;
        destImage.info.height = 5;
        destImage.info.format = linuxface::ImageFormat::RGB;
        destImage.info.pixelSizeBytes = 3;
        destImage.black();
    }

    void verifyPixelMapping(int destX, int destY, int srcX, int srcY, const std::string& testName,
                            const linuxface::Image& srcImage = {})
    {
        auto destPixel = destImage(destX, destY);
        // Use provided source image, or default to class member
        const linuxface::Image& refSrcImage = srcImage.data() ? srcImage : sourceImage;
        auto srcPixel = refSrcImage(srcX, srcY);

        EXPECT_EQ(destPixel.r, srcPixel.r) << testName << " - Dest(" << destX << "," << destY << ") should map to Src("
                                           << srcX << "," << srcY << ") - R channel mismatch";
        EXPECT_EQ(destPixel.g, srcPixel.g) << testName << " - Dest(" << destX << "," << destY << ") should map to Src("
                                           << srcX << "," << srcY << ") - G channel mismatch";
        EXPECT_EQ(destPixel.b, srcPixel.b) << testName << " - Dest(" << destX << "," << destY << ") should map to Src("
                                           << srcX << "," << srcY << ") - B channel mismatch";
    }

    void verifyPixelIsBlack(int x, int y, const std::string& testName)
    {
        auto pixel = destImage(x, y);
        EXPECT_EQ(pixel.r, 0) << testName << " - Pixel(" << x << "," << y << ") should be black";
        EXPECT_EQ(pixel.g, 0) << testName << " - Pixel(" << x << "," << y << ") should be black";
        EXPECT_EQ(pixel.b, 0) << testName << " - Pixel(" << x << "," << y << ") should be black";
    }

    linuxface::Image sourceImage;
    linuxface::Image destImage;
};

// Test 1: Basic paste at origin - all pixels should be copied correctly
TEST_F(ImageCoordinateMappingTest, PasteAtOrigin)
{
    destImage.pasteAt(sourceImage, 0, 0, false);

    // Verify complete source is copied to destination
    verifyPixelMapping(0, 0, 0, 0, "PasteAtOrigin");
    verifyPixelMapping(1, 0, 1, 0, "PasteAtOrigin");
    verifyPixelMapping(2, 0, 2, 0, "PasteAtOrigin");
    verifyPixelMapping(0, 1, 0, 1, "PasteAtOrigin");
    verifyPixelMapping(1, 1, 1, 1, "PasteAtOrigin");
    verifyPixelMapping(2, 1, 2, 1, "PasteAtOrigin");

    // Verify untouched areas remain black
    verifyPixelIsBlack(3, 0, "PasteAtOrigin");
    verifyPixelIsBlack(0, 2, "PasteAtOrigin");
    verifyPixelIsBlack(5, 4, "PasteAtOrigin");
}

// Test 2: Paste with offset - verify coordinate mapping works correctly
TEST_F(ImageCoordinateMappingTest, PasteWithOffset)
{
    destImage.pasteAt(sourceImage, 2, 1, false);

    // Source (0,0) should go to dest (2,1)
    verifyPixelMapping(2, 1, 0, 0, "PasteWithOffset");
    verifyPixelMapping(3, 1, 1, 0, "PasteWithOffset");
    verifyPixelMapping(4, 1, 2, 0, "PasteWithOffset");
    verifyPixelMapping(2, 2, 0, 1, "PasteWithOffset");
    verifyPixelMapping(3, 2, 1, 1, "PasteWithOffset");
    verifyPixelMapping(4, 2, 2, 1, "PasteWithOffset");

    // Verify areas outside paste region remain black
    verifyPixelIsBlack(0, 0, "PasteWithOffset");
    verifyPixelIsBlack(1, 1, "PasteWithOffset");
    verifyPixelIsBlack(5, 1, "PasteWithOffset");
    verifyPixelIsBlack(0, 3, "PasteWithOffset");
}

// Test 3: Partial overlap on right edge - rightmost pixels should be clipped
TEST_F(ImageCoordinateMappingTest, PartialOverlapRight)
{
    destImage.pasteAt(sourceImage, 4, 1, false);

    // Only pixels within destination bounds should be copied
    // Source width=3, paste at x=4, dest width=6
    // So dest positions 4,5 are valid, 6 would be out of bounds
    verifyPixelMapping(4, 1, 0, 0, "PartialOverlapRight");
    verifyPixelMapping(5, 1, 1, 0, "PartialOverlapRight");
    verifyPixelMapping(4, 2, 0, 1, "PartialOverlapRight");
    verifyPixelMapping(5, 2, 1, 1, "PartialOverlapRight");

    // Verify clipped areas remain black
    verifyPixelIsBlack(0, 1, "PartialOverlapRight");
    verifyPixelIsBlack(3, 1, "PartialOverlapRight");

    // Note: We can't test x=6 as it would be out of bounds for the destination image
}

// Test 4: Partial overlap on bottom edge - bottom pixels should be clipped
TEST_F(ImageCoordinateMappingTest, PartialOverlapBottom)
{
    destImage.pasteAt(sourceImage, 1, 4, false);

    // Source height=2, paste at y=4, dest height=5
    // So dest positions with y=4 are valid, y=5 would be out of bounds
    verifyPixelMapping(1, 4, 0, 0, "PartialOverlapBottom");
    verifyPixelMapping(2, 4, 1, 0, "PartialOverlapBottom");
    verifyPixelMapping(3, 4, 2, 0, "PartialOverlapBottom");

    // Bottom row of source would be clipped (y=5 is out of bounds)
    // Verify untouched areas remain black
    verifyPixelIsBlack(0, 4, "PartialOverlapBottom");
    verifyPixelIsBlack(4, 4, "PartialOverlapBottom");
    verifyPixelIsBlack(1, 3, "PartialOverlapBottom");
}

// Test 5: Paste completely outside bounds - no pixels should be copied
TEST_F(ImageCoordinateMappingTest, CompletelyOutOfBounds)
{
    destImage.pasteAt(sourceImage, 10, 10, false);

    // Entire image should remain black
    for (int y = 0; y < 5; y++)
    {
        for (int x = 0; x < 6; x++)
        {
            verifyPixelIsBlack(x, y, "CompletelyOutOfBounds");
        }
    }
}

// Test 6: Paste at negative position - only visible part should be copied
TEST_F(ImageCoordinateMappingTest, NegativePosition)
{
    destImage.pasteAt(sourceImage, -1, -1, false);

    // Only the bottom-right portion of source should be visible
    // Source (-1,-1) means source (1,1) goes to dest (0,0)
    verifyPixelMapping(0, 0, 1, 1, "NegativePosition");
    verifyPixelMapping(1, 0, 2, 1, "NegativePosition");

    // Verify rest remains black
    verifyPixelIsBlack(2, 0, "NegativePosition");
    verifyPixelIsBlack(0, 1, "NegativePosition");
}

// Test 7: Alpha blending with RGBA source
TEST_F(ImageCoordinateMappingTest, AlphaBlendingRGBA)
{
    // Create RGBA source with semi-transparent pixels
    linuxface::Image rgbaSource(2 * 2 * 4);
    rgbaSource.info.width = 2;
    rgbaSource.info.height = 2;
    rgbaSource.info.format = linuxface::ImageFormat::RGBA;
    rgbaSource.info.pixelSizeBytes = 4;

    rgbaSource.pxy(0, 0, 255, 0, 0, 128);     // Semi-transparent red
    rgbaSource.pxy(1, 0, 0, 255, 0, 128);     // Semi-transparent green
    rgbaSource.pxy(0, 1, 0, 0, 255, 128);     // Semi-transparent blue
    rgbaSource.pxy(1, 1, 255, 255, 255, 255); // Opaque white

    // Fill destination with known values for blending verification
    destImage.pxy(1, 1, 100, 100, 100, 255);
    destImage.pxy(2, 1, 100, 100, 100, 255);
    destImage.pxy(1, 2, 100, 100, 100, 255);
    destImage.pxy(2, 2, 100, 100, 100, 255);

    destImage.pasteAt(rgbaSource, 1, 1, false);

    // Verify alpha blending occurred (semi-transparent pixels should blend)
    auto blendedPixel = destImage(1, 1);
    EXPECT_GT(blendedPixel.r, 100); // Should be blend of red and background
    EXPECT_LT(blendedPixel.r, 255); // Should not be pure red

    // Verify opaque pixel completely replaces destination
    auto opaquePixel = destImage(2, 2);
    EXPECT_EQ(opaquePixel.r, 255);
    EXPECT_EQ(opaquePixel.g, 255);
    EXPECT_EQ(opaquePixel.b, 255);
}

// Test 8: Edge case - 1x1 source image
TEST_F(ImageCoordinateMappingTest, SinglePixelSource)
{
    linuxface::Image singlePixel(1 * 1 * 3);
    singlePixel.info.width = 1;
    singlePixel.info.height = 1;
    singlePixel.info.format = linuxface::ImageFormat::RGB;
    singlePixel.info.pixelSizeBytes = 3;
    singlePixel.pxy(0, 0, 123, 45, 67, 255);

    destImage.pasteAt(singlePixel, 3, 2, false);

    verifyPixelMapping(3, 2, 0, 0, "SinglePixelSource", singlePixel);

    // Verify surrounding pixels remain black
    verifyPixelIsBlack(2, 2, "SinglePixelSource");
    verifyPixelIsBlack(4, 2, "SinglePixelSource");
    verifyPixelIsBlack(3, 1, "SinglePixelSource");
    verifyPixelIsBlack(3, 3, "SinglePixelSource");
}

// Test 9: Clipping with multiple edges - corner case
TEST_F(ImageCoordinateMappingTest, CornerClipping)
{
    destImage.pasteAt(sourceImage, 5, 4, false);

    // Only the top-left corner of source should be visible
    // Source (0,0) should go to dest (5,4)
    verifyPixelMapping(5, 4, 0, 0, "CornerClipping");

    // Everything else should remain black
    verifyPixelIsBlack(4, 4, "CornerClipping");
    verifyPixelIsBlack(5, 3, "CornerClipping");

    // Verify the rest of the destination is untouched
    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 5; x++)
        {
            verifyPixelIsBlack(x, y, "CornerClipping");
        }
    }
}

// Test 10: Zero-size clipping region
TEST_F(ImageCoordinateMappingTest, ZeroClipRegion)
{
    // Try to paste at exact boundary where no pixels would be visible
    destImage.pasteAt(sourceImage, 6, 5, false);

    // Entire destination should remain black
    for (int y = 0; y < 5; y++)
    {
        for (int x = 0; x < 6; x++)
        {
            verifyPixelIsBlack(x, y, "ZeroClipRegion");
        }
    }
}
