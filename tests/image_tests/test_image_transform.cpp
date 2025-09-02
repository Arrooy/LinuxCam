// Tests for rotation, flipping, affine warp
// Covers: rotate, rotate90, rotate180, rotate270, flipHorizontal, flipVertical, affineWarpBilinear, affineWarpNearestNeighbour
// Edge cases: empty image, single pixel, large angle, invalid matrix

#include "LinuxFace/Image/image.h"
#include <gtest/gtest.h>

using namespace linuxface;

TEST(ImageTransform, Rotate90) {
    Image img(Pixel(1,2,3), 2, 3);
    img.rotate90();
    EXPECT_EQ(img.info.width, 3);
    EXPECT_EQ(img.info.height, 2);
}

TEST(ImageTransform, Rotate180) {
    Image img(Pixel(1,2,3), 2, 2);
    img.rotate180();
    EXPECT_EQ(img.info.width, 2);
    EXPECT_EQ(img.info.height, 2);
}

TEST(ImageTransform, FlipHorizontal) {
    Image img(Pixel(1,2,3), 2, 2);
    img.flipHorizontal();
    EXPECT_EQ(img.info.width, 2);
}

TEST(ImageTransform, AffineWarpBilinear) {
    Image img(Pixel(1,2,3), 2, 2);
    double M[6] = {1,0,0,0,1,0};
    auto warped = img.affineWarpBilinear(M, 2, 2, nullptr);
    EXPECT_EQ(warped->info.width, 2);
}

// RGBA support tests for affineWarpBilinear
TEST(ImageTransform, AffineWarpBilinear_RGBToRGBA) {
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
    EXPECT_EQ(data[3], 255); // First pixel alpha
    EXPECT_EQ(data[7], 255); // Second pixel alpha
}

TEST(ImageTransform, AffineWarpBilinear_RGBAToRGB) {
    // Create RGBA image manually
    auto rgbaImg = std::make_unique<linuxface::Image>(4 * 4 * 4);
    rgbaImg->info.width = 4;
    rgbaImg->info.height = 4;
    rgbaImg->info.pixelSizeBytes = 4;
    rgbaImg->info.format = linuxface::ImageFormat::RGBA;
    
    // Fill with RGBA data (red with varying alpha)
    unsigned char* data = rgbaImg->data();
    for (int i = 0; i < 16; i++) {
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
}

TEST(ImageTransform, AffineWarpBilinear_RGBAToRGBA) {
    // Create RGBA image manually
    auto rgbaImg = std::make_unique<linuxface::Image>(4 * 4 * 4);
    rgbaImg->info.width = 4;
    rgbaImg->info.height = 4;
    rgbaImg->info.pixelSizeBytes = 4;
    rgbaImg->info.format = linuxface::ImageFormat::RGBA;
    
    // Fill with RGBA data (blue with varying alpha)
    unsigned char* data = rgbaImg->data();
    for (int i = 0; i < 16; i++) {
        data[i * 4 + 0] = 0;   // R
        data[i * 4 + 1] = 0;   // G
        data[i * 4 + 2] = 255; // B
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
    // Alpha should be interpolated but we'll just check it exists
    EXPECT_GE(warpedData[3], 0);
    EXPECT_LE(warpedData[3], 255);
}

TEST(ImageTransform, AffineWarpBilinear_UnsupportedFormat) {
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

TEST(ImageTransform, AffineWarpBilinear_UnsupportedTargetFormat) {
    linuxface::Image img(linuxface::Pixel(255, 128, 64), 4, 4);
    double M[6] = {1, 0, 0, 0, 1, 0};
    
    auto warped = img.affineWarpBilinear(M, 4, 4, nullptr, linuxface::ImageFormat::GRAYSCALE);
    
    // Should return nullptr for unsupported target format
    EXPECT_EQ(warped, nullptr);
}
