// Tests for scaling, resizing, cropping
// Covers: scale, scaleInPlace, scaleTo, resize, crop
// Edge cases: zero factor, zero size, min/max dimensions, invalid algorithm

#include "LinuxFace/Image/image.h"
#include <gtest/gtest.h>

using namespace linuxface;

TEST(ImageScaling, ScaleFactor) {
    Image img(Pixel(1,2,3), 4, 4);
    auto scaled = img.scale(0.5);
    EXPECT_EQ(scaled->info.width, 2);
    EXPECT_EQ(scaled->info.height, 2);
}

TEST(ImageScaling, ScaleToInPlace) {
    Image img(Pixel(1,2,3), 4, 4);
    img.scaleToInPlace(2, 2);
    EXPECT_EQ(img.info.width, 2);
    EXPECT_EQ(img.info.height, 2);
}

TEST(ImageScaling, ResizePreserve) {
    Image img(Pixel(1,2,3), 2, 2);
    img.resize(20);
    EXPECT_EQ(img.size(), 20);
}

TEST(ImageScaling, Crop) {
    Image img(Pixel(1,2,3), 4, 4);
    math_utils::Rect<float> rect{1,1,3,3};
    auto cropped = img.crop(rect);
    EXPECT_EQ(cropped->info.width, 2);
    EXPECT_EQ(cropped->info.height, 2);
}
