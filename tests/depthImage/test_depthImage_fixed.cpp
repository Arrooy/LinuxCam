#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>

#include "LinuxFace/depthImage.h"

using namespace linuxface;

class DepthImageTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Test image dimensions
        width = 640;
        height = 480;
        testImageSize = width * height * sizeof(float);

        // Create test depth data
        size_t pixelCount = width * height;
        testDepthData = std::make_unique<float[]>(pixelCount);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            // Generate gradient depth values
            testDepthData[i] = 1000.0f + (i % 1000);
        }

        // Create test normal data (3 components per pixel)
        size_t normalCount = pixelCount * 3;
        testNormalData = std::make_unique<float[]>(normalCount);
        for (size_t i = 0; i < normalCount; i += 3)
        {
            testNormalData[i] = 0.0f;     // x
            testNormalData[i + 1] = 0.0f; // y
            testNormalData[i + 2] = 1.0f; // z (pointing up)
        }

        // Create test confidence data
        testConfidenceData = std::make_unique<float[]>(pixelCount);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            testConfidenceData[i] = 0.8f + (i % 2) * 0.1f; // Alternating 0.8 and 0.9
        }
    }

    unsigned long width, height;
    size_t testImageSize;
    std::unique_ptr<float[]> testDepthData;
    std::unique_ptr<float[]> testNormalData;
    std::unique_ptr<float[]> testConfidenceData;
};

// Test basic construction
TEST_F(DepthImageTest, DefaultConstruction)
{
    DepthImage depthImage;

    EXPECT_EQ(depthImage.info.width, 0);
    EXPECT_EQ(depthImage.info.height, 0);
    EXPECT_EQ(depthImage.info.format, ImageFormat::DEPTH_FLOAT);
}

// Test construction with dimensions
TEST_F(DepthImageTest, ConstructionWithDimensions)
{
    DepthImage depthImage(width, height);

    EXPECT_EQ(depthImage.info.width, width);
    EXPECT_EQ(depthImage.info.height, height);
    EXPECT_EQ(depthImage.info.format, ImageFormat::DEPTH_FLOAT);
    EXPECT_EQ(depthImage.info.pixelSizeBytes, sizeof(float));
    EXPECT_NE(depthImage.data(), nullptr);
}

// Test buffer adoption constructor
TEST_F(DepthImageTest, ConstructionWithBuffer)
{
    auto buffer = std::make_unique<unsigned char[]>(testImageSize);
    unsigned char* bufferPtr = buffer.get();

    DepthImage depthImage(buffer.release(), testImageSize, width, height, true);

    EXPECT_EQ(depthImage.info.width, width);
    EXPECT_EQ(depthImage.info.height, height);
    EXPECT_EQ(depthImage.info.format, ImageFormat::DEPTH_FLOAT);
    EXPECT_EQ(depthImage.data(), bufferPtr);
}

// Test depth value access
TEST_F(DepthImageTest, GetSetDepth)
{
    DepthImage depthImage(width, height);

    // Test setting and getting depth values
    float testDepth = 1500.0f;
    depthImage.setDepth(10, 20, testDepth);

    float retrievedDepth = depthImage.getDepth(10, 20);
    EXPECT_FLOAT_EQ(retrievedDepth, testDepth);

    // Test edge cases - set values first, then test them
    // (uninitialized memory can contain any value, including -nan)
    depthImage.setDepth(0, 0, 100.0f);
    float edgeDepth = depthImage.getDepth(0, 0);
    EXPECT_FLOAT_EQ(edgeDepth, 100.0f);

    depthImage.setDepth(width - 1, height - 1, 200.0f);
    float cornerDepth = depthImage.getDepth(width - 1, height - 1);
    EXPECT_FLOAT_EQ(cornerDepth, 200.0f);
}

// Test out of bounds access
TEST_F(DepthImageTest, GetDepthOutOfBounds)
{
    DepthImage depthImage(width, height);

    // Test out of bounds access
    float depth = depthImage.getDepth(width + 10, height + 10);
    EXPECT_EQ(depth, 0.0f);
}

// Test normals functionality
TEST_F(DepthImageTest, SetNormals)
{
    DepthImage depthImage(width, height);

    EXPECT_FALSE(depthImage.hasNormals());

    // Set normal data
    size_t normalDataSize = width * height * 3 * sizeof(float);
    depthImage.setNormals(testNormalData.get(), normalDataSize);

    EXPECT_TRUE(depthImage.hasNormals());

    // Test getting normal at specific pixel
    const float* normal = depthImage.getNormal(10, 20);
    ASSERT_NE(normal, nullptr);
    EXPECT_FLOAT_EQ(normal[0], 0.0f); // x
    EXPECT_FLOAT_EQ(normal[1], 0.0f); // y
    EXPECT_FLOAT_EQ(normal[2], 1.0f); // z
}

// Test confidence functionality
TEST_F(DepthImageTest, SetConfidence)
{
    DepthImage depthImage(width, height);

    EXPECT_FALSE(depthImage.hasConfidence());

    // Set confidence data
    size_t confidenceDataSize = width * height * sizeof(float);
    depthImage.setConfidence(testConfidenceData.get(), confidenceDataSize);

    EXPECT_TRUE(depthImage.hasConfidence());

    // Test getting confidence at specific pixel
    float confidence = depthImage.getConfidence(10, 20);
    EXPECT_GE(confidence, 0.0f);
    EXPECT_LE(confidence, 1.0f);
}

// Test confidence filtering
TEST_F(DepthImageTest, FilterByConfidence)
{
    DepthImage depthImage(width, height);

    // Set some depth values
    for (unsigned long y = 0; y < height; y += 10)
    {
        for (unsigned long x = 0; x < width; x += 10)
        {
            depthImage.setDepth(x, y, 1000.0f + x + y);
        }
    }

    // Set confidence data with some low confidence values
    size_t confidenceDataSize = width * height * sizeof(float);
    depthImage.setConfidence(testConfidenceData.get(), confidenceDataSize);

    // Filter by confidence
    depthImage.filterByConfidence(0.85f);

    // Check that some depth values were filtered (set to 0)
    bool foundFilteredPixel = false;
    for (unsigned long y = 0; y < height && !foundFilteredPixel; y += 10)
    {
        for (unsigned long x = 0; x < width && !foundFilteredPixel; x += 10)
        {
            if (depthImage.getDepth(x, y) == 0.0f)
            {
                foundFilteredPixel = true;
            }
        }
    }
    EXPECT_TRUE(foundFilteredPixel);
}

// Test depth statistics
TEST_F(DepthImageTest, GetDepthStats)
{
    DepthImage depthImage(width, height);

    // Set depth data using the raw data
    float* depthData = depthImage.getDepthData();
    std::memcpy(depthData, testDepthData.get(), width * height * sizeof(float));

    auto stats = depthImage.getDepthStats();

    EXPECT_GT(stats.validPixels, 0UL);
    EXPECT_GT(stats.minDepth, 0.0f);
    EXPECT_GT(stats.maxDepth, stats.minDepth);
    EXPECT_GE(stats.meanDepth, stats.minDepth);
    EXPECT_LE(stats.meanDepth, stats.maxDepth);
}

// Test deep copy
TEST_F(DepthImageTest, DeepCopy)
{
    DepthImage original(width, height);

    // Set some depth data
    original.setDepth(10, 20, 1500.0f);

    // Set normals and confidence
    size_t normalDataSize = width * height * 3 * sizeof(float);
    size_t confidenceDataSize = width * height * sizeof(float);
    original.setNormals(testNormalData.get(), normalDataSize);
    original.setConfidence(testConfidenceData.get(), confidenceDataSize);

    // Create deep copy
    auto copy = original.deepCopyDepth();

    EXPECT_EQ(copy->info.width, original.info.width);
    EXPECT_EQ(copy->info.height, original.info.height);
    EXPECT_EQ(copy->info.format, original.info.format);

    // Check that depth data was copied
    EXPECT_FLOAT_EQ(copy->getDepth(10, 20), original.getDepth(10, 20));
}

// Test memory safety with zero dimensions
TEST_F(DepthImageTest, ZeroDimensions)
{
    DepthImage depthImage(0, 0);

    // These should not crash
    float depth = depthImage.getDepth(0, 0);
    EXPECT_EQ(depth, 0.0f);

    const float* normal = depthImage.getNormal(0, 0);
    EXPECT_EQ(normal, nullptr);

    float confidence = depthImage.getConfidence(0, 0);
    EXPECT_EQ(confidence, 0.0f);
}

// Test data pointer access
TEST_F(DepthImageTest, DataPointerAccess)
{
    DepthImage depthImage(width, height);

    float* depthData = depthImage.getDepthData();
    EXPECT_NE(depthData, nullptr);

    // Direct data manipulation
    depthData[0] = 999.0f;
    EXPECT_FLOAT_EQ(depthImage.getDepth(0, 0), 999.0f);
}
