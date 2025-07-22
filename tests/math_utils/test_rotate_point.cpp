#include <gtest/gtest.h>

#include <cmath>

#include "LinuxFace/math_utils.h"

using namespace linuxface;

TEST(MathUtilsTest, RotatePoint_ZeroAngle)
{
    math_utils::Point<long> pt(1, 2);
    math_utils::Point<long> origin(0, 0);
    auto rotated = math_utils::rotate_point(pt, origin, 0.0);
    EXPECT_EQ(rotated.x, 1);
    EXPECT_EQ(rotated.y, 2);
}

TEST(MathUtilsTest, RotatePoint_90Degrees)
{
    math_utils::Point<long> pt(1, 0);
    math_utils::Point<long> origin(0, 0);
    auto rotated = math_utils::rotate_point(pt, origin, M_PI / 2);
    EXPECT_NEAR(rotated.x, 0, 1e-6);
    EXPECT_NEAR(rotated.y, 1, 1e-6);
}

TEST(MathUtilsTest, RotatePoint_OriginShift)
{
    math_utils::Point<long> pt(2, 2);
    math_utils::Point<long> origin(1, 1);
    auto rotated = math_utils::rotate_point(pt, origin, M_PI);
    EXPECT_NEAR(rotated.x, 0, 1e-6);
    EXPECT_NEAR(rotated.y, 0, 1e-6);
}
