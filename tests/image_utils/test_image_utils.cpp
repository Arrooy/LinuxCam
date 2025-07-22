#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/math_utils.h"

using namespace linuxface::image_utils;
using linuxface::Pixel;
using linuxface::math_utils::Point;
using linuxface::math_utils::Point3D;


TEST(ImageUtilsAffineTransformTest, TransformPointsAffine)
{
    std::vector<Point<>> pts = {
        {1, 2},
        {3, 4},
        {5, 6}
    };
    double M[6] = {1, 0, 1, 0, 1, 2}; // x' = x+1, y' = y+2
    auto out = transform_points_affine(pts, M);
    EXPECT_EQ(out.size(), 3);
    EXPECT_DOUBLE_EQ(out[0].x, 2);
    EXPECT_DOUBLE_EQ(out[0].y, 4);
    EXPECT_DOUBLE_EQ(out[2].x, 6);
    EXPECT_DOUBLE_EQ(out[2].y, 8);
}

TEST(ImageUtilsAffineTransformTest, TransformPairsAffine)
{
    std::vector<std::pair<double, double>> pts = {
        {1, 2},
        {3, 4}
    };
    double M[6] = {2, 0, 0, 0, 2, 0}; // x' = 2x, y' = 2y
    auto out = transform_points_affine(pts, M);
    EXPECT_EQ(out.size(), 2);
    EXPECT_DOUBLE_EQ(out[1].first, 6);
    EXPECT_DOUBLE_EQ(out[1].second, 8);
}

TEST(ImageUtilsMaskTest, CreateStaticBoxMaskBasic)
{
    std::vector<double> crop_size = {10, 10};
    auto mask = create_static_box_mask(crop_size);
    ASSERT_NE(mask, nullptr);
    EXPECT_EQ(mask->info.width, 10);
    EXPECT_EQ(mask->info.height, 10);
    EXPECT_EQ(mask->info.format, linuxface::ImageFormat::GRAYSCALE);
    // Check all values are 255 or blurred (not zero everywhere)
    int nonzero = 0;
    for (int i = 0; i < 100; ++i)
    {
        nonzero += mask->data()[i] > 0 ? 1 : 0;
    }
    EXPECT_GT(nonzero, 0);
}

TEST(ImageUtilsMaskTest, CreateStaticBoxMaskEdgeCases)
{
    std::vector<double> crop_size = {0, 0};
    auto mask = create_static_box_mask(crop_size);
    EXPECT_EQ(mask->info.width, 0);
    EXPECT_EQ(mask->info.height, 0);
}

TEST(ImageUtilsBlurTest, FastBoxBlurNoop)
{
    unsigned char src[4] = {255, 0, 255, 0};
    unsigned char dst[4] = {0, 0, 0, 0};
    fast_box_blur(src, dst, 2, 2, 0); // radius 0
    EXPECT_EQ(dst[0], 255);
    EXPECT_EQ(dst[1], 0);
    EXPECT_EQ(dst[2], 255);
    EXPECT_EQ(dst[3], 0);
}

TEST(ImageUtilsMathTest, CubicHermiteBasic)
{
    float v = cubicHermite(0, 1, 2, 3, 0.5f);
    EXPECT_NEAR(v, 1.5f, 0.1f); // Should interpolate between B and C
}

TEST(ImageUtilsPaintTest, PaintCircle)
{
    // Properly construct a 10x10 RGB image with black background
    Pixel black{0, 0, 0, 255};
    auto img = std::make_unique<linuxface::Image>(black, 10, 10);
    Point3D center{5, 5, 0};
    Pixel color{255, 255, 255, 255};
    paintCircle(img, center, 2.0f, color);
    // Check at least one pixel is painted
    int painted = 0;
    for (int i = 0; i < img->size(); ++i)
    {
        painted += img->data()[i] == 255 ? 1 : 0;
    }
    EXPECT_GT(painted, 0);
}

TEST(ImageUtilsKernelTest, LanczosKernelRadius)
{
    EXPECT_EQ(LanczosKernel::RADIUS, 3);
}

// Edge case: empty points
TEST(ImageUtilsEdgeCaseTest, TransformPointsAffineEmpty)
{
    std::vector<Point<>> pts;
    double M[6] = {1, 0, 0, 0, 1, 0};
    auto out = transform_points_affine(pts, M);
    EXPECT_TRUE(out.empty());
}
