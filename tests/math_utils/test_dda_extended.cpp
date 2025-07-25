#include <gtest/gtest.h>

#include "LinuxFace/math_utils.h"

using namespace linuxface::math_utils;

class DDAExtendedTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Set up common test cases
    }

    // Helper function to verify line continuity
    void verifyLineContinuity(const std::vector<Point<long>>& line)
    {
        for (size_t i = 1; i < line.size(); ++i)
        {
            long dx = std::abs(line[i].x - line[i - 1].x);
            long dy = std::abs(line[i].y - line[i - 1].y);

            // Points should be adjacent (max distance of 1 in both directions)
            EXPECT_LE(dx, 1) << "Gap in x direction at index " << i;
            EXPECT_LE(dy, 1) << "Gap in y direction at index " << i;

            // At least one coordinate should change (no duplicate points)
            EXPECT_TRUE(dx > 0 || dy > 0) << "Duplicate point at index " << i;
        }
    }
};

// Test basic DDA functionality with integers
TEST_F(DDAExtendedTest, BasicIntegerLines)
{
    // Horizontal line
    auto horizontal = DDA(0, 5, 10, 5);
    EXPECT_EQ(horizontal.size(), 11); // 0 to 10 inclusive
    EXPECT_EQ(horizontal.front().x, 0);
    EXPECT_EQ(horizontal.front().y, 5);
    EXPECT_EQ(horizontal.back().x, 10);
    EXPECT_EQ(horizontal.back().y, 5);
    verifyLineContinuity(horizontal);

    // Vertical line
    auto vertical = DDA(5, 0, 5, 10);
    EXPECT_EQ(vertical.size(), 11);
    EXPECT_EQ(vertical.front().x, 5);
    EXPECT_EQ(vertical.front().y, 0);
    EXPECT_EQ(vertical.back().x, 5);
    EXPECT_EQ(vertical.back().y, 10);
    verifyLineContinuity(vertical);

    // Diagonal line (45 degrees)
    auto diagonal = DDA(0, 0, 10, 10);
    EXPECT_EQ(diagonal.size(), 11);
    EXPECT_EQ(diagonal.front().x, 0);
    EXPECT_EQ(diagonal.front().y, 0);
    EXPECT_EQ(diagonal.back().x, 10);
    EXPECT_EQ(diagonal.back().y, 10);
    verifyLineContinuity(diagonal);
}

// Test DDA with floating point coordinates
TEST_F(DDAExtendedTest, FloatingPointLines)
{
    // Float coordinates
    auto floatLine = DDA(0.5f, 1.5f, 10.7f, 8.3f);
    EXPECT_GE(floatLine.size(), 2);    // At least start and end points
    EXPECT_EQ(floatLine.front().x, 1); // rounded from 0.5
    EXPECT_EQ(floatLine.front().y, 2); // rounded from 1.5
    EXPECT_EQ(floatLine.back().x, 10); // floor of 10.7
    EXPECT_EQ(floatLine.back().y, 8);  // floor of 8.3
    verifyLineContinuity(floatLine);

    // Double coordinates
    auto doubleLine = DDA(0.25, 0.75, 5.9, 3.1);
    EXPECT_GE(doubleLine.size(), 2);
    verifyLineContinuity(doubleLine);

    // Very small float differences
    auto tinyLine = DDA(5.001f, 5.001f, 5.999f, 5.999f);
    EXPECT_GE(tinyLine.size(), 1);
    verifyLineContinuity(tinyLine);
}

// Test edge cases and special scenarios
TEST_F(DDAExtendedTest, EdgeCases)
{
    // Same start and end point
    auto samePoint = DDA(5, 5, 5, 5);
    EXPECT_EQ(samePoint.size(), 1);
    EXPECT_EQ(samePoint[0].x, 5);
    EXPECT_EQ(samePoint[0].y, 5);

    // Single pixel movement
    auto onePixelX = DDA(0, 0, 1, 0);
    EXPECT_EQ(onePixelX.size(), 2);
    EXPECT_EQ(onePixelX[0].x, 0);
    EXPECT_EQ(onePixelX[0].y, 0);
    EXPECT_EQ(onePixelX[1].x, 1);
    EXPECT_EQ(onePixelX[1].y, 0);

    auto onePixelY = DDA(0, 0, 0, 1);
    EXPECT_EQ(onePixelY.size(), 2);
    EXPECT_EQ(onePixelY[0].x, 0);
    EXPECT_EQ(onePixelY[0].y, 0);
    EXPECT_EQ(onePixelY[1].x, 0);
    EXPECT_EQ(onePixelY[1].y, 1);

    // Single pixel diagonal
    auto onePixelDiag = DDA(0, 0, 1, 1);
    EXPECT_EQ(onePixelDiag.size(), 2);
    verifyLineContinuity(onePixelDiag);
}

// Test negative coordinates
TEST_F(DDAExtendedTest, NegativeCoordinates)
{
    // From negative to positive
    auto negToPos = DDA(-5, -5, 5, 5);
    EXPECT_EQ(negToPos.front().x, -5);
    EXPECT_EQ(negToPos.front().y, -5);
    EXPECT_EQ(negToPos.back().x, 5);
    EXPECT_EQ(negToPos.back().y, 5);
    verifyLineContinuity(negToPos);

    // Both negative
    auto bothNeg = DDA(-10, -10, -5, -3);
    EXPECT_EQ(bothNeg.front().x, -10);
    EXPECT_EQ(bothNeg.front().y, -10);
    EXPECT_EQ(bothNeg.back().x, -5);
    EXPECT_EQ(bothNeg.back().y, -3);
    verifyLineContinuity(bothNeg);

    // Reverse direction (positive to negative)
    auto posToNeg = DDA(5, 5, -5, -5);
    EXPECT_EQ(posToNeg.front().x, 5);
    EXPECT_EQ(posToNeg.front().y, 5);
    EXPECT_EQ(posToNeg.back().x, -5);
    EXPECT_EQ(posToNeg.back().y, -5);
    verifyLineContinuity(posToNeg);
}

// Test steep and shallow lines
TEST_F(DDAExtendedTest, SteepAndShallowLines)
{
    // Very steep line (more Y than X)
    auto steepLine = DDA(0, 0, 2, 10);
    EXPECT_EQ(steepLine.size(), 11); // 10 steps + 1 endpoint
    EXPECT_EQ(steepLine.front().x, 0);
    EXPECT_EQ(steepLine.front().y, 0);
    EXPECT_EQ(steepLine.back().x, 2);
    EXPECT_EQ(steepLine.back().y, 10);
    verifyLineContinuity(steepLine);

    // Very shallow line (more X than Y)
    auto shallowLine = DDA(0, 0, 10, 2);
    EXPECT_EQ(shallowLine.size(), 11); // 10 steps + 1 endpoint
    EXPECT_EQ(shallowLine.front().x, 0);
    EXPECT_EQ(shallowLine.front().y, 0);
    EXPECT_EQ(shallowLine.back().x, 10);
    EXPECT_EQ(shallowLine.back().y, 2);
    verifyLineContinuity(shallowLine);

    // Nearly horizontal (1 pixel change in Y over many X)
    auto nearlyHorizontal = DDA(0, 0, 100, 1);
    EXPECT_EQ(nearlyHorizontal.size(), 101);
    verifyLineContinuity(nearlyHorizontal);

    // Nearly vertical (1 pixel change in X over many Y)
    auto nearlyVertical = DDA(0, 0, 1, 100);
    EXPECT_EQ(nearlyVertical.size(), 101);
    verifyLineContinuity(nearlyVertical);
}

// Test large coordinate values
TEST_F(DDAExtendedTest, LargeCoordinates)
{
    // Large positive coordinates
    auto largeLine = DDA(1000, 1000, 2000, 1500);
    EXPECT_EQ(largeLine.front().x, 1000);
    EXPECT_EQ(largeLine.front().y, 1000);
    EXPECT_EQ(largeLine.back().x, 2000);
    EXPECT_EQ(largeLine.back().y, 1500);
    verifyLineContinuity(largeLine);

    // Very large coordinates
    auto veryLargeLine = DDA(100000, 200000, 100010, 200020);
    EXPECT_EQ(veryLargeLine.front().x, 100000);
    EXPECT_EQ(veryLargeLine.front().y, 200000);
    EXPECT_EQ(veryLargeLine.back().x, 100010);
    EXPECT_EQ(veryLargeLine.back().y, 200020);
    verifyLineContinuity(veryLargeLine);
}

// Test different numeric types
TEST_F(DDAExtendedTest, DifferentNumericTypes)
{
    // Long integers
    auto longLine = DDA(0L, 0L, 10L, 10L);
    EXPECT_EQ(longLine.size(), 11);
    verifyLineContinuity(longLine);

    // Short integers
    short sx1 = 0, sy1 = 0, sx2 = 5, sy2 = 5;
    auto shortLine = DDA(sx1, sy1, sx2, sy2);
    EXPECT_EQ(shortLine.size(), 6);
    verifyLineContinuity(shortLine);

    // Mixed types (all should be same type to avoid template deduction issues)
    auto mixedLine = DDA(0, 0, 5, 5);
    EXPECT_GE(mixedLine.size(), 2);
    verifyLineContinuity(mixedLine);
}

// Test symmetry property
TEST_F(DDAExtendedTest, SymmetryProperty)
{
    // Forward line
    auto forward = DDA(0, 0, 10, 6);

    // Reverse line
    auto reverse = DDA(10, 6, 0, 0);

    // Should have same number of points
    EXPECT_EQ(forward.size(), reverse.size());

    // Reverse should be mirror of forward
    for (size_t i = 0; i < forward.size(); ++i)
    {
        size_t reverseIndex = reverse.size() - 1 - i;
        EXPECT_EQ(forward[i].x, reverse[reverseIndex].x);
        EXPECT_EQ(forward[i].y, reverse[reverseIndex].y);
    }
}

// Test performance characteristics
TEST_F(DDAExtendedTest, PerformanceCharacteristics)
{
    // Long line should still be manageable
    auto longPerformanceLine = DDA(0, 0, 1000, 1000);
    EXPECT_EQ(longPerformanceLine.size(), 1001);
    verifyLineContinuity(longPerformanceLine);

    // Very long line
    auto veryLongLine = DDA(0, 0, 10000, 5000);
    EXPECT_EQ(veryLongLine.size(), 10001); // Steps = max(dx, dy) = 10000

    // Should always have reasonable size (not infinite)
    EXPECT_LT(veryLongLine.size(), 20000);
}

// Test special angle cases
TEST_F(DDAExtendedTest, SpecialAngles)
{
    // 0 degrees (horizontal right)
    auto angle0 = DDA(0, 5, 10, 5);
    EXPECT_EQ(angle0.size(), 11);

    // 90 degrees (vertical up)
    auto angle90 = DDA(5, 0, 5, 10);
    EXPECT_EQ(angle90.size(), 11);

    // 180 degrees (horizontal left)
    auto angle180 = DDA(10, 5, 0, 5);
    EXPECT_EQ(angle180.size(), 11);

    // 270 degrees (vertical down)
    auto angle270 = DDA(5, 10, 5, 0);
    EXPECT_EQ(angle270.size(), 11);

    // 45 degrees
    auto angle45 = DDA(0, 0, 10, 10);
    EXPECT_EQ(angle45.size(), 11);

    // 135 degrees
    auto angle135 = DDA(10, 0, 0, 10);
    EXPECT_EQ(angle135.size(), 11);
}
