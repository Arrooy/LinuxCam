#include <gtest/gtest.h>

#include <cmath>

#include "LinuxFace/math_utils.h"

using namespace linuxface;

TEST(MathUtilsTest, Distance_Simple)
{
    double d = math_utils::distance(0, 0, 3, 4);
    EXPECT_DOUBLE_EQ(d, 5.0);
}

TEST(MathUtilsTest, Distance_SamePoint)
{
    double d = math_utils::distance(1, 1, 1, 1);
    EXPECT_DOUBLE_EQ(d, 0.0);
}
