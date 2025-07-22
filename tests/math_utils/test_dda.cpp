#include <gtest/gtest.h>

#include "LinuxFace/math_utils.h"

TEST(MathUtilsTest, DDA_Simple)
{
    auto points = linuxface::math_utils::DDA(0.0, 0.0, 2.0, 2.0);
    ASSERT_EQ(points.size(), 3);
    EXPECT_EQ(points[0].x, 0);
    EXPECT_EQ(points[0].y, 0);
    EXPECT_EQ(points[1].x, 1);
    EXPECT_EQ(points[1].y, 1);
    EXPECT_EQ(points[2].x, 2);
    EXPECT_EQ(points[2].y, 2);
}

TEST(MathUtilsTest, DDA_Horizontal)
{
    auto points = linuxface::math_utils::DDA(0.0, 0.0, 3.0, 0.0);
    ASSERT_EQ(points.size(), 4);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(points[i].x, i);
        EXPECT_EQ(points[i].y, 0);
    }
}

TEST(MathUtilsTest, DDA_Vertical)
{
    auto points = linuxface::math_utils::DDA(0.0, 0.0, 0.0, 3.0);
    ASSERT_EQ(points.size(), 4);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(points[i].x, 0);
        EXPECT_EQ(points[i].y, i);
    }
}

TEST(MathUtilsTest, DDA_Negative)
{
    auto points = linuxface::math_utils::DDA(2.0, 2.0, 0.0, 0.0);
    ASSERT_EQ(points.size(), 3);
    EXPECT_EQ(points[0].x, 2);
    EXPECT_EQ(points[0].y, 2);
    EXPECT_EQ(points[1].x, 1);
    EXPECT_EQ(points[1].y, 1);
    EXPECT_EQ(points[2].x, 0);
    EXPECT_EQ(points[2].y, 0);
}

TEST(MathUtilsTest, DDA_FloatInput)
{
    auto points = linuxface::math_utils::DDA(0.5f, 0.5f, 2.5f, 2.5f);
    ASSERT_EQ(points.size(), 3);
    EXPECT_EQ(points[0].x, 1);
    EXPECT_EQ(points[0].y, 1);
    EXPECT_EQ(points[1].x, 2);
    EXPECT_EQ(points[1].y, 2);
    EXPECT_EQ(points[2].x, 2);
    EXPECT_EQ(points[2].y, 2);
}
