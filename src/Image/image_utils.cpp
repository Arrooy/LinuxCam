/**
 * IMAGE UTILITIES IMPLEMENTATION
 *
 * Implementation of image quality metrics and utility functions
 */

#include "LinuxFace/Image/image_utils.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace linuxface::image_utils
{

// ========== ImageMetrics Implementation ==========

std::string ImageMetrics::toString() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "MSE: " << mse << ", PSNR: " << psnr << "dB, SSIM: " << ssim;
    oss << ", LPIPS: " << lpips << ", Identity: " << identity_similarity;
    return oss.str();
}

// ========== Image Quality Metrics Implementation ==========

double calculateMSE(const Image& img1, const Image& img2) {
    if (img1.info.width != img2.info.width || img1.info.height != img2.info.height) {
        return -1.0; // Invalid comparison - different dimensions
    }
    
    size_t width = img1.info.width;
    size_t height = img1.info.height;
    
    // Handle empty images
    if (width == 0 || height == 0) {
        return -1.0;
    }
    
    double mse = 0.0;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            const Pixel p1 = img1(x, y);
            const Pixel p2 = img2(x, y);
            
            double dr = static_cast<double>(p1.r) - static_cast<double>(p2.r);
            double dg = static_cast<double>(p1.g) - static_cast<double>(p2.g);
            double db = static_cast<double>(p1.b) - static_cast<double>(p2.b);
            
            mse += (dr * dr + dg * dg + db * db) / 3.0;
        }
    }
    
    return mse / (width * height);
}

double calculatePSNR(double mse)
{
    if (mse <= 0.0)
    {
        return 100.0; // Perfect match
    }
    return 20.0 * std::log10(255.0 / std::sqrt(mse));
}

double calculateSSIM(const Image& img1, const Image& img2)
{
    if (img1.info.width != img2.info.width || img1.info.height != img2.info.height)
    {
        return -1.0; // Invalid comparison
    }

    size_t width = img1.info.width;
    size_t height = img1.info.height;
    
    // Handle empty images
    if (width == 0 || height == 0) {
        return -1.0;
    }

    const double C1 = 6.5025;  // (0.01 * 255)^2
    const double C2 = 58.5225; // (0.03 * 255)^2

    // Calculate means
    double mu1 = 0.0, mu2 = 0.0;
    size_t totalPixels = width * height;

    for (size_t y = 0; y < height; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const Pixel p1 = img1(x, y);
            const Pixel p2 = img2(x, y);

            // Convert to grayscale for SSIM calculation
            double gray1 = 0.299 * p1.r + 0.587 * p1.g + 0.114 * p1.b;
            double gray2 = 0.299 * p2.r + 0.587 * p2.g + 0.114 * p2.b;

            mu1 += gray1;
            mu2 += gray2;
        }
    }

    mu1 /= totalPixels;
    mu2 /= totalPixels;

    // Calculate variances and covariance
    double sigma1_sq = 0.0, sigma2_sq = 0.0, sigma12 = 0.0;

    for (size_t y = 0; y < height; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const Pixel p1 = img1(x, y);
            const Pixel p2 = img2(x, y);

            double gray1 = 0.299 * p1.r + 0.587 * p1.g + 0.114 * p1.b;
            double gray2 = 0.299 * p2.r + 0.587 * p2.g + 0.114 * p2.b;

            double diff1 = gray1 - mu1;
            double diff2 = gray2 - mu2;

            sigma1_sq += diff1 * diff1;
            sigma2_sq += diff2 * diff2;
            sigma12 += diff1 * diff2;
        }
    }

    sigma1_sq /= (totalPixels - 1);
    sigma2_sq /= (totalPixels - 1);
    sigma12 /= (totalPixels - 1);

    // Handle single pixel case
    if (totalPixels == 1) {
        // For single pixels, SSIM reduces to intensity comparison
        return (mu1 == mu2) ? 1.0 : 0.0;
    }

    // Calculate SSIM
    double numerator = (2 * mu1 * mu2 + C1) * (2 * sigma12 + C2);
    double denominator = (mu1 * mu1 + mu2 * mu2 + C1) * (sigma1_sq + sigma2_sq + C2);

    return numerator / denominator;
}

double calculateApproximateLPIPS(const Image& img1, const Image& img2)
{
    if (img1.info.width != img2.info.width || img1.info.height != img2.info.height)
    {
        return -1.0; // Invalid comparison
    }

    size_t width = img1.info.width;
    size_t height = img1.info.height;
    
    // Handle empty images
    if (width == 0 || height == 0) {
        return -1.0;
    }

    // For very small images, use simple pixel difference
    if (width < 3 || height < 3) {
        double totalDiff = 0.0;
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                const Pixel p1 = img1(x, y);
                const Pixel p2 = img2(x, y);
                
                double diff = std::abs(static_cast<double>(p1.r) - p2.r) +
                             std::abs(static_cast<double>(p1.g) - p2.g) +
                             std::abs(static_cast<double>(p1.b) - p2.b);
                totalDiff += diff;
            }
        }
        return totalDiff / (width * height * 255.0 * 3.0);
    }

    // Simple gradient-based perceptual difference
    double totalDiff = 0.0;
    size_t validPixels = 0;

    for (size_t y = 1; y < height - 1; ++y)
    {
        for (size_t x = 1; x < width - 1; ++x)
        {
            // Calculate gradients for both images
            const Pixel p1_center = img1(x, y);
            const Pixel p1_right = img1(x + 1, y);
            const Pixel p1_bottom = img1(x, y + 1);

            const Pixel p2_center = img2(x, y);
            const Pixel p2_right = img2(x + 1, y);
            const Pixel p2_bottom = img2(x, y + 1);

            // Convert to grayscale
            double gray1_center = 0.299 * p1_center.r + 0.587 * p1_center.g + 0.114 * p1_center.b;
            double gray1_right = 0.299 * p1_right.r + 0.587 * p1_right.g + 0.114 * p1_right.b;
            double gray1_bottom = 0.299 * p1_bottom.r + 0.587 * p1_bottom.g + 0.114 * p1_bottom.b;

            double gray2_center = 0.299 * p2_center.r + 0.587 * p2_center.g + 0.114 * p2_center.b;
            double gray2_right = 0.299 * p2_right.r + 0.587 * p2_right.g + 0.114 * p2_right.b;
            double gray2_bottom = 0.299 * p2_bottom.r + 0.587 * p2_bottom.g + 0.114 * p2_bottom.b;

            // Calculate gradients
            double grad1_x = gray1_right - gray1_center;
            double grad1_y = gray1_bottom - gray1_center;
            double grad2_x = gray2_right - gray2_center;
            double grad2_y = gray2_bottom - gray2_center;

            // Calculate difference in gradients
            double diff_x = grad1_x - grad2_x;
            double diff_y = grad1_y - grad2_y;
            double gradDiff = std::sqrt(diff_x * diff_x + diff_y * diff_y);

            totalDiff += gradDiff;
            validPixels++;
        }
    }

    if (validPixels == 0) {
        return 0.0;
    }

    double gradientLPIPS = totalDiff / (validPixels * 255.0);
    
    // If gradient difference is very small (solid colors), fall back to pixel difference
    if (gradientLPIPS < 1e-6) {
        double pixelDiff = 0.0;
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                const Pixel p1 = img1(x, y);
                const Pixel p2 = img2(x, y);
                
                double diff = std::abs(static_cast<double>(p1.r) - p2.r) +
                             std::abs(static_cast<double>(p1.g) - p2.g) +
                             std::abs(static_cast<double>(p1.b) - p2.b);
                pixelDiff += diff;
            }
        }
        return pixelDiff / (width * height * 255.0 * 3.0);
    }
    
    return gradientLPIPS;
}

ImageMetrics calculateImageMetrics(const Image& img1, const Image& img2)
{
    ImageMetrics metrics;
    
    metrics.mse = calculateMSE(img1, img2);
    metrics.psnr = calculatePSNR(metrics.mse);
    metrics.ssim = calculateSSIM(img1, img2);
    metrics.lpips = calculateApproximateLPIPS(img1, img2);
    
    // Identity similarity would require a face recognition model
    // For now, we'll leave it as 0.0 or could use SSIM as a proxy
    metrics.identity_similarity = metrics.ssim;
    
    return metrics;
}

} // namespace linuxface::image_utils
