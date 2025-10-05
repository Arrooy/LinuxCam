/**
 * UNIT TESTS FOR IMAGE UTILITIES
 *
 * Tests for image quality metrics and utility functions
 */

#include <gtest/gtest.h>
#include <memory>

#include "../common/test_utils.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/image_utils.h"

using namespace linuxface;
using namespace linuxface::image_utils;

class ImageUtilsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create checkerboard pattern images - both start as black
        identical1_ = std::make_unique<Image>(Pixel(0, 0, 0), 10, 10);
        identical2_ = std::make_unique<Image>(Pixel(0, 0, 0), 10, 10);

        // Create checkerboard pattern for both identical images
        for (uint32_t y = 0; y < 10; ++y)
        {
            for (uint32_t x = 0; x < 10; ++x)
            {
                if ((x + y) % 2 == 0)
                {
                    // White squares
                    identical1_->ppx(x, y, Pixel(255, 255, 255));
                    identical2_->ppx(x, y, Pixel(255, 255, 255));
                }
                else
                {
                    // Black squares (already set, but being explicit)
                    identical1_->ppx(x, y, Pixel(0, 0, 0));
                    identical2_->ppx(x, y, Pixel(0, 0, 0));
                }
            }
        }

        // Create a slightly different image (similar checkerboard but with gray instead of white)
        different_ = std::make_unique<Image>(Pixel(0, 0, 0), 10, 10);
        for (uint32_t y = 0; y < 10; ++y)
        {
            for (uint32_t x = 0; x < 10; ++x)
            {
                if ((x + y) % 2 == 0)
                {
                    different_->ppx(x, y, Pixel(128, 128, 128)); // Gray instead of white
                }
                else
                {
                    different_->ppx(x, y, Pixel(0, 0, 0));
                }
            }
        }

        // Create a very different image (horizontal stripes vs checkerboard)
        very_different_ = std::make_unique<Image>(Pixel(0, 0, 0), 10, 10);
        for (uint32_t y = 0; y < 10; ++y)
        {
            for (uint32_t x = 0; x < 10; ++x)
            {
                if (y % 2 == 0)
                {
                    very_different_->ppx(x, y, Pixel(255, 0, 0)); // Red horizontal stripes
                }
                else
                {
                    very_different_->ppx(x, y, Pixel(0, 255, 0)); // Green horizontal stripes
                }
            }
        }

        // Create incompatible size image
        incompatible_ = std::make_unique<Image>(Pixel(128, 128, 128), 5, 5);
    }

    void createTestImages()
    {
        // Create much smaller test images
        identical1_ = std::make_unique<Image>(Pixel(128, 128, 128), 10, 10);
        identical2_ = std::make_unique<Image>(Pixel(128, 128, 128), 10, 10);

        // Create a slightly different image
        different_ = std::make_unique<Image>(Pixel(100, 100, 100), 10, 10);

        // Create a very different image
        very_different_ = std::make_unique<Image>(Pixel(255, 0, 0), 10, 10);

        // Create incompatible size image
        incompatible_ = std::make_unique<Image>(Pixel(128, 128, 128), 5, 5);
    }

    std::unique_ptr<Image> identical1_;
    std::unique_ptr<Image> identical2_;
    std::unique_ptr<Image> different_;
    std::unique_ptr<Image> very_different_;
    std::unique_ptr<Image> incompatible_;
};

// ========== Debug Tests ==========

TEST_F(ImageUtilsTest, ImageDimensionsTest)
{
    // Test that our test images have correct dimensions
    EXPECT_EQ(identical1_->info.width, 10U);
    EXPECT_EQ(identical1_->info.height, 10U);
    EXPECT_EQ(identical2_->info.width, 10U);
    EXPECT_EQ(identical2_->info.height, 10U);

    std::cout << "Image1 dimensions: " << identical1_->info.width << "x" << identical1_->info.height << std::endl;
    std::cout << "Image2 dimensions: " << identical2_->info.width << "x" << identical2_->info.height << std::endl;
}

// ========== MSE Tests ==========

TEST_F(ImageUtilsTest, CalculateMSE_IdenticalImages)
{
    double mse = calculateMSE(*identical1_, *identical2_);
    EXPECT_DOUBLE_EQ(0.0, mse) << "MSE between identical images should be 0.0";
}

TEST_F(ImageUtilsTest, CalculateMSE_DifferentImages)
{
    double mse = calculateMSE(*identical1_, *different_);
    EXPECT_GT(mse, 0.0) << "MSE between different images should be greater than 0";
    EXPECT_LT(mse, 20000.0) << "MSE should be reasonable for slightly different images";
}

TEST_F(ImageUtilsTest, CalculateMSE_VeryDifferentImages)
{
    double mse = calculateMSE(*identical1_, *very_different_);
    EXPECT_GT(mse, 1000.0) << "MSE between very different images should be high";
}

TEST_F(ImageUtilsTest, CalculateMSE_IncompatibleSizes)
{
    double mse = calculateMSE(*identical1_, *incompatible_);
    EXPECT_DOUBLE_EQ(-1.0, mse) << "MSE for incompatible sizes should return -1.0";
}

// ========== PSNR Tests ==========

TEST_F(ImageUtilsTest, CalculatePSNR_PerfectMatch)
{
    double psnr = calculatePSNR(0.0);
    EXPECT_DOUBLE_EQ(100.0, psnr) << "PSNR for perfect match (MSE=0) should be 100.0";
}

TEST_F(ImageUtilsTest, CalculatePSNR_ValidMSE)
{
    double mse = 100.0;
    double psnr = calculatePSNR(mse);
    EXPECT_GT(psnr, 0.0) << "PSNR should be positive for valid MSE";
    EXPECT_LT(psnr, 100.0) << "PSNR should be less than 100 for non-zero MSE";
}

TEST_F(ImageUtilsTest, CalculatePSNR_HighMSE)
{
    double mse = 10000.0; // High MSE
    double psnr = calculatePSNR(mse);
    EXPECT_GT(psnr, 0.0) << "PSNR should still be positive for high MSE";
    EXPECT_LT(psnr, 20.0) << "PSNR should be low for high MSE";
}

// ========== SSIM Tests ==========

TEST_F(ImageUtilsTest, CalculateSSIM_IdenticalImages)
{
    double ssim = calculateSSIM(*identical1_, *identical2_);
    EXPECT_NEAR(1.0, ssim, 0.01) << "SSIM between identical images should be close to 1.0";
}

TEST_F(ImageUtilsTest, CalculateSSIM_DifferentImages)
{
    double ssim = calculateSSIM(*identical1_, *different_);
    EXPECT_GT(ssim, 0.5) << "SSIM between similar images should be > 0.5";
    EXPECT_LT(ssim, 1.0) << "SSIM between different images should be < 1.0";
}

TEST_F(ImageUtilsTest, CalculateSSIM_VeryDifferentImages)
{
    double ssim = calculateSSIM(*identical1_, *very_different_);
    EXPECT_LT(ssim, 0.5) << "SSIM between very different images should be low";
}

TEST_F(ImageUtilsTest, CalculateSSIM_IncompatibleSizes)
{
    double ssim = calculateSSIM(*identical1_, *incompatible_);
    EXPECT_DOUBLE_EQ(-1.0, ssim) << "SSIM for incompatible sizes should return -1.0";
}

// ========== LPIPS Tests ==========

TEST_F(ImageUtilsTest, CalculateApproximateLPIPS_IdenticalImages)
{
    double lpips = calculateApproximateLPIPS(*identical1_, *identical2_);
    EXPECT_NEAR(0.0, lpips, 0.01) << "LPIPS between identical images should be close to 0.0";
}

TEST_F(ImageUtilsTest, CalculateApproximateLPIPS_DifferentImages)
{
    double lpips = calculateApproximateLPIPS(*identical1_, *different_);
    EXPECT_GT(lpips, 0.0) << "LPIPS between different images should be > 0";
}

TEST_F(ImageUtilsTest, CalculateApproximateLPIPS_IncompatibleSizes)
{
    double lpips = calculateApproximateLPIPS(*identical1_, *incompatible_);
    EXPECT_DOUBLE_EQ(-1.0, lpips) << "LPIPS for incompatible sizes should return -1.0";
}

// ========== ImageMetrics Tests ==========

TEST_F(ImageUtilsTest, CalculateImageMetrics_IdenticalImages)
{
    ImageMetrics metrics = calculateImageMetrics(*identical1_, *identical2_);

    EXPECT_DOUBLE_EQ(0.0, metrics.mse) << "MSE should be 0.0 for identical images";
    EXPECT_DOUBLE_EQ(100.0, metrics.psnr) << "PSNR should be 100.0 for identical images";
    EXPECT_NEAR(1.0, metrics.ssim, 0.01) << "SSIM should be close to 1.0 for identical images";
    EXPECT_NEAR(0.0, metrics.lpips, 0.01) << "LPIPS should be close to 0.0 for identical images";
}

TEST_F(ImageUtilsTest, CalculateImageMetrics_DifferentImages)
{
    ImageMetrics metrics = calculateImageMetrics(*identical1_, *different_);

    EXPECT_GT(metrics.mse, 0.0) << "MSE should be > 0 for different images";
    EXPECT_LT(metrics.psnr, 100.0) << "PSNR should be < 100 for different images";
    EXPECT_LT(metrics.ssim, 1.0) << "SSIM should be < 1.0 for different images";
    EXPECT_GT(metrics.lpips, 0.0) << "LPIPS should be > 0 for different images";
}

TEST_F(ImageUtilsTest, ImageMetrics_ToString)
{
    ImageMetrics metrics = calculateImageMetrics(*identical1_, *different_);
    std::string result = metrics.toString();

    EXPECT_FALSE(result.empty()) << "toString() should return non-empty string";
    EXPECT_NE(result.find("MSE:"), std::string::npos) << "toString() should contain MSE";
    EXPECT_NE(result.find("PSNR:"), std::string::npos) << "toString() should contain PSNR";
    EXPECT_NE(result.find("SSIM:"), std::string::npos) << "toString() should contain SSIM";
    EXPECT_NE(result.find("LPIPS:"), std::string::npos) << "toString() should contain LPIPS";
}

// ========== Edge Cases ==========

TEST_F(ImageUtilsTest, EmptyImages)
{
    auto empty1 = std::make_unique<Image>(0);
    auto empty2 = std::make_unique<Image>(0);

    EXPECT_DOUBLE_EQ(-1.0, calculateMSE(*empty1, *empty2));
    EXPECT_DOUBLE_EQ(-1.0, calculateSSIM(*empty1, *empty2));
    EXPECT_DOUBLE_EQ(-1.0, calculateApproximateLPIPS(*empty1, *empty2));
}

TEST_F(ImageUtilsTest, SinglePixelImages)
{
    auto single1 = std::make_unique<Image>(Pixel(100, 100, 100), 1, 1);
    auto single2 = std::make_unique<Image>(Pixel(100, 100, 100), 1, 1);

    EXPECT_DOUBLE_EQ(0.0, calculateMSE(*single1, *single2));
    // SSIM might not be meaningful for single pixels, but should not crash
    double ssim = calculateSSIM(*single1, *single2);
    EXPECT_TRUE(std::isfinite(ssim));
}

// ========== Euclidean Distance Transform Tests ==========

class DistanceTransformTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create simple test masks
        createSimpleSquareMask();
        createCircularMask();
    }

    void createSimpleSquareMask()
    {
        // 10x10 mask with 4x4 white square in center
        simpleSquare_ = std::make_unique<Image>(100); // 10*10 grayscale
        simpleSquare_->info.width = 10;
        simpleSquare_->info.height = 10;
        simpleSquare_->info.format = ImageFormat::GRAYSCALE;
        simpleSquare_->info.pixelSizeBytes = 1;

        unsigned char* data = simpleSquare_->data();
        std::fill(data, data + 100, 0); // Initialize all to black

        // Create 4x4 white square from (3,3) to (6,6)
        for (int y = 3; y < 7; ++y)
        {
            for (int x = 3; x < 7; ++x)
            {
                data[y * 10 + x] = 255;
            }
        }
    }

    void createCircularMask()
    {
        // 20x20 mask with approximate circle in center
        circularMask_ = std::make_unique<Image>(400); // 20*20 grayscale
        circularMask_->info.width = 20;
        circularMask_->info.height = 20;
        circularMask_->info.format = ImageFormat::GRAYSCALE;
        circularMask_->info.pixelSizeBytes = 1;

        unsigned char* data = circularMask_->data();
        std::fill(data, data + 400, 0); // Initialize all to black

        const int centerX = 10, centerY = 10, radius = 6;

        for (int y = 0; y < 20; ++y)
        {
            for (int x = 0; x < 20; ++x)
            {
                int dx = x - centerX;
                int dy = y - centerY;
                if (dx * dx + dy * dy <= radius * radius)
                {
                    data[y * 20 + x] = 255;
                }
            }
        }
    }

    std::vector<unsigned char> imageToVector(const Image& img)
    {
        const unsigned char* data = img.data();
        const size_t size = img.info.width * img.info.height * img.info.pixelSizeBytes;
        return std::vector<unsigned char>(data, data + size);
    }

    std::unique_ptr<Image> simpleSquare_;
    std::unique_ptr<Image> circularMask_;
};

TEST_F(DistanceTransformTest, ComputeDistanceField_BasicFunctionality)
{
    // Test with a simple 5x5 mask with single pixel in center
    std::vector<unsigned char> mask(25, 0);
    mask[2 * 5 + 2] = 255; // Center pixel

    auto distances = computeDistanceField(mask, 5, 5, true);

    ASSERT_EQ(distances.size(), 25U);

    // Center should have distance 0
    EXPECT_FLOAT_EQ(distances[2 * 5 + 2], 0.0f);

    // Adjacent pixels should have distance ≈ 1
    EXPECT_NEAR(distances[1 * 5 + 2], 1.0f, 0.1f); // Above
    EXPECT_NEAR(distances[3 * 5 + 2], 1.0f, 0.1f); // Below
    EXPECT_NEAR(distances[2 * 5 + 1], 1.0f, 0.1f); // Left
    EXPECT_NEAR(distances[2 * 5 + 3], 1.0f, 0.1f); // Right

    // Diagonal pixels should have distance ≈ √2
    EXPECT_NEAR(distances[1 * 5 + 1], std::sqrt(2.0f), 0.2f);
    EXPECT_NEAR(distances[3 * 5 + 3], std::sqrt(2.0f), 0.2f);
}

TEST_F(DistanceTransformTest, ComputeDistanceField_InsideSeeds)
{
    const auto maskVector = imageToVector(*simpleSquare_);
    const int width = static_cast<int>(simpleSquare_->info.width);
    const int height = static_cast<int>(simpleSquare_->info.height);

    // Test with seeds inside the mask (white pixels are seeds)
    auto distances = computeDistanceField(maskVector, width, height, true);

    ASSERT_EQ(distances.size(), static_cast<size_t>(width * height));

    // Center pixels (inside the square) should have distance 0
    EXPECT_FLOAT_EQ(distances[5 * width + 5], 0.0f) << "Center pixel should have distance 0";
    EXPECT_FLOAT_EQ(distances[4 * width + 4], 0.0f) << "Inner pixel should have distance 0";

    // Edge pixels of the square should have distance 0
    EXPECT_FLOAT_EQ(distances[3 * width + 3], 0.0f) << "Square boundary should have distance 0";
    EXPECT_FLOAT_EQ(distances[6 * width + 6], 0.0f) << "Square boundary should have distance 0";

    // Outside pixels should have positive distances
    EXPECT_GT(distances[1 * width + 1], 0.0f) << "Outside pixel should have positive distance";
    EXPECT_GT(distances[8 * width + 8], 0.0f) << "Outside pixel should have positive distance";
}

TEST_F(DistanceTransformTest, ComputeDistanceField_OutsideSeeds)
{
    const auto maskVector = imageToVector(*simpleSquare_);
    const int width = static_cast<int>(simpleSquare_->info.width);
    const int height = static_cast<int>(simpleSquare_->info.height);

    // Test with seeds outside the mask (black pixels are seeds)
    auto distances = computeDistanceField(maskVector, width, height, false);

    ASSERT_EQ(distances.size(), static_cast<size_t>(width * height));

    // Outside pixels should have distance 0
    EXPECT_FLOAT_EQ(distances[0 * width + 0], 0.0f) << "Corner pixel should have distance 0";
    EXPECT_FLOAT_EQ(distances[1 * width + 1], 0.0f) << "Outside pixel should have distance 0";

    // Inside pixels should have positive distances
    EXPECT_GT(distances[5 * width + 5], 0.0f) << "Center pixel should have positive distance";
    EXPECT_GT(distances[4 * width + 4], 0.0f) << "Inner pixel should have positive distance";

    // Pixels closer to the edge should have smaller distances
    float center_dist = distances[5 * width + 5];
    float edge_dist = distances[3 * width + 5]; // Closer to left edge
    EXPECT_LT(edge_dist, center_dist) << "Pixels closer to boundary should have smaller distances";
}

TEST_F(DistanceTransformTest, ComputeDistanceField_EuclideanAccuracy)
{
    // Create a simple 3x3 mask with center pixel as seed
    std::vector<unsigned char> mask = {0, 0, 0, 0, 255, 0, 0, 0, 0};

    auto distances = computeDistanceField(mask, 3, 3, true);

    ASSERT_EQ(distances.size(), 9U);

    // Verify exact Euclidean distances
    EXPECT_FLOAT_EQ(distances[1 * 3 + 1], 0.0f) << "Center (seed) should have distance 0";

    // Adjacent pixels (distance 1)
    EXPECT_NEAR(distances[0 * 3 + 1], 1.0f, 0.01f) << "Adjacent pixel should have distance 1";
    EXPECT_NEAR(distances[1 * 3 + 0], 1.0f, 0.01f) << "Adjacent pixel should have distance 1";
    EXPECT_NEAR(distances[1 * 3 + 2], 1.0f, 0.01f) << "Adjacent pixel should have distance 1";
    EXPECT_NEAR(distances[2 * 3 + 1], 1.0f, 0.01f) << "Adjacent pixel should have distance 1";

    // Diagonal pixels (distance √2)
    EXPECT_NEAR(distances[0 * 3 + 0], std::sqrt(2.0f), 0.01f) << "Diagonal pixel should have distance √2";
    EXPECT_NEAR(distances[0 * 3 + 2], std::sqrt(2.0f), 0.01f) << "Diagonal pixel should have distance √2";
    EXPECT_NEAR(distances[2 * 3 + 0], std::sqrt(2.0f), 0.01f) << "Diagonal pixel should have distance √2";
    EXPECT_NEAR(distances[2 * 3 + 2], std::sqrt(2.0f), 0.01f) << "Diagonal pixel should have distance √2";
}

TEST_F(DistanceTransformTest, ComputeDistanceField_EdgeCases)
{
    // Test empty mask
    std::vector<unsigned char> emptyMask(100, 0); // All black
    auto distances = computeDistanceField(emptyMask, 10, 10, true);

    ASSERT_EQ(distances.size(), 100U);

    // All distances should be infinite (no seeds inside)
    for (float dist : distances)
    {
        EXPECT_FALSE(std::isfinite(dist)) << "Distance should be infinite when no seeds exist";
    }

    // Test full mask
    std::vector<unsigned char> fullMask(100, 255); // All white
    auto distances2 = computeDistanceField(fullMask, 10, 10, true);

    ASSERT_EQ(distances2.size(), 100U);

    // All distances should be 0 (all pixels are seeds)
    for (float dist : distances2)
    {
        EXPECT_FLOAT_EQ(dist, 0.0f) << "Distance should be 0 when all pixels are seeds";
    }
}

TEST_F(DistanceTransformTest, ComputeDistanceField_InvalidDimensions)
{
    std::vector<unsigned char> mask(4, 0);

    // Test with zero width
    auto distances1 = computeDistanceField(mask, 0, 4, true);
    EXPECT_TRUE(distances1.empty()) << "Zero width should return empty result";

    // Test with zero height
    auto distances2 = computeDistanceField(mask, 4, 0, true);
    EXPECT_TRUE(distances2.empty()) << "Zero height should return empty result";

    // Test with negative dimensions
    auto distances3 = computeDistanceField(mask, -2, 2, true);
    EXPECT_TRUE(distances3.empty()) << "Negative width should return empty result";
}

TEST_F(DistanceTransformTest, ComputeDistanceField_Performance)
{
    const int size = 128;
    std::vector<unsigned char> testMask(size * size, 0);

    // Create a checkerboard pattern for realistic complexity
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            if ((x / 16 + y / 16) % 2 == 0)
            {
                testMask[y * size + x] = 255;
            }
        }
    }

    auto start = std::chrono::high_resolution_clock::now();
    auto distances = computeDistanceField(testMask, size, size, true);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    ASSERT_EQ(distances.size(), static_cast<size_t>(size * size));

    // EDT should be very fast for 128x128 (linear time complexity)
    EXPECT_LT(duration.count(), 5000) << "EDT should complete 128x128 image in under 5ms";

    std::cout << "Performance: " << size << "x" << size << " image processed in " << duration.count() << " μs"
              << std::endl;
}

TEST_F(DistanceTransformTest, BuildSmartFeatherMask_Integration)
{
    // Test that our EDT works correctly with buildSmartFeatherMask function
    auto featheredMask = buildSmartFeatherMask(*simpleSquare_, 1.0f, 3.0f);

    ASSERT_NE(featheredMask, nullptr) << "Smart feather mask should be created successfully";
    EXPECT_EQ(featheredMask->info.width, simpleSquare_->info.width);
    EXPECT_EQ(featheredMask->info.height, simpleSquare_->info.height);
    EXPECT_EQ(featheredMask->info.format, ImageFormat::GRAYSCALE);

    const unsigned char* featheredData = featheredMask->data();
    const int width = static_cast<int>(featheredMask->info.width);
    const int height = static_cast<int>(featheredMask->info.height);

    // Center of original square should have high alpha (close to 255)
    EXPECT_GT(featheredData[5 * width + 5], 200) << "Center should have high alpha";

    // Feathered edges should have intermediate values
    bool hasIntermediateValues = false;
    for (int i = 0; i < width * height; ++i)
    {
        if (featheredData[i] > 50 && featheredData[i] < 200)
        {
            hasIntermediateValues = true;
            break;
        }
    }
    EXPECT_TRUE(hasIntermediateValues) << "Feathered mask should have smooth transitions";

    // Far outside should have alpha close to 0
    EXPECT_LT(featheredData[0 * width + 0], 50) << "Far corners should have low alpha";
}

TEST_F(DistanceTransformTest, BuildSmartFeatherMask_Symmetry)
{
    // Test that the feathered mask has symmetric smoothing on all sides
    auto featheredMask = buildSmartFeatherMask(*simpleSquare_, 1.0f, 3.0f);

    ASSERT_NE(featheredMask, nullptr);
    
    const unsigned char* data = featheredMask->data();
    const int width = static_cast<int>(featheredMask->info.width);
    const int height = static_cast<int>(featheredMask->info.height);

    // Test that all four edges have smoothing (non-zero, non-255 values)
    // Top edge
    bool topEdgeSmoothed = false;
    for (int x = 3; x <= 6; ++x)
    {
        unsigned char val = data[2 * width + x];
        if (val > 10 && val < 245)
        {
            topEdgeSmoothed = true;
            break;
        }
    }
    EXPECT_TRUE(topEdgeSmoothed) << "Top edge should have smooth transition";

    // Bottom edge
    bool bottomEdgeSmoothed = false;
    for (int x = 3; x <= 6; ++x)
    {
        unsigned char val = data[7 * width + x];
        if (val > 10 && val < 245)
        {
            bottomEdgeSmoothed = true;
            break;
        }
    }
    EXPECT_TRUE(bottomEdgeSmoothed) << "Bottom edge should have smooth transition";

    // Left edge
    bool leftEdgeSmoothed = false;
    for (int y = 3; y <= 6; ++y)
    {
        unsigned char val = data[y * width + 2];
        if (val > 10 && val < 245)
        {
            leftEdgeSmoothed = true;
            break;
        }
    }
    EXPECT_TRUE(leftEdgeSmoothed) << "Left edge should have smooth transition";

    // Right edge
    bool rightEdgeSmoothed = false;
    for (int y = 3; y <= 6; ++y)
    {
        unsigned char val = data[y * width + 7];
        if (val > 10 && val < 245)
        {
            rightEdgeSmoothed = true;
            break;
        }
    }
    EXPECT_TRUE(rightEdgeSmoothed) << "Right edge should have smooth transition";
}

