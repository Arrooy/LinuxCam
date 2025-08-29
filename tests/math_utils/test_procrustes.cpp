#include <gtest/gtest.h>

#include <cmath>

#include "LinuxFace/math_utils.h"

using namespace linuxface;

TEST(MathUtilsTest, EstimateProcrustesSimilarity_Identity)
{
    double src[] = {0, 0, 1, 0};
    double dst[] = {0, 0, 1, 0};
    double M[6];
    bool ok = math_utils::estimateProcrustesSimilarity(src, dst, 2, M);
    EXPECT_TRUE(ok);
    EXPECT_NEAR(M[0], 1, 1e-6);
    EXPECT_NEAR(M[1], 0, 1e-6);
    EXPECT_NEAR(M[2], 0, 1e-6);
    EXPECT_NEAR(M[3], 0, 1e-6);
    EXPECT_NEAR(M[4], 1, 1e-6);
    EXPECT_NEAR(M[5], 0, 1e-6);
}

TEST(MathUtilsTest, EstimateProcrustesSimilarity_Failure)
{
    double src[] = {0, 0, 0, 0};
    double dst[] = {0, 0, 0, 0};
    double M[6];
    bool ok = math_utils::estimateProcrustesSimilarity(src, dst, 2, M);
    EXPECT_FALSE(ok);
}
