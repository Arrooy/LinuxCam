/**
 * UNIT TESTS FOR IMAGE UTILITIES
 *
 * Tests for image quality metrics and utility functions
 */

#include <gtest/gtest.h>
#include <memory>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/image_utils.h"
#include "../common/test_utils.h"

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
        for (uint32_t y = 0; y < 10; ++y) {
            for (uint32_t x = 0; x < 10; ++x) {
                if ((x + y) % 2 == 0) {
                    // White squares
                    identical1_->ppx(x, y, Pixel(255, 255, 255));
                    identical2_->ppx(x, y, Pixel(255, 255, 255));
                } else {
                    // Black squares (already set, but being explicit)
                    identical1_->ppx(x, y, Pixel(0, 0, 0));
                    identical2_->ppx(x, y, Pixel(0, 0, 0));
                }
            }
        }
        
        // Create a slightly different image (similar checkerboard but with gray instead of white)
        different_ = std::make_unique<Image>(Pixel(0, 0, 0), 10, 10);
        for (uint32_t y = 0; y < 10; ++y) {
            for (uint32_t x = 0; x < 10; ++x) {
                if ((x + y) % 2 == 0) {
                    different_->ppx(x, y, Pixel(128, 128, 128)); // Gray instead of white
                } else {
                    different_->ppx(x, y, Pixel(0, 0, 0));
                }
            }
        }

        // Create a very different image (horizontal stripes vs checkerboard)
        very_different_ = std::make_unique<Image>(Pixel(0, 0, 0), 10, 10);
        for (uint32_t y = 0; y < 10; ++y) {
            for (uint32_t x = 0; x < 10; ++x) {
                if (y % 2 == 0) {
                    very_different_->ppx(x, y, Pixel(255, 0, 0)); // Red horizontal stripes
                } else {
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
