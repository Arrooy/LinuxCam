#include <gtest/gtest.h>
#include <vector>
#include <cmath>
#include "LinuxFace/math_utils.h"

using namespace linuxface;

class L2NormTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(L2NormTest, BasicNormalization) {
    std::vector<float> vec = {3.0f, 4.0f};
    math_utils::l2norm(vec);
    
    // Expected norm: sqrt(3^2 + 4^2) = 5
    // Normalized: [3/5, 4/5] = [0.6, 0.8]
    EXPECT_NEAR(vec[0], 0.6f, 1e-6f);
    EXPECT_NEAR(vec[1], 0.8f, 1e-6f);
    
    // Verify unit length
    float norm = std::sqrt(vec[0] * vec[0] + vec[1] * vec[1]);
    EXPECT_NEAR(norm, 1.0f, 1e-6f);
}

TEST_F(L2NormTest, ZeroVector) {
    std::vector<float> vec = {0.0f, 0.0f, 0.0f};
    math_utils::l2norm(vec);
    
    // Zero vector should remain zero (division by zero protection)
    EXPECT_EQ(vec[0], 0.0f);
    EXPECT_EQ(vec[1], 0.0f);
    EXPECT_EQ(vec[2], 0.0f);
}

TEST_F(L2NormTest, SingleElement) {
    std::vector<double> vec = {5.0};
    math_utils::l2norm(vec);
    
    // Single element should become 1.0
    EXPECT_NEAR(vec[0], 1.0, 1e-15);
}

TEST_F(L2NormTest, EmbeddingSize) {
    // Test with 512-dimensional vector like ArcFace embeddings
    std::vector<float> vec(512);
    for (int i = 0; i < 512; ++i) {
        vec[i] = static_cast<float>(i + 1);
    }
    
    math_utils::l2norm(vec);
    
    // Verify unit length
    float norm = 0.0f;
    for (const auto& val : vec) {
        norm += val * val;
    }
    norm = std::sqrt(norm);
    EXPECT_NEAR(norm, 1.0f, 1e-5f);
}

TEST_F(L2NormTest, NegativeValues) {
    std::vector<float> vec = {-3.0f, 4.0f, -5.0f};
    math_utils::l2norm(vec);
    
    // Verify unit length
    float norm = 0.0f;
    for (const auto& val : vec) {
        norm += val * val;
    }
    norm = std::sqrt(norm);
    EXPECT_NEAR(norm, 1.0f, 1e-6f);
}

TEST_F(L2NormTest, TemplateTypes) {
    // Test with double
    std::vector<double> vec_double = {1.0, 2.0, 3.0};
    math_utils::l2norm(vec_double);
    
    double norm_double = 0.0;
    for (const auto& val : vec_double) {
        norm_double += val * val;
    }
    norm_double = std::sqrt(norm_double);
    EXPECT_NEAR(norm_double, 1.0, 1e-15);
    
    // Test with float
    std::vector<float> vec_float = {1.0f, 2.0f, 3.0f};
    math_utils::l2norm(vec_float);
    
    float norm_float = 0.0f;
    for (const auto& val : vec_float) {
        norm_float += val * val;
    }
    norm_float = std::sqrt(norm_float);
    EXPECT_NEAR(norm_float, 1.0f, 1e-6f);
}
