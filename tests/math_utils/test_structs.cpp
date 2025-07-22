#include <gtest/gtest.h>

#include <cmath>

#include "LinuxFace/math_utils.h"

using namespace linuxface;

TEST(MathUtilsTest, Anchor_Construct)
{
    math_utils::Anchor a{1.5, 2.5, 4};
    EXPECT_DOUBLE_EQ(a.cx, 1.5);
    EXPECT_DOUBLE_EQ(a.cy, 2.5);
    EXPECT_EQ(a.stride, 4);
}

TEST(MathUtilsTest, Point_Construct)
{
    math_utils::Point<long> p1(3, 4);
    EXPECT_EQ(p1.x, 3);
    EXPECT_EQ(p1.y, 4);
    math_utils::Point<float> p2(1.5f, 2.5f);
    EXPECT_FLOAT_EQ(p2.x, 1.5f);
    EXPECT_FLOAT_EQ(p2.y, 2.5f);
}

TEST(MathUtilsTest, Point3D_Construct)
{
    math_utils::Point3D p1(1.0, 2.0, 3.0);
    EXPECT_DOUBLE_EQ(p1.x, 1.0);
    EXPECT_DOUBLE_EQ(p1.y, 2.0);
    EXPECT_DOUBLE_EQ(p1.z, 3.0);
    math_utils::Point3D p2(4.0, 5.0);
    EXPECT_DOUBLE_EQ(p2.z, 0.0);
}

TEST(MathUtilsTest, StridePoint_Construct)
{
    math_utils::StridePoint s(1.0, 2.0, 3.0);
    EXPECT_DOUBLE_EQ(s.cx, 1.0);
    EXPECT_DOUBLE_EQ(s.cy, 2.0);
    EXPECT_DOUBLE_EQ(s.stride, 3.0);
}

TEST(MathUtilsTest, Rect_Constructors)
{
    math_utils::Rect<long> r1(1, 2, 3, 4);
    EXPECT_EQ(r1.l, 1);
    EXPECT_EQ(r1.t, 2);
    EXPECT_EQ(r1.r, 3);
    EXPECT_EQ(r1.b, 4);
    math_utils::Point<long> p(5, 6);
    math_utils::Rect<long> r2(p, 7, 8);
    EXPECT_EQ(r2.l, 5);
    EXPECT_EQ(r2.t, 6);
    EXPECT_EQ(r2.r, 12);
    EXPECT_EQ(r2.b, 14);
    math_utils::Rect<long> r3(p, math_utils::Point<long>(8, 9));
    EXPECT_EQ(r3.l, 5);
    EXPECT_EQ(r3.t, 6);
    EXPECT_EQ(r3.r, 8);
    EXPECT_EQ(r3.b, 9);
}

TEST(MathUtilsTest, Rect_Methods)
{
    math_utils::Rect<long> r(1, 2, 4, 5);
    EXPECT_EQ(r.x(), 1);
    EXPECT_EQ(r.y(), 2);
    EXPECT_EQ(r.width(), 4 - 1 + 1);
    EXPECT_EQ(r.height(), 5 - 2 + 1);
    EXPECT_TRUE(r.contains(2, 3));
    EXPECT_FALSE(r.contains(0, 0));
    math_utils::Point<long> p(2, 3);
    EXPECT_TRUE(r.contains(p));
    EXPECT_FALSE(r.contains(math_utils::Point<long>(0, 0)));
    EXPECT_TRUE(r.isWithinBounds(10, 10));
    EXPECT_FALSE(r.isWithinBounds(2, 2));
    r.addPadding(1, 1, 1, 1);
    EXPECT_EQ(r.l, 0);
    EXPECT_EQ(r.t, 1);
    EXPECT_EQ(r.r, 5);
    EXPECT_EQ(r.b, 6);
}
