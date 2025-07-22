#include <gtest/gtest.h>

#include "LinuxFace/math_utils.h"

using namespace linuxface;

TEST(MathUtilsTest, IoU_IdenticalRects)
{
    math_utils::Rect<long> r1(0, 0, 10, 10);
    math_utils::Rect<long> r2(0, 0, 10, 10);
    float iou = math_utils::calculateIoU(r1, r2);
    EXPECT_FLOAT_EQ(iou, 1.0f);
}

TEST(MathUtilsTest, IoU_NoOverlap)
{
    math_utils::Rect<long> r1(0, 0, 10, 10);
    math_utils::Rect<long> r2(20, 20, 30, 30);
    float iou = math_utils::calculateIoU(r1, r2);
    EXPECT_FLOAT_EQ(iou, 0.0f);
}

TEST(MathUtilsTest, IoU_PartialOverlap)
{
    math_utils::Rect<long> r1(0, 0, 10, 10);
    math_utils::Rect<long> r2(5, 5, 15, 15);
    float iou = math_utils::calculateIoU(r1, r2);
    float expected = 25.0f / (100.0f + 100.0f - 25.0f); // intersection/union
    EXPECT_FLOAT_EQ(iou, expected);
}

TEST(MathUtilsTest, IoU_AreaRatioFilter)
{
    math_utils::Rect<long> r1(0, 0, 100, 100);
    math_utils::Rect<long> r2(0, 0, 1, 1);
    float iou = math_utils::calculateIoU(r1, r2);
    EXPECT_FLOAT_EQ(iou, 0.0f);
}
