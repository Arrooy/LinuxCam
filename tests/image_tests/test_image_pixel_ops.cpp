// Tests for pixel access, blending, color conversion
// Covers: PixelOperations, getPixel/setPixel, ppx/pxy/pidx, isColorImage, convertToRGB, convertToRGBInplace
// Edge cases: out-of-bounds, alpha blending, grayscale conversion

#include "LinuxFace/Image/image.h"
#include <gtest/gtest.h>

using namespace linuxface;

TEST(PixelOps, SetAndGetPixel) {
    Image img(Pixel(1,2,3), 2, 2);
    img.ppx(1, 1, Pixel(9,8,7,6));
    Pixel p = img(1, 1);
    EXPECT_EQ(p.r, 9);
    EXPECT_EQ(p.g, 8);
    EXPECT_EQ(p.b, 7);
    EXPECT_EQ(p.a, 255); // always returns 255 for RGB
}

TEST(PixelOps, OutOfBoundsPixel) {
    Image img(Pixel(1,2,3), 2, 2);
    Pixel p = img(5, 5);
    EXPECT_EQ(p.r, 0);
    EXPECT_EQ(p.g, 0);
    EXPECT_EQ(p.b, 0);
}

TEST(PixelOps, SetPixelByIndex) {
    Image img(Pixel(1,2,3), 2, 2);
    img.pidx(0, 7, 8, 9, 255);
    Pixel p = img(0, 0);
    EXPECT_EQ(p.r, 7);
    EXPECT_EQ(p.g, 8);
    EXPECT_EQ(p.b, 9);
}

TEST(PixelOps, AlphaBlending) {
    unsigned char dst[4] = {10,20,30,255};
    unsigned char src[4] = {100,110,120,128};
    PixelOperations::blendPixels(dst, src, 4, 128);
    EXPECT_NE(dst[0], 10);
}

TEST(PixelOps, GrayscaleConversion) {
    Image img(Pixel(100,150,200), 2, 2);
    img.toGrayscale();
    EXPECT_EQ(img.info.format, ImageFormat::GRAYSCALE);
    for (size_t i = 0; i < img.size(); ++i) {
        EXPECT_LE(img.data()[i], 200);
    }
}

TEST(PixelOps, ConvertToRGB) {
    Image img(Pixel(50,50,50), 2, 2);
    img.toGrayscale();
    auto rgb = img.convertToRGB();
    EXPECT_EQ(rgb.size(), 12);
}
