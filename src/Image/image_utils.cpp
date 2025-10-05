/**
 * IMAGE UTILITIES IMPLEMENTATION
 *
 * Implementation of image quality metrics and utility functions
 */

#include "LinuxFace/Image/image_utils.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <queue>
#include <sstream>
#include <vector>
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

double calculateMSE(const Image& img1, const Image& img2)
{
    if (img1.info.width != img2.info.width || img1.info.height != img2.info.height)
    {
        return -1.0; // Invalid comparison - different dimensions
    }

    size_t width = img1.info.width;
    size_t height = img1.info.height;

    // Handle empty images
    if (width == 0 || height == 0)
    {
        return -1.0;
    }

    double mse = 0.0;
    for (size_t y = 0; y < height; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
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
    if (width == 0 || height == 0)
    {
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
    if (totalPixels == 1)
    {
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
    if (width == 0 || height == 0)
    {
        return -1.0;
    }

    // For very small images, use simple pixel difference
    if (width < 3 || height < 3)
    {
        double totalDiff = 0.0;
        for (size_t y = 0; y < height; ++y)
        {
            for (size_t x = 0; x < width; ++x)
            {
                const Pixel p1 = img1(x, y);
                const Pixel p2 = img2(x, y);

                double diff = std::abs(static_cast<double>(p1.r) - p2.r) + std::abs(static_cast<double>(p1.g) - p2.g)
                              + std::abs(static_cast<double>(p1.b) - p2.b);
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

    if (validPixels == 0)
    {
        return 0.0;
    }

    double gradientLPIPS = totalDiff / (validPixels * 255.0);

    // If gradient difference is very small (solid colors), fall back to pixel difference
    if (gradientLPIPS < 1e-6)
    {
        double pixelDiff = 0.0;
        for (size_t y = 0; y < height; ++y)
        {
            for (size_t x = 0; x < width; ++x)
            {
                const Pixel p1 = img1(x, y);
                const Pixel p2 = img2(x, y);

                double diff = std::abs(static_cast<double>(p1.r) - p2.r) + std::abs(static_cast<double>(p1.g) - p2.g)
                              + std::abs(static_cast<double>(p1.b) - p2.b);
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

void softenMaskEdges(Image& mask, int blurRadius)
{
    if (blurRadius <= 0 || mask.info.format != ImageFormat::GRAYSCALE)
    {
        // Edge softening only applies to positive-radius grayscale masks
        return;
    }

    const unsigned long maskWidth = mask.info.width;
    const unsigned long maskHeight = mask.info.height;
    unsigned char* maskData = mask.data();

    if (maskWidth == 0 || maskHeight == 0 || maskData == nullptr)
    {
        return;
    }

    int minX = static_cast<int>(maskWidth);
    int minY = static_cast<int>(maskHeight);
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < static_cast<int>(maskHeight); ++y)
    {
        const size_t rowOffset = static_cast<size_t>(y) * maskWidth;
        for (int x = 0; x < static_cast<int>(maskWidth); ++x)
        {
            if (maskData[rowOffset + x] != 0)
            {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (maxX < minX || maxY < minY)
    {
        return;
    }

    // Limit processing to the tightest ROI to keep work proportional to the visible mask
    const int roiWidth = maxX - minX + 1;
    const int roiHeight = maxY - minY + 1;
    const size_t roiSize = static_cast<size_t>(roiWidth) * static_cast<size_t>(roiHeight);

    std::vector<unsigned char> centerMask(roiSize, 0);
    std::vector<unsigned char> edgeMask(roiSize, 0);
    std::vector<unsigned char> blurredEdge(roiSize, 0);
    std::vector<int> integral((static_cast<size_t>(roiWidth) + 1) * (static_cast<size_t>(roiHeight) + 1), 0);

    for (int y = 0; y < roiHeight; ++y)
    {
        int rowSum = 0;
        const size_t srcRowOffset = static_cast<size_t>(minY + y) * maskWidth + static_cast<size_t>(minX);
        for (int x = 0; x < roiWidth; ++x)
        {
            const int val = maskData[srcRowOffset + x] > 0 ? 1 : 0;
            rowSum += val;
            const size_t integralIdx = static_cast<size_t>(y + 1) * (roiWidth + 1) + static_cast<size_t>(x + 1);
            integral[integralIdx] =
                integral[static_cast<size_t>(y) * (roiWidth + 1) + static_cast<size_t>(x + 1)] + rowSum;
        }
    }

    const int kernelSize = 2 * blurRadius + 1;
    const int kernelArea = kernelSize * kernelSize;

    for (int y = 0; y < roiHeight; ++y)
    {
        const int globalY = minY + y;
        for (int x = 0; x < roiWidth; ++x)
        {
            const int globalX = minX + x;
            const int idx = y * roiWidth + x;
            const size_t srcIdx = static_cast<size_t>(globalY) * maskWidth + static_cast<size_t>(globalX);

            if (x < blurRadius || y < blurRadius || x + blurRadius >= roiWidth || y + blurRadius >= roiHeight)
            {
                // Preserve edge pixels for later blending when the kernel would spill outside the ROI
                if (maskData[srcIdx] > 0)
                {
                    edgeMask[idx] = maskData[srcIdx];
                }
                continue;
            }

            const int x0 = x - blurRadius;
            const int y0 = y - blurRadius;
            const int x1 = x + blurRadius;
            const int y1 = y + blurRadius;

            const size_t a = static_cast<size_t>(y1 + 1) * (roiWidth + 1) + static_cast<size_t>(x1 + 1);
            const size_t b = static_cast<size_t>(y0) * (roiWidth + 1) + static_cast<size_t>(x1 + 1);
            const size_t c = static_cast<size_t>(y1 + 1) * (roiWidth + 1) + static_cast<size_t>(x0);
            const size_t d = static_cast<size_t>(y0) * (roiWidth + 1) + static_cast<size_t>(x0);
            const int sum = integral[a] - integral[b] - integral[c] + integral[d];
            centerMask[idx] = (sum == kernelArea) ? 255 : 0;

            if (maskData[srcIdx] > 0 && centerMask[idx] == 0)
            {
                edgeMask[idx] = maskData[srcIdx];
            }
        }
    }

    std::fill(integral.begin(), integral.end(), 0);
    for (int y = 0; y < roiHeight; ++y)
    {
        int rowSum = 0;
        for (int x = 0; x < roiWidth; ++x)
        {
            const int idx = y * roiWidth + x;
            rowSum += edgeMask[idx];
            const size_t integralIdx = static_cast<size_t>(y + 1) * (roiWidth + 1) + static_cast<size_t>(x + 1);
            integral[integralIdx] =
                integral[static_cast<size_t>(y) * (roiWidth + 1) + static_cast<size_t>(x + 1)] + rowSum;
        }
    }

    for (int y = 0; y < roiHeight; ++y)
    {
        const int globalY = minY + y;
        for (int x = 0; x < roiWidth; ++x)
        {
            const int globalX = minX + x;
            const int idx = y * roiWidth + x;

            const int x0 = std::max(0, x - blurRadius);
            const int y0 = std::max(0, y - blurRadius);
            const int x1 = std::min(roiWidth - 1, x + blurRadius);
            const int y1 = std::min(roiHeight - 1, y + blurRadius);

            const size_t a = static_cast<size_t>(y1 + 1) * (roiWidth + 1) + static_cast<size_t>(x1 + 1);
            const size_t b = static_cast<size_t>(y0) * (roiWidth + 1) + static_cast<size_t>(x1 + 1);
            const size_t c = static_cast<size_t>(y1 + 1) * (roiWidth + 1) + static_cast<size_t>(x0);
            const size_t d = static_cast<size_t>(y0) * (roiWidth + 1) + static_cast<size_t>(x0);
            const int sum = integral[a] - integral[b] - integral[c] + integral[d];
            const int area = (x1 - x0 + 1) * (y1 - y0 + 1);
            const int blurredValue = area > 0 ? sum / area : 0;
            blurredEdge[idx] = static_cast<unsigned char>(std::clamp(blurredValue, 0, 255));

            const size_t dstIdx = static_cast<size_t>(globalY) * maskWidth + static_cast<size_t>(globalX);
            if (centerMask[idx] > 0)
            {
                maskData[dstIdx] = 255;
            }
            else if (blurredEdge[idx] > 0)
            {
                // Blend softened boundary back into the original mask to avoid hard contours
                maskData[dstIdx] = blurredEdge[idx];
            }
            else
            {
                maskData[dstIdx] = 0;
            }
        }
    }
}

// ========== Euclidean Distance Transform Implementation ==========

// Helper function for 1D distance transform using the envelope method
// Based on Felzenszwalb & Huttenlocher's linear-time algorithm
static void transformRow(const std::vector<float>& input, std::vector<float>& output, int width)
{
    // Resize output to match width
    output.resize(width);
    
    // If all values are infinite, fill output with infinity and return
    bool hasFiniteValue = false;
    for (int i = 0; i < width; ++i)
    {
        if (std::isfinite(input[i]))
        {
            hasFiniteValue = true;
            break;
        }
    }
    if (!hasFiniteValue)
    {
        std::fill(output.begin(), output.end(), std::numeric_limits<float>::infinity());
        return;
    }
    
    // Parabola intersections and envelope tracking
    std::vector<int> v(width);         // Parabola indices in lower envelope
    std::vector<float> z(width + 1);   // Intersection points between parabolas
    int k = 0; // Number of parabolas in lower envelope
    
    // Find first finite value to start with
    int firstFinite = 0;
    while (firstFinite < width && !std::isfinite(input[firstFinite]))
    {
        firstFinite++;
    }
    
    if (firstFinite >= width)
    {
        std::fill(output.begin(), output.end(), std::numeric_limits<float>::infinity());
        return; // No finite values
    }
    
    // Initialize with first finite pixel
    v[0] = firstFinite;
    z[0] = -std::numeric_limits<float>::infinity();
    z[1] = std::numeric_limits<float>::infinity();
    
    // Build lower envelope of parabolas
    for (int q = firstFinite + 1; q < width; ++q)
    {
        // Skip infinite values - they don't contribute to the envelope
        if (!std::isfinite(input[q]))
        {
            continue;
        }
        
        // Intersection calculation between parabolas at v[k] and q
        auto intersection = [&](int i, int j) -> float {
            return (input[j] + j * j - input[i] - i * i) / (2.0f * (j - i));
        };
        
        float s = intersection(v[k], q);
        
        // Remove parabolas that are no longer part of the lower envelope
        while (k >= 0 && s <= z[k])
        {
            k--;
            if (k >= 0)
            {
                s = intersection(v[k], q);
            }
        }
        
        k++;
        v[k] = q;
        z[k] = s;
        z[k + 1] = std::numeric_limits<float>::infinity();
    }
    
    // Fill output using the lower envelope
    int envK = 0;
    for (int q = 0; q < width; ++q)
    {
        // Find which parabola in the envelope covers this point
        while (envK < k && z[envK + 1] < q)
        {
            envK++;
        }
        
        const float dx = q - v[envK];
        const float baseValue = input[v[envK]];
        
        // Compute squared distance from this point to the seed at v[envK]
        output[q] = baseValue + dx * dx;
    }
}

// Two-pass linear-time Euclidean Distance Transform
// First pass: transform each row, second pass: transform each column
static std::vector<float>
euclideanDistanceTransform(const std::vector<unsigned char>& binaryMask, int width, int height, bool seedsAreInside)
{
    const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<float> result(size);
    std::vector<float> temp(std::max(width, height));

    // Initialize distance field based on seed pixels
    const float inf = std::numeric_limits<float>::infinity();
    for (size_t idx = 0; idx < size; ++idx)
    {
        const bool isInside = binaryMask[idx] != 0;
        const bool isSeed = (seedsAreInside && isInside) || (!seedsAreInside && !isInside);
        result[idx] = isSeed ? 0.0f : inf;
    }

    // First pass: transform each row
    for (int y = 0; y < height; ++y)
    {
        const size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(width);

        // Copy row to temporary buffer
        for (int x = 0; x < width; ++x)
        {
            temp[x] = result[rowOffset + x];
        }

        // Transform the row
        std::vector<float> rowOutput(width);
        transformRow(temp, rowOutput, width);

        // Copy back to result
        for (int x = 0; x < width; ++x)
        {
            result[rowOffset + x] = rowOutput[x];
        }
    }

    // Second pass: transform each column
    for (int x = 0; x < width; ++x)
    {
        // Copy column to temporary buffer
        for (int y = 0; y < height; ++y)
        {
            temp[y] = result[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
        }

        // Transform the column
        std::vector<float> colOutput(height);
        transformRow(temp, colOutput, height);

        // Copy back to result (taking square root to get actual distances)
        for (int y = 0; y < height; ++y)
        {
            const float sqDist = colOutput[y];
            result[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
                std::isfinite(sqDist) ? std::sqrt(sqDist) : inf;
        }
    }

    return result;
}

std::vector<float>
computeDistanceField(const std::vector<unsigned char>& binaryMask, int width, int height, bool seedsAreInside)
{
    // PERF: Optimized linear-time O(n) Euclidean Distance Transform implementation
    // Replaced Dijkstra-style O(n log n) approach with two-pass EDT for better performance and quality
    // Uses Felzenszwalb & Huttenlocher algorithm for exact Euclidean distances

    if (width <= 0 || height <= 0)
    {
        return {};
    }

    return euclideanDistanceTransform(binaryMask, width, height, seedsAreInside);
}

// Stepped interpolation functions for smooth transitions
// This uses x^3 * (x * (x * 6 - 15) + 10) polynomial
float smootherstep(float edge0, float edge1, float value)
{
    if (edge0 == edge1)
    {
        return value >= edge0 ? 1.0f : 0.0f;
    }

    float t = (value - edge0) / (edge1 - edge0);
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// This uses x^2 * (3 - 2x) polynomial
float smoothstep(float edge0, float edge1, float value)
{
    if (edge0 == edge1)
    {
        return value >= edge0 ? 1.0f : 0.0f;
    }

    float t = (value - edge0) / (edge1 - edge0);
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Builds a smart feathered mask from a binary mask using distance fields
// Inner radius defines the distance from the mask edge where the mask is fully opaque
// Outer radius defines the distance from the mask edge where the mask is fully transparent
// The transition between inner and outer radius is smoothed using a smootherstep function
// Returns a new Image object with the feathered mask, or nullptr on failure
std::unique_ptr<Image> buildSmartFeatherMask(const Image& mask, float innerRadius, float outerRadius)
{
    if (mask.info.format != ImageFormat::GRAYSCALE || mask.info.pixelSizeBytes != 1 || innerRadius <= 0.0f
        || outerRadius <= 0.0f)
    {
        return nullptr;
    }

    if (outerRadius < innerRadius)
    {
        std::swap(outerRadius, innerRadius);
    }

    const int width = static_cast<int>(mask.info.width);
    const int height = static_cast<int>(mask.info.height);
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    const unsigned char* maskData = mask.data();
    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    bool hasInside = false;

    for (int y = 0; y < height; ++y)
    {
        const int rowOffset = y * width;
        for (int x = 0; x < width; ++x)
        {
            if (maskData[static_cast<size_t>(rowOffset + x)] > 0)
            {
                hasInside = true;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (!hasInside || maxX < minX || maxY < minY)
    {
        return mask.deepCopy();
    }

    const float radiusExtent = std::max(innerRadius, outerRadius);
    const int margin = std::max(2, static_cast<int>(std::ceil(radiusExtent)));
    minX = std::max(0, minX - margin);
    minY = std::max(0, minY - margin);
    maxX = std::min(width - 1, maxX + margin);
    maxY = std::min(height - 1, maxY + margin);

    const int roiWidth = maxX - minX + 1;
    const int roiHeight = maxY - minY + 1;
    if (roiWidth <= 0 || roiHeight <= 0)
    {
        return mask.deepCopy();
    }

    std::vector<unsigned char> binary(static_cast<size_t>(roiWidth) * static_cast<size_t>(roiHeight), 0);
    bool hasOutside = false;
    for (int y = 0; y < roiHeight; ++y)
    {
        const int srcY = minY + y;
        const int srcRowOffset = srcY * width;
        for (int x = 0; x < roiWidth; ++x)
        {
            const int srcX = minX + x;
            const unsigned char value = maskData[static_cast<size_t>(srcRowOffset + srcX)];
            if (value > 0)
            {
                binary[static_cast<size_t>(y) * roiWidth + static_cast<size_t>(x)] = 1;
            }
            else
            {
                hasOutside = true;
            }
        }
    }

    if (!hasOutside)
    {
        return mask.deepCopy();
    }

    // Compute distance fields:
    // - distFromOutside: distance from black pixels (outside), gives distance to boundary for inside points
    // - distFromInside: distance from white pixels (inside), gives distance to boundary for outside points
    auto distFromOutside = computeDistanceField(binary, roiWidth, roiHeight, false);
    auto distFromInside = computeDistanceField(binary, roiWidth, roiHeight, true);

    auto smartMask = mask.deepCopy();
    if (!smartMask)
    {
        return nullptr;
    }

    unsigned char* output = smartMask->data();
    if (!output)
    {
        return nullptr;
    }

    std::fill(output, output + smartMask->size(), static_cast<unsigned char>(0));

    const float fallbackDistance = innerRadius + outerRadius;
    constexpr float kMinInnerAlpha = 0.2f;

    for (int y = 0; y < roiHeight; ++y)
    {
        const int srcY = minY + y;
        const size_t srcRowOffset = static_cast<size_t>(srcY) * static_cast<size_t>(width);
        for (int x = 0; x < roiWidth; ++x)
        {
            const size_t idx = static_cast<size_t>(y) * roiWidth + static_cast<size_t>(x);
            const bool isInside = binary[idx] == 1;
            
            // Get distances to boundary
            float distOut = distFromOutside[idx];
            float distIn = distFromInside[idx];

            if (!std::isfinite(distOut))
            {
                distOut = fallbackDistance;
            }
            if (!std::isfinite(distIn))
            {
                distIn = fallbackDistance;
            }

            // Compute signed distance:
            // - For inside points: positive (distance inward from boundary)
            // - For outside points: negative (distance outward from boundary)
            const float signedDistance = isInside ? distOut : -distIn;
            const size_t dstIndex = srcRowOffset + static_cast<size_t>(minX + x);

            if (signedDistance >= innerRadius)
            {
                output[dstIndex] = 255;
                continue;
            }

            if (signedDistance <= -outerRadius)
            {
                output[dstIndex] = 0;
                continue;
            }

            float alpha = smootherstep(-outerRadius, innerRadius, signedDistance);
            if (isInside)
            {
                alpha = std::max(alpha, kMinInnerAlpha);
            }
            alpha = std::clamp(alpha, 0.0f, 1.0f);

            output[dstIndex] = static_cast<unsigned char>(std::round(alpha * 255.0f));
        }
    }

    return smartMask;
}

} // namespace linuxface::image_utils
