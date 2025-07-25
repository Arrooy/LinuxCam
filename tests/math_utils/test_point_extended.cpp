#include <gtest/gtest.h>

#include <climits>

#include "LinuxFace/math_utils.h"

using namespace linuxface::math_utils;

class PointExtendedTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        intPoint = Point<int>(10, 20);
        floatPoint = Point<float>(10.5f, 20.5f);
        doublePoint = Point<double>(10.25, 20.75);

        point3D = Point3D(5.0, 10.0, 15.0);
        point3DDefault = Point3D(5.0, 10.0); // z should default to 0
    }

    Point<int> intPoint;
    Point<float> floatPoint;
    Point<double> doublePoint;
    Point3D point3D;
    Point3D point3DDefault;
};

// Test Point constructors and basic functionality
TEST_F(PointExtendedTest, PointConstructors)
{
    // Default constructor
    Point<int> defaultPoint;
    EXPECT_EQ(defaultPoint.x, 0);
    EXPECT_EQ(defaultPoint.y, 0);

    // Parameterized constructor
    Point<int> paramPoint(42, 84);
    EXPECT_EQ(paramPoint.x, 42);
    EXPECT_EQ(paramPoint.y, 84);

    // Copy constructor (implicit)
    Point<int> copiedPoint = intPoint;
    EXPECT_EQ(copiedPoint.x, intPoint.x);
    EXPECT_EQ(copiedPoint.y, intPoint.y);
}

// Test Point with different numeric types
TEST_F(PointExtendedTest, PointNumericTypes)
{
    // Integer point
    EXPECT_EQ(intPoint.x, 10);
    EXPECT_EQ(intPoint.y, 20);

    // Float point
    EXPECT_NEAR(floatPoint.x, 10.5f, 0.001f);
    EXPECT_NEAR(floatPoint.y, 20.5f, 0.001f);

    // Double point
    EXPECT_NEAR(doublePoint.x, 10.25, 0.001);
    EXPECT_NEAR(doublePoint.y, 20.75, 0.001);

    // Long point
    Point<long> longPoint(1000000000L, 2000000000L);
    EXPECT_EQ(longPoint.x, 1000000000L);
    EXPECT_EQ(longPoint.y, 2000000000L);

    // Unsigned types
    Point<unsigned int> unsignedPoint(100U, 200U);
    EXPECT_EQ(unsignedPoint.x, 100U);
    EXPECT_EQ(unsignedPoint.y, 200U);
}

// Test Point3D constructors and functionality
TEST_F(PointExtendedTest, Point3DConstructors)
{
    // Default constructor
    Point3D defaultPoint3D;
    EXPECT_DOUBLE_EQ(defaultPoint3D.x, 0.0);
    EXPECT_DOUBLE_EQ(defaultPoint3D.y, 0.0);
    EXPECT_DOUBLE_EQ(defaultPoint3D.z, 0.0);

    // Constructor with x, y, z
    EXPECT_DOUBLE_EQ(point3D.x, 5.0);
    EXPECT_DOUBLE_EQ(point3D.y, 10.0);
    EXPECT_DOUBLE_EQ(point3D.z, 15.0);

    // Constructor with x, y (z defaults to 0)
    EXPECT_DOUBLE_EQ(point3DDefault.x, 5.0);
    EXPECT_DOUBLE_EQ(point3DDefault.y, 10.0);
    EXPECT_DOUBLE_EQ(point3DDefault.z, 0.0);

    // Copy constructor
    Point3D copiedPoint3D = point3D;
    EXPECT_DOUBLE_EQ(copiedPoint3D.x, point3D.x);
    EXPECT_DOUBLE_EQ(copiedPoint3D.y, point3D.y);
    EXPECT_DOUBLE_EQ(copiedPoint3D.z, point3D.z);
}

// Test Point assignment and modification
TEST_F(PointExtendedTest, PointAssignmentAndModification)
{
    Point<int> modifiablePoint;

    // Assignment
    modifiablePoint = intPoint;
    EXPECT_EQ(modifiablePoint.x, intPoint.x);
    EXPECT_EQ(modifiablePoint.y, intPoint.y);

    // Modification
    modifiablePoint.x = 100;
    modifiablePoint.y = 200;
    EXPECT_EQ(modifiablePoint.x, 100);
    EXPECT_EQ(modifiablePoint.y, 200);

    // Original should be unchanged
    EXPECT_EQ(intPoint.x, 10);
    EXPECT_EQ(intPoint.y, 20);
}

// Test Point3D assignment and modification
TEST_F(PointExtendedTest, Point3DAssignmentAndModification)
{
    Point3D modifiablePoint3D;

    // Assignment
    modifiablePoint3D = point3D;
    EXPECT_DOUBLE_EQ(modifiablePoint3D.x, point3D.x);
    EXPECT_DOUBLE_EQ(modifiablePoint3D.y, point3D.y);
    EXPECT_DOUBLE_EQ(modifiablePoint3D.z, point3D.z);

    // Modification
    modifiablePoint3D.x = 100.5;
    modifiablePoint3D.y = 200.75;
    modifiablePoint3D.z = 300.25;
    EXPECT_DOUBLE_EQ(modifiablePoint3D.x, 100.5);
    EXPECT_DOUBLE_EQ(modifiablePoint3D.y, 200.75);
    EXPECT_DOUBLE_EQ(modifiablePoint3D.z, 300.25);
}

// Test edge cases and boundary values
TEST_F(PointExtendedTest, EdgeCasesAndBoundaryValues)
{
    // Negative coordinates
    Point<int> negativePoint(-10, -20);
    EXPECT_EQ(negativePoint.x, -10);
    EXPECT_EQ(negativePoint.y, -20);

    Point3D negativePoint3D(-5.5, -10.5, -15.5);
    EXPECT_DOUBLE_EQ(negativePoint3D.x, -5.5);
    EXPECT_DOUBLE_EQ(negativePoint3D.y, -10.5);
    EXPECT_DOUBLE_EQ(negativePoint3D.z, -15.5);

    // Zero coordinates
    Point<int> zeroPoint(0, 0);
    EXPECT_EQ(zeroPoint.x, 0);
    EXPECT_EQ(zeroPoint.y, 0);

    Point3D zeroPoint3D(0.0, 0.0, 0.0);
    EXPECT_DOUBLE_EQ(zeroPoint3D.x, 0.0);
    EXPECT_DOUBLE_EQ(zeroPoint3D.y, 0.0);
    EXPECT_DOUBLE_EQ(zeroPoint3D.z, 0.0);

    // Maximum values for different types
    Point<int> maxIntPoint(INT_MAX, INT_MAX);
    EXPECT_EQ(maxIntPoint.x, INT_MAX);
    EXPECT_EQ(maxIntPoint.y, INT_MAX);

    // Minimum values
    Point<int> minIntPoint(INT_MIN, INT_MIN);
    EXPECT_EQ(minIntPoint.x, INT_MIN);
    EXPECT_EQ(minIntPoint.y, INT_MIN);
}

// Test floating point precision
TEST_F(PointExtendedTest, FloatingPointPrecision)
{
    // Very small numbers
    Point<double> tinyPoint(1e-10, 1e-10);
    EXPECT_DOUBLE_EQ(tinyPoint.x, 1e-10);
    EXPECT_DOUBLE_EQ(tinyPoint.y, 1e-10);

    Point3D tinyPoint3D(1e-15, 1e-15, 1e-15);
    EXPECT_DOUBLE_EQ(tinyPoint3D.x, 1e-15);
    EXPECT_DOUBLE_EQ(tinyPoint3D.y, 1e-15);
    EXPECT_DOUBLE_EQ(tinyPoint3D.z, 1e-15);

    // Very large numbers
    Point<double> largePoint(1e10, 1e10);
    EXPECT_DOUBLE_EQ(largePoint.x, 1e10);
    EXPECT_DOUBLE_EQ(largePoint.y, 1e10);

    Point3D largePoint3D(1e12, 1e12, 1e12);
    EXPECT_DOUBLE_EQ(largePoint3D.x, 1e12);
    EXPECT_DOUBLE_EQ(largePoint3D.y, 1e12);
    EXPECT_DOUBLE_EQ(largePoint3D.z, 1e12);
}

// Test type conversions and implicit conversions
TEST_F(PointExtendedTest, TypeConversions)
{
    // Creating points with different but compatible types
    Point<long> longFromInt(intPoint.x, intPoint.y);
    EXPECT_EQ(longFromInt.x, static_cast<long>(intPoint.x));
    EXPECT_EQ(longFromInt.y, static_cast<long>(intPoint.y));

    // Float to double conversion for Point3D
    Point3D doubleFrom3D(static_cast<double>(floatPoint.x), static_cast<double>(floatPoint.y), 25.0);
    EXPECT_NEAR(doubleFrom3D.x, 10.5, 0.001);
    EXPECT_NEAR(doubleFrom3D.y, 20.5, 0.001);
    EXPECT_DOUBLE_EQ(doubleFrom3D.z, 25.0);
}

// Test with various specialized point types
TEST_F(PointExtendedTest, SpecializedPointTypes)
{
    // Test with size_t (commonly used for array indices)
    Point<size_t> sizePoint(100UL, 200UL);
    EXPECT_EQ(sizePoint.x, 100UL);
    EXPECT_EQ(sizePoint.y, 200UL);

    // Test with short integers
    Point<short> shortPoint(32000, -32000);
    EXPECT_EQ(shortPoint.x, 32000);
    EXPECT_EQ(shortPoint.y, -32000);

    // Test with unsigned char (for small coordinates)
    Point<unsigned char> bytePoint(255, 128);
    EXPECT_EQ(bytePoint.x, 255);
    EXPECT_EQ(bytePoint.y, 128);
}

// Test array-like access patterns
TEST_F(PointExtendedTest, ArrayLikeAccess)
{
    // Create an array of points
    std::vector<Point<int>> pointArray;
    for (int i = 0; i < 10; ++i)
    {
        pointArray.emplace_back(Point<int>(i, i * 2));
    }

    // Verify the points
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_EQ(pointArray[i].x, i);
        EXPECT_EQ(pointArray[i].y, i * 2);
    }

    // Create an array of 3D points
    std::vector<Point3D> point3DArray;
    for (int i = 0; i < 5; ++i)
    {
        point3DArray.emplace_back(Point3D(i, i * 2.0, i * 3.0));
    }

    // Verify the 3D points
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_DOUBLE_EQ(point3DArray[i].x, static_cast<double>(i));
        EXPECT_DOUBLE_EQ(point3DArray[i].y, static_cast<double>(i * 2));
        EXPECT_DOUBLE_EQ(point3DArray[i].z, static_cast<double>(i * 3));
    }
}
