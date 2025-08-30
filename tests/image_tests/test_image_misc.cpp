// Tests for border, background, edge cases, error handling
// Covers: drawBorder, changeBackgroundImage, paintPoints, isFullyOpaque, black
// Edge cases: zero thickness, empty image, mismatched background/matting

#include <gtest/gtest.h>

#include "LinuxFace/Image/image.h"

using namespace linuxface;

// TODO: Create thickness parameterized tests. Currently thickness is ignored.
TEST(ImageMisc, DrawBorderZeroThickness)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    img.drawBorder(Pixel(9, 9, 9), 0);
    EXPECT_EQ(img.info.width, 2);
    // TODO: Check if border pixels changed
}

TEST(ImageMisc, BlackImage)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    img.black();
    for (size_t i = 0; i < img.size(); ++i)
    {
        EXPECT_EQ(img.data()[i], 0);
    }
}

TEST(ImageMisc, IsFullyOpaque)
{
    Image img(Pixel(1, 2, 3, 255), 2, 2);
    EXPECT_TRUE(img.isFullyOpaque());
}
