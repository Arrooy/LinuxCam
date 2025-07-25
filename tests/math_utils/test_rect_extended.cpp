#include <gtest/gtest.h>

#include "LinuxFace/math_utils.h"

using namespace linuxface::math_utils;

class RectExtendedTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Set up common test rectangles
        intRect = Rect<int>(10, 20, 50, 60);
        floatRect = Rect<float>(10.5f, 20.5f, 50.5f, 60.5f);
        doubleRect = Rect<double>(10.25, 20.25, 50.75, 60.75);
    }

    Rect<int> intRect;
    Rect<float> floatRect;
    Rect<double> doubleRect;
};

// Test comprehensive Rect constructors
TEST_F(RectExtendedTest, ConstructorVariations)
{
    // Default constructor
    Rect<int> defaultRect;
    EXPECT_EQ(defaultRect.l, 0);
    EXPECT_EQ(defaultRect.t, 0);
    EXPECT_EQ(defaultRect.r, 0);
    EXPECT_EQ(defaultRect.b, 0);

    // Constructor with coordinates
    Rect<int> coordRect(5, 10, 15, 20);
    EXPECT_EQ(coordRect.l, 5);
    EXPECT_EQ(coordRect.t, 10);
    EXPECT_EQ(coordRect.r, 15);
    EXPECT_EQ(coordRect.b, 20);

    // Constructor with point and dimensions
    Point<int> topLeft(10, 20);
    Rect<int> pointDimRect(topLeft, 30, 40);
    EXPECT_EQ(pointDimRect.l, 10);
    EXPECT_EQ(pointDimRect.t, 20);
    EXPECT_EQ(pointDimRect.r, 40); // 10 + 30
    EXPECT_EQ(pointDimRect.b, 60); // 20 + 40

    // Constructor with two points
    Point<int> bottomRight(40, 60);
    Rect<int> twoPointRect(topLeft, bottomRight);
    EXPECT_EQ(twoPointRect.l, 10);
    EXPECT_EQ(twoPointRect.t, 20);
    EXPECT_EQ(twoPointRect.r, 40);
    EXPECT_EQ(twoPointRect.b, 60);
}

// Test width and height calculations
TEST_F(RectExtendedTest, DimensionCalculations)
{
    // Standard case
    EXPECT_EQ(intRect.width(), 41);  // 50 - 10 + 1
    EXPECT_EQ(intRect.height(), 41); // 60 - 20 + 1

    // Zero-width rectangle
    Rect<int> zeroWidth(10, 10, 10, 20);
    EXPECT_EQ(zeroWidth.width(), 1);
    EXPECT_EQ(zeroWidth.height(), 11);

    // Zero-height rectangle
    Rect<int> zeroHeight(10, 10, 20, 10);
    EXPECT_EQ(zeroHeight.width(), 11);
    EXPECT_EQ(zeroHeight.height(), 1);

    // Single point rectangle
    Rect<int> singlePoint(5, 5, 5, 5);
    EXPECT_EQ(singlePoint.width(), 1);
    EXPECT_EQ(singlePoint.height(), 1);
}

// Test contains functionality
TEST_F(RectExtendedTest, ContainsFunction)
{
    // Point inside rectangle
    EXPECT_TRUE(intRect.contains(25, 40));
    EXPECT_TRUE(intRect.contains(Point<int>(25, 40)));

    // Point on edges (should be contained)
    EXPECT_TRUE(intRect.contains(10, 20)); // top-left corner
    EXPECT_TRUE(intRect.contains(50, 60)); // bottom-right corner
    EXPECT_TRUE(intRect.contains(30, 20)); // top edge
    EXPECT_TRUE(intRect.contains(10, 40)); // left edge

    // Point outside rectangle
    EXPECT_FALSE(intRect.contains(5, 15));  // outside left
    EXPECT_FALSE(intRect.contains(55, 40)); // outside right
    EXPECT_FALSE(intRect.contains(25, 15)); // outside top
    EXPECT_FALSE(intRect.contains(25, 65)); // outside bottom

    // Far outside
    EXPECT_FALSE(intRect.contains(-100, -100));
    EXPECT_FALSE(intRect.contains(1000, 1000));
}

// Test bounds checking functionality
TEST_F(RectExtendedTest, BoundsChecking)
{
    // Rectangle within bounds
    EXPECT_TRUE(intRect.isWithinBounds(100, 100, 1.0f));

    // Rectangle exactly at bounds
    Rect<int> exactBounds(0, 0, 99, 99);
    EXPECT_TRUE(exactBounds.isWithinBounds(100, 100, 1.0f));

    // Rectangle exceeding width bounds
    Rect<int> tooWide(0, 0, 150, 50);
    EXPECT_FALSE(tooWide.isWithinBounds(100, 100, 1.0f));

    // Rectangle exceeding height bounds
    Rect<int> tooTall(0, 0, 50, 150);
    EXPECT_FALSE(tooTall.isWithinBounds(100, 100, 1.0f));

    // Test with scale factor
    Rect<int> scaledRect(0, 0, 110, 110);
    EXPECT_FALSE(scaledRect.isWithinBounds(100, 100, 1.0f));
    EXPECT_TRUE(scaledRect.isWithinBounds(100, 100, 1.2f));

    // Zero or negative dimensions
    Rect<int> invalidRect(10, 10, 10, 10);
    EXPECT_TRUE(invalidRect.isWithinBounds(100, 100, 1.0f)); // Single point is valid

    Rect<int> negativeRect(20, 20, 10, 10); // Negative dimensions
    EXPECT_FALSE(negativeRect.isWithinBounds(100, 100, 1.0f));
}

// Test padding functionality
TEST_F(RectExtendedTest, PaddingOperations)
{
    Rect<int> testRect(10, 20, 30, 40);

    // Add symmetric padding
    testRect.addPadding(5, 5, 5, 5);
    EXPECT_EQ(testRect.l, 5);
    EXPECT_EQ(testRect.t, 15);
    EXPECT_EQ(testRect.r, 35);
    EXPECT_EQ(testRect.b, 45);

    // Add asymmetric padding
    Rect<int> asymRect(10, 10, 20, 20);
    asymRect.addPadding(2, 3, 4, 5);
    EXPECT_EQ(asymRect.l, 8);
    EXPECT_EQ(asymRect.t, 7);
    EXPECT_EQ(asymRect.r, 24);
    EXPECT_EQ(asymRect.b, 25);

    // Zero padding (should not change)
    Rect<int> noPadding(5, 5, 15, 15);
    auto original = noPadding;
    noPadding.addPadding(0, 0, 0, 0);
    EXPECT_EQ(noPadding.l, original.l);
    EXPECT_EQ(noPadding.t, original.t);
    EXPECT_EQ(noPadding.r, original.r);
    EXPECT_EQ(noPadding.b, original.b);
}

// Test with different numeric types
TEST_F(RectExtendedTest, NumericTypeSupport)
{
    // Float rectangle
    EXPECT_NEAR(floatRect.width(), 40.0f, 0.001f);
    EXPECT_NEAR(floatRect.height(), 40.0f, 0.001f);
    EXPECT_TRUE(floatRect.contains(25.0f, 40.0f));

    // Double rectangle
    EXPECT_NEAR(doubleRect.width(), 40.5, 0.001);
    EXPECT_NEAR(doubleRect.height(), 40.5, 0.001);
    EXPECT_TRUE(doubleRect.contains(25.0, 40.0));

    // Long rectangle
    Rect<long> longRect(1000000L, 2000000L, 5000000L, 6000000L);
    EXPECT_EQ(longRect.width(), 4000001L);
    EXPECT_EQ(longRect.height(), 4000001L);
}

// Test edge cases and boundary conditions
TEST_F(RectExtendedTest, EdgeCases)
{
    // Negative coordinates
    Rect<int> negativeRect(-10, -20, 10, 20);
    EXPECT_EQ(negativeRect.width(), 21);
    EXPECT_EQ(negativeRect.height(), 41);
    EXPECT_TRUE(negativeRect.contains(0, 0));
    EXPECT_TRUE(negativeRect.contains(-5, -10));

    // Very large coordinates
    Rect<long> largeRect(1000000000L, 1000000000L, 2000000000L, 2000000000L);
    EXPECT_EQ(largeRect.width(), 1000000001L);
    EXPECT_TRUE(largeRect.contains(1500000000L, 1500000000L));

    // Minimum size rectangle
    Rect<int> minRect(0, 0, 0, 0);
    EXPECT_EQ(minRect.width(), 1);
    EXPECT_EQ(minRect.height(), 1);
    EXPECT_TRUE(minRect.contains(0, 0));
    EXPECT_FALSE(minRect.contains(1, 1));
}

// Test getters
TEST_F(RectExtendedTest, GetterMethods)
{
    EXPECT_EQ(intRect.x(), 10);
    EXPECT_EQ(intRect.y(), 20);

    // Test with different types
    EXPECT_NEAR(floatRect.x(), 10.5f, 0.001f);
    EXPECT_NEAR(floatRect.y(), 20.5f, 0.001f);
}

// Test copy and assignment
TEST_F(RectExtendedTest, CopyAndAssignment)
{
    Rect<int> copy = intRect;
    EXPECT_EQ(copy.l, intRect.l);
    EXPECT_EQ(copy.t, intRect.t);
    EXPECT_EQ(copy.r, intRect.r);
    EXPECT_EQ(copy.b, intRect.b);

    Rect<int> assigned;
    assigned = intRect;
    EXPECT_EQ(assigned.l, intRect.l);
    EXPECT_EQ(assigned.t, intRect.t);
    EXPECT_EQ(assigned.r, intRect.r);
    EXPECT_EQ(assigned.b, intRect.b);
}

// Test performance with different scale factors
TEST_F(RectExtendedTest, ScaleFactorVariations)
{
    Rect<int> testRect(0, 0, 99, 99); // 100x100 area with inclusive bounds

    // Test various scale factors
    EXPECT_TRUE(testRect.isWithinBounds(100, 100, 1.0f));
    EXPECT_TRUE(testRect.isWithinBounds(90, 90, 1.2f));
    EXPECT_FALSE(testRect.isWithinBounds(90, 90, 1.0f));
    EXPECT_TRUE(testRect.isWithinBounds(50, 50, 2.1f));
    EXPECT_FALSE(testRect.isWithinBounds(50, 50, 1.9f));
}
