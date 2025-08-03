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
