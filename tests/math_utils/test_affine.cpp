#include <gtest/gtest.h>

#include <cmath>

#include "LinuxFace/math_utils.h"

using namespace linuxface;

TEST(MathUtilsTest, EstimateAffine2D_Identity)
{
    double src[] = {0, 0, 1, 0, 0, 1};
    double dst[] = {0, 0, 1, 0, 0, 1};
    double M[6];
    bool ok = math_utils::estimate_affine_2d(src, dst, 3, M);
    EXPECT_TRUE(ok);
    EXPECT_NEAR(M[0], 1, 1e-6);
    EXPECT_NEAR(M[1], 0, 1e-6);
    EXPECT_NEAR(M[2], 0, 1e-6);
    EXPECT_NEAR(M[3], 0, 1e-6);
    EXPECT_NEAR(M[4], 1, 1e-6);
    EXPECT_NEAR(M[5], 0, 1e-6);
}

TEST(MathUtilsTest, EstimateAffine2D_Translation)
{
    double src[] = {0, 0, 1, 0, 0, 1};
    double dst[] = {1, 2, 2, 2, 1, 3};
    double M[6];
    bool ok = math_utils::estimate_affine_2d(src, dst, 3, M);
    EXPECT_TRUE(ok);
    EXPECT_NEAR(M[2], 1, 1e-6);
    EXPECT_NEAR(M[5], 2, 1e-6);
}

TEST(MathUtilsTest, EstimateAffine2D_Failure)
{
    double src[] = {0, 0, 0, 0, 0, 0};
    double dst[] = {0, 0, 0, 0, 0, 0};
    double M[6];
    bool ok = math_utils::estimate_affine_2d(src, dst, 3, M);
    EXPECT_FALSE(ok);
    EXPECT_NEAR(M[0], 1, 1e-6);
    EXPECT_NEAR(M[1], 0, 1e-6);
    EXPECT_NEAR(M[2], 0, 1e-6);
    EXPECT_NEAR(M[3], 0, 1e-6);
    EXPECT_NEAR(M[4], 1, 1e-6);
    EXPECT_NEAR(M[5], 0, 1e-6);
}

TEST(MathUtilsTest, EstimateAffine2D_NaNInf)
{
    double src[] = {0, 0, NAN, 0, 0, 1};
    double dst[] = {0, 0, 1, 0, 0, 1};
    double M[6];
    bool ok = math_utils::estimate_affine_2d(src, dst, 3, M);
    EXPECT_FALSE(ok);
    EXPECT_NEAR(M[0], 1, 1e-6);
    EXPECT_NEAR(M[1], 0, 1e-6);
    EXPECT_NEAR(M[2], 0, 1e-6);
    EXPECT_NEAR(M[3], 0, 1e-6);
    EXPECT_NEAR(M[4], 1, 1e-6);
    EXPECT_NEAR(M[5], 0, 1e-6);

    src[2] = INFINITY;
    ok = math_utils::estimate_affine_2d(src, dst, 3, M);
    EXPECT_FALSE(ok);
    EXPECT_NEAR(M[0], 1, 1e-6);
    EXPECT_NEAR(M[4], 1, 1e-6);
}
