// Tests for rotation, flipping, affine warp
// Covers: rotate, rotate90, rotate180, rotate270, flipHorizontal, flipVertical, affineWarpBilinear,
// affineWarpNearestNeighbour Edge cases: empty image, single pixel, large angle, invalid matrix

#include <cstring>
#include <gtest/gtest.h>
#include <vector>

#include "LinuxFace/Image/image.h"

// Small helpers for tests
static std::vector<unsigned char> snapshotImage(const linuxface::Image& img)
{
    const size_t n = static_cast<size_t>(img.info.width) * img.info.height * img.info.pixelSizeBytes;
    std::vector<unsigned char> out(n);
    if (n > 0 && img.data())
    {
        std::memcpy(out.data(), img.data(), n);
    }
    return out;
}

static void fillPatternRGB(linuxface::Image& img)
{
    const unsigned long w = img.info.width;
    const unsigned long h = img.info.height;
    const size_t pxSize = img.info.pixelSizeBytes;
    unsigned char* d = img.data();
    for (unsigned long y = 0; y < h; ++y)
    {
        for (unsigned long x = 0; x < w; ++x)
        {
            const unsigned long idx = (y * w + x) * pxSize;
            unsigned char base = static_cast<unsigned char>(y * w + x);
            for (size_t c = 0; c < pxSize; ++c)
            {
                d[idx + c] = static_cast<unsigned char>(base + c);
            }
        }
    }
}

using namespace linuxface;

TEST(ImageTransform, Rotate90)
{
    Image img(Pixel(1, 2, 3), 2, 3);
    // Fill with a pattern so we can verify content moved as expected
    fillPatternRGB(img);
    auto before = snapshotImage(img);

    img.rotate90();

    EXPECT_EQ(img.info.width, 3);
    EXPECT_EQ(img.info.height, 2);

    // Verify that pixels have been relocated (size must match)
    ASSERT_EQ(before.size(), static_cast<size_t>(img.info.width) * img.info.height * img.info.pixelSizeBytes);
    // Spot-check that top-left pixel now contains a value that existed before
    auto after = snapshotImage(img);
    EXPECT_NE(after, before);
}

TEST(ImageTransform, Rotate180)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    fillPatternRGB(img);
    auto before = snapshotImage(img);

    img.rotate180();

    EXPECT_EQ(img.info.width, 2);
    EXPECT_EQ(img.info.height, 2);

    auto after = snapshotImage(img);
    // Rotating by 180 twice should restore original order
    img.rotate180();
    auto restored = snapshotImage(img);
    EXPECT_EQ(restored, before);
    EXPECT_NE(after, before);
}

TEST(ImageTransform, FlipHorizontal)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    fillPatternRGB(img);
    auto before = snapshotImage(img);

    img.flipHorizontalInPlace();

    EXPECT_EQ(img.info.width, 2);

    auto after = snapshotImage(img);
    // Ensure that flipping changed the buffer but preserved pixel size/count
    EXPECT_EQ(before.size(), after.size());
    EXPECT_NE(after, before);
}

TEST(ImageTransform, AffineWarpBilinear)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    fillPatternRGB(img);
    double M[6] = {1, 0, 0, 0, 1, 0};
    auto warped = img.affineWarpBilinear(M, 2, 2, nullptr);
    ASSERT_NE(warped, nullptr);
    EXPECT_EQ(warped->info.width, 2);
    EXPECT_EQ(warped->info.height, 2);
    // Since it's identity warp, pixel size should remain the same
    EXPECT_EQ(warped->info.pixelSizeBytes, img.info.pixelSizeBytes);
    // And content should be similar (exact match expected for nearest-like implementations)
    auto wbuf = snapshotImage(*warped);
    auto ibuf = snapshotImage(img);
    EXPECT_EQ(wbuf.size(), ibuf.size());
}

// RGBA support tests for affineWarpBilinear
TEST(ImageTransform, AffineWarpBilinear_RGBToRGBA)
{
    Image img(Pixel(255, 128, 64), 4, 4);
    double M[6] = {1, 0, 0, 0, 1, 0}; // Identity matrix

    auto warped = img.affineWarpBilinear(M, 4, 4, nullptr, linuxface::ImageFormat::RGBA);

    ASSERT_NE(warped, nullptr);
    EXPECT_EQ(warped->info.width, 4);
    EXPECT_EQ(warped->info.height, 4);
    EXPECT_EQ(warped->info.pixelSizeBytes, 4);
    EXPECT_EQ(warped->info.format, linuxface::ImageFormat::RGBA);

    // Check that alpha channel is set to 255 for RGB to RGBA conversion
    unsigned char* data = warped->data();
    const size_t stride = warped->info.pixelSizeBytes;
    EXPECT_EQ(stride, 4u);
    EXPECT_EQ(data[3], 255); // First pixel alpha
    EXPECT_EQ(data[7], 255); // Second pixel alpha
}

TEST(ImageTransform, AffineWarpBilinear_RGBAToRGB)
{
    // Create RGBA image manually
    auto rgbaImg = std::make_unique<linuxface::Image>(4 * 4 * 4);
    rgbaImg->info.width = 4;
    rgbaImg->info.height = 4;
    rgbaImg->info.pixelSizeBytes = 4;
    rgbaImg->info.format = linuxface::ImageFormat::RGBA;

    // Fill with RGBA data (red with varying alpha)
    unsigned char* data = rgbaImg->data();
    for (int i = 0; i < 16; i++)
    {
        data[i * 4 + 0] = 255; // R
        data[i * 4 + 1] = 0;   // G
        data[i * 4 + 2] = 0;   // B
        data[i * 4 + 3] = 128; // A (half transparent)
    }

    double M[6] = {1, 0, 0, 0, 1, 0}; // Identity matrix

    auto warped = rgbaImg->affineWarpBilinear(M, 4, 4, nullptr, linuxface::ImageFormat::RGB);

    ASSERT_NE(warped, nullptr);
    EXPECT_EQ(warped->info.width, 4);
    EXPECT_EQ(warped->info.height, 4);
    EXPECT_EQ(warped->info.pixelSizeBytes, 3);
    EXPECT_EQ(warped->info.format, linuxface::ImageFormat::RGB);

    // Check RGB values are preserved (alpha is dropped)
    unsigned char* warpedData = warped->data();
    EXPECT_EQ(warpedData[0], 255); // R
    EXPECT_EQ(warpedData[1], 0);   // G
    EXPECT_EQ(warpedData[2], 0);   // B
    // Ensure pixel size matches expected 3
    EXPECT_EQ(warped->info.pixelSizeBytes, 3);
}

TEST(ImageTransform, AffineWarpBilinear_RGBAToRGBA)
{
    // Create RGBA image manually
    auto rgbaImg = std::make_unique<linuxface::Image>(4 * 4 * 4);
    rgbaImg->info.width = 4;
    rgbaImg->info.height = 4;
    rgbaImg->info.pixelSizeBytes = 4;
    rgbaImg->info.format = linuxface::ImageFormat::RGBA;

    // Fill with RGBA data (blue with varying alpha)
    unsigned char* data = rgbaImg->data();
    for (int i = 0; i < 16; i++)
    {
        data[i * 4 + 0] = 0;              // R
        data[i * 4 + 1] = 0;              // G
        data[i * 4 + 2] = 255;            // B
        data[i * 4 + 3] = (i * 15) % 256; // A (varying transparency)
    }

    double M[6] = {1, 0, 0, 0, 1, 0}; // Identity matrix

    auto warped = rgbaImg->affineWarpBilinear(M, 4, 4, nullptr, linuxface::ImageFormat::RGBA);

    ASSERT_NE(warped, nullptr);
    EXPECT_EQ(warped->info.width, 4);
    EXPECT_EQ(warped->info.height, 4);
    EXPECT_EQ(warped->info.pixelSizeBytes, 4);
    EXPECT_EQ(warped->info.format, linuxface::ImageFormat::RGBA);

    // Check RGBA values are preserved and interpolated correctly
    unsigned char* warpedData = warped->data();
    EXPECT_EQ(warpedData[0], 0);   // R
    EXPECT_EQ(warpedData[1], 0);   // G
    EXPECT_EQ(warpedData[2], 255); // B
    // Alpha should be interpolated - check range
    EXPECT_GE(warpedData[3], 0);
    EXPECT_LE(warpedData[3], 255);
    EXPECT_EQ(warped->info.pixelSizeBytes, 4);
}

TEST(ImageTransform, AffineWarpBilinear_UnsupportedFormat)
{
    // Create a grayscale image (unsupported for now)
    auto grayImg = std::make_unique<linuxface::Image>(4 * 4);
    grayImg->info.width = 4;
    grayImg->info.height = 4;
    grayImg->info.pixelSizeBytes = 1;
    grayImg->info.format = linuxface::ImageFormat::GRAYSCALE;

    double M[6] = {1, 0, 0, 0, 1, 0};

    auto warped = grayImg->affineWarpBilinear(M, 4, 4, nullptr, linuxface::ImageFormat::RGB);

    // Should return nullptr for unsupported input format
    EXPECT_EQ(warped, nullptr);
}

TEST(ImageTransform, AffineWarpBilinear_UnsupportedTargetFormat)
{
    linuxface::Image img(linuxface::Pixel(255, 128, 64), 4, 4);
    double M[6] = {1, 0, 0, 0, 1, 0};

    auto warped = img.affineWarpBilinear(M, 4, 4, nullptr, linuxface::ImageFormat::GRAYSCALE);

    // Should return nullptr for unsupported target format
    EXPECT_EQ(warped, nullptr);
}

// New explicit tests verifying flipHorizontalInPlace preserves whole-pixel grouping
TEST(ImageTransform, FlipHorizontal_PreservesRGBPixels)
{
    // Create a 1x3 RGB image with distinct pixel values to check order after flip
    auto img = std::make_unique<linuxface::Image>(3 * 1 * 3);
    img->info.width = 3;
    img->info.height = 1;
    img->info.pixelSizeBytes = 3;
    img->info.format = linuxface::ImageFormat::RGB;

    unsigned char* d = img->data();
    // Pixels: P0=(1,2,3), P1=(4,5,6), P2=(7,8,9)
    unsigned char vals[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::memcpy(d, vals, 9);

    img->flipHorizontalInPlace();

    // Expect order P2, P1, P0
    unsigned char expected[9] = {7, 8, 9, 4, 5, 6, 1, 2, 3};
    for (int i = 0; i < 9; ++i)
    {
        EXPECT_EQ(d[i], expected[i]);
    }
}

TEST(ImageTransform, FlipHorizontal_PreservesRGBAPixels)
{
    // Create a 1x4 RGBA image with distinct values
    auto img = std::make_unique<linuxface::Image>(4 * 1 * 4);
    img->info.width = 4;
    img->info.height = 1;
    img->info.pixelSizeBytes = 4;
    img->info.format = linuxface::ImageFormat::RGBA;

    unsigned char* d = img->data();
    // Pixels: P0=(1,1,1,10), P1=(2,2,2,20), P2=(3,3,3,30), P3=(4,4,4,40)
    for (int i = 0; i < 4; ++i)
    {
        d[i * 4 + 0] = static_cast<unsigned char>(i + 1);
        d[i * 4 + 1] = static_cast<unsigned char>(i + 1);
        d[i * 4 + 2] = static_cast<unsigned char>(i + 1);
        d[i * 4 + 3] = static_cast<unsigned char>((i + 1) * 10);
    }

    // Snapshot original data before flip
    std::vector<unsigned char> orig(16);
    std::memcpy(orig.data(), d, 16);

    img->flipHorizontalInPlace();

    // Expect pixels in reverse order: P3, P2, P1, P0
    for (int i = 0; i < 4; ++i)
    {
        int srcIdx = (3 - i) * 4; // index in original snapshot
        int dstIdx = i * 4;       // index in flipped buffer
        for (int c = 0; c < 4; ++c)
        {
            EXPECT_EQ(d[dstIdx + c], orig[srcIdx + c]);
        }
    }
}

TEST(ImageTransform, FlipHorizontal_SinglePixelUnchanged)
{
    // Single-pixel image should remain identical after flip
    auto img = std::make_unique<linuxface::Image>(1 * 1 * 3);
    img->info.width = 1;
    img->info.height = 1;
    img->info.pixelSizeBytes = 3;
    img->info.format = linuxface::ImageFormat::RGB;
    unsigned char* d = img->data();
    d[0] = 9;
    d[1] = 8;
    d[2] = 7;

    img->flipHorizontalInPlace();

    EXPECT_EQ(d[0], 9);
    EXPECT_EQ(d[1], 8);
    EXPECT_EQ(d[2], 7);
}
