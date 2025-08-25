#include <gtest/gtest.h>

#include <cmath>

#include "LinuxFace/math_utils.h"

using namespace linuxface;

TEST(MathUtilsTest, InvertAffine_Identity)
{
    double M[6] = {1, 0, 0, 0, 1, 0};
    double invM[6];
    bool ok = math_utils::invertAffine(M, invM);
    EXPECT_TRUE(ok);
    for (int i = 0; i < 6; ++i)
    {
        EXPECT_NEAR(invM[i], M[i], 1e-6);
    }
}

TEST(MathUtilsTest, InvertAffine_Simple)
{
    double M[6] = {2, 0, 0, 0, 2, 0};
    double invM[6];
    bool ok = math_utils::invertAffine(M, invM);
    EXPECT_TRUE(ok);
    EXPECT_NEAR(invM[0], 0.5, 1e-6);
    EXPECT_NEAR(invM[4], 0.5, 1e-6);
}

TEST(MathUtilsTest, InvertAffine_Singular)
{
    double M[6] = {0, 0, 0, 0, 0, 0};
    double invM[6];
    bool ok = math_utils::invertAffine(M, invM);
    EXPECT_FALSE(ok);
}
