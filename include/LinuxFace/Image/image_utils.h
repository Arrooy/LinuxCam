#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/math_utils.h"

namespace linuxface::image_utils
{

// ========== Image Quality Metrics ==========

/**
 * Structure to hold various image quality metrics
 */
struct ImageMetrics
{
    double mse = 0.0;                 // Mean Squared Error
    double psnr = 0.0;                // Peak Signal-to-Noise Ratio (dB)
    double ssim = 0.0;                // Structural Similarity Index
    double lpips = 0.0;               // Learned Perceptual Image Patch Similarity
    double identity_similarity = 0.0; // Identity similarity score

    std::string toString() const;
};

/**
 * Calculate Mean Squared Error between two images
 * @param img1 First image for comparison
 * @param img2 Second image for comparison
 * @return MSE value, or -1.0 if images have different dimensions
 */
double calculateMSE(const Image& img1, const Image& img2);

/**
 * Calculate Peak Signal-to-Noise Ratio from MSE
 * @param mse Mean squared error value
 * @return PSNR in decibels
 */
double calculatePSNR(double mse);

/**
 * Calculate Structural Similarity Index (SSIM) between two images
 * @param img1 First image for comparison
 * @param img2 Second image for comparison
 * @return SSIM value between -1 and 1, or -1.0 if images have different dimensions
 */
double calculateSSIM(const Image& img1, const Image& img2);

/**
 * Calculate approximate LPIPS (Learned Perceptual Image Patch Similarity)
 * This is a simplified version using gradient-based features
 * @param img1 First image for comparison
 * @param img2 Second image for comparison
 * @return Approximate LPIPS value, or -1.0 if images have different dimensions
 */
double calculateApproximateLPIPS(const Image& img1, const Image& img2);

/**
 * Calculate comprehensive image metrics between two images
 * @param img1 First image for comparison
 * @param img2 Second image for comparison
 * @return ImageMetrics structure with all calculated metrics
 */
ImageMetrics calculateImageMetrics(const Image& img1, const Image& img2);

// ========== Face Alignment Templates ==========

const double TEMPLATE_112[5][2] = {
    {0.34191607, 0.46157411},
    {0.65653393, 0.45983393},
    {0.50022500, 0.64050536},
    {0.37097589, 0.82469196},
    {0.63151696, 0.82325089}
};

const double TEMPLATE_128[5][2] = {
    {0.36167656, 0.40387734},
    {0.63696719, 0.40235469},
    {0.50019687, 0.56044219},
    {0.38710391, 0.72160547},
    {0.61507734, 0.72034453}
};

const double TEMPLATE_192_OLD[5][2] = {
    {0.40625,  0.390625},
    {0.59375,  0.390625},
    {0.5,      0.46875 },
    {0.442708, 0.598958},
    {0.557292, 0.598958}
};
const double TEMPLATE_192[5][2] = {
    {0.35546875, 0.396484375},
    {0.64453125, 0.396484375},
    {0.5,        0.482421875},
    {0.37109375, 0.611328125},
    {0.62890625, 0.611328125}
};

// Alternative template with slightly different proportions
// that may work better with certain face shapes
const double TEMPLATE_192_ALT[5][2] = {
    {0.36328125, 0.40234375},
    {0.63671875, 0.40234375},
    {0.5,        0.48828125},
    {0.3828125,  0.61328125},
    {0.6171875,  0.61328125}
};

// const double TEMPLATE_256[5][2] = {
//     {0.37, 0.40},
//     {0.63, 0.40},
//     {0.50, 0.50},
//     {0.37, 0.70},
//     {0.63, 0.70}
// };

const double TEMPLATE_256[5][2] = {
    {0.36167656, 0.40387734},  // left eye
    {0.63696719, 0.40235469},  // right eye
    {0.50019687, 0.56044219},  // nose
    {0.38710391, 0.72160547},  // left mouth corner
    {0.61507734, 0.72034453}   // right mouth corner
};

const double TEMPLATE_256_OPTIMIZED[5][2] = {
    // Optimized coordinates that match face structure better
    {0.35, 0.38}, {0.65, 0.38}, {0.50, 0.48}, 
    {0.38, 0.58}, {0.62, 0.58}
};

const double TEMPLATE_512[5][2] = {
    {0.37691676, 0.46864664},
    {0.62285697, 0.46912813},
    {0.50123859, 0.61331904},
    {0.39308822, 0.72541100},
    {0.61150205, 0.72490465}
};

// Helper function to calculate destination index based on layout
template <ImageLayout layout>
constexpr size_t calculateDestIndex(unsigned long y, unsigned long x, unsigned char ch, unsigned long width,
                                    unsigned long height, unsigned char channels)
{
    if constexpr (layout == ImageLayout::HWC)
    {
        return (y * width + x) * channels + ch;
    }
    else // CHW
    {
        return ch * (height * width) + y * width + x;
    }
}

inline std::pair<std::unique_ptr<Image>, std::array<double, 6>>
faceTransform(const std::vector<math_utils::Point<>>& landmarks, const double templatePoints[5][2], int targetSize,
              const std::function<bool(const double*, const double*, int, double*)>& estimateTransform,
              const std::function<std::unique_ptr<Image>(const double*, int, int)>& warpFn, bool alignToTemplate = true)
{
    if (landmarks.size() != 5)
    {
        return std::make_pair(nullptr, std::array<double, 6>{1.0, 0.0, 0.0, 0.0, 1.0, 0.0});
    }

    double src[10];
    double dst[10];
    for (int i = 0; i < 5; ++i)
    {
        if (alignToTemplate)
        {
            src[2 * i] = static_cast<double>(landmarks[i].x);
            src[2 * i + 1] = static_cast<double>(landmarks[i].y);
            dst[2 * i] = static_cast<double>(templatePoints[i][0] * targetSize);
            dst[2 * i + 1] = static_cast<double>(templatePoints[i][1] * targetSize);
        }
        else
        {
            src[2 * i] = static_cast<double>(templatePoints[i][0] * targetSize);
            src[2 * i + 1] = static_cast<double>(templatePoints[i][1] * targetSize);
            dst[2 * i] = static_cast<double>(landmarks[i].x);
            dst[2 * i + 1] = static_cast<double>(landmarks[i].y);
        }
    }
    double m[6] = {0};
    estimateTransform(src, dst, 5, m);
    std::array<double, 6> arrM{};
    for (int i = 0; i < 6; ++i)
    {
        arrM[i] = m[i];
    }
    return {warpFn(m, targetSize, targetSize), arrM};
}

// Align or unalign face using 5 landmarks and a template (returns nullptr if not possible)
// Now returns both the aligned image and the affine matrix used
// Supports RGB to RGB, RGBA to RGB, and RGBA to RGBA transformations based on targetFormat
inline std::pair<std::unique_ptr<Image>, std::array<double, 6>>
affineFaceTransform(const Image& inputImg, const std::vector<math_utils::Point<>>& landmarks,
                    const double templatePoints[5][2], int targetSize, bool alignToTemplate = true,
                    ImageFormat targetFormat = ImageFormat::RGB)
{
    return faceTransform(
        landmarks, templatePoints, targetSize, math_utils::estimateAffine2d,
        [&](const double* m, int outWidth, int outHeight)
        { return inputImg.affineWarpBilinear(m, outWidth, outHeight, nullptr, targetFormat); }, alignToTemplate);
}

inline std::pair<std::unique_ptr<Image>, std::array<double, 6>>
similarityFaceTransform(const Image& inputImg, const std::vector<math_utils::Point<>>& landmarks,
                        const double templatePoints[5][2], int targetSize, bool alignToTemplate = true,
                        ImageFormat targetFormat = ImageFormat::RGB)
{
    return faceTransform(
        landmarks, templatePoints, targetSize, math_utils::estimateSimilarity2d,
        [&](const double* m, int outWidth, int outHeight)
        { return inputImg.affineWarpBilinear(m, outWidth, outHeight, nullptr, targetFormat); }, alignToTemplate);
}

inline std::pair<std::unique_ptr<Image>, std::array<double, 6>>
procrustesSimilarityFaceTransform(const Image& inputImg, const std::vector<math_utils::Point<>>& landmarks,
                                  const double templatePoints[5][2], int targetSize, bool alignToTemplate = true,
                                  ImageFormat targetFormat = ImageFormat::RGB)
{
    return faceTransform(
        landmarks, templatePoints, targetSize, math_utils::estimateProcrustesSimilarity,
        [&](const double* m, int outWidth, int outHeight)
        { return inputImg.affineWarpBilinear(m, outWidth, outHeight, nullptr, targetFormat); }, alignToTemplate);
}

/**
 * Eliptical mask
 *  // Fill the face region in the mask
    int mask_left = std::max(0, (int)min_x);
    int mask_top = std::max(0, (int)min_y);
    int mask_right = std::min(image->info.width, (int)(min_x + out_width));
    int mask_bottom = std::min(image->info.height, (int)(min_y + out_height));

    for (int y = mask_top; y < mask_bottom; ++y) {
        for (int x = mask_left; x < mask_right; ++x) {
            // Create a smooth circular/elliptical mask
            double cx = min_x + out_width / 2.0;
            double cy = min_y + out_height / 2.0;
            double dx = (x - cx) / (out_width / 2.0);
            double dy = (y - cy) / (out_height / 2.0);
            double dist = sqrt(dx*dx + dy*dy);

            if (dist <= 1.0) {
                // Smooth falloff at edges
                double alpha = dist < 0.8 ? 1.0 : (1.0 - (dist - 0.8) / 0.2);
                full_mask->data()[y * image->info.width + x] =
                    static_cast<unsigned char>(255 * std::max(0.0, alpha));
            }
        }
    }
 */

/*
Original: very very slow.
*/
// Create a mask for the crop size using the Image class

// inline std::unique_ptr<Image> create_static_box_mask(const std::vector<double>& crop_size)
// {
//     int width = static_cast<int>(crop_size[0]);
//     int height = static_cast<int>(crop_size[1]);
//     double face_mask_blur = 0.4;
//     std::vector<int> face_mask_padding = {0, 0, 0, 0};
//     int blur_amount = static_cast<int>(width * 0.5 * face_mask_blur);
//     int blur_area = std::max(blur_amount / 2, 1);
//     std::unique_ptr<Image> box_mask = std::make_unique<Image>(width * height);
//     box_mask->info.width = width;
//     box_mask->info.height = height;
//     box_mask->info.pixelSizeBytes = 1;
//     box_mask->info.format = linuxface::ImageFormat::GRAYSCALE;

//     std::fill(box_mask->data(), box_mask->data() + width * height, 255);

//     int top_padding = std::max(blur_area, static_cast<int>(height * face_mask_padding[0] / 100.0));
//     int bottom_padding = std::max(blur_area, static_cast<int>(height * face_mask_padding[2] / 100.0));
//     int right_padding = std::max(blur_area, static_cast<int>(width * face_mask_padding[1] / 100.0));
//     int left_padding = std::max(blur_area, static_cast<int>(width * face_mask_padding[3] / 100.0));
//     for (int y = 0; y < top_padding; ++y)
//     {
//         std::fill(box_mask->data() + y * width, box_mask->data() + (y + 1) * width, 0);
//     }
//     for (int y = height - bottom_padding; y < height; ++y)
//     {
//         std::fill(box_mask->data() + y * width, box_mask->data() + (y + 1) * width, 0);
//     }
//     for (int y = 0; y < height; ++y)
//     {
//         std::fill(box_mask->data() + y * width, box_mask->data() + y * width + left_padding, 0);
//     }
//     for (int y = 0; y < height; ++y)
//     {
//         std::fill(box_mask->data() + y * width + (width - right_padding), box_mask->data() + (y + 1) * width, 0);
//     }
//     if (blur_amount > 0)
//     {
//         std::unique_ptr<Image> blurred = box_mask->deepCopy();
//         int radius = blur_amount / 2;
//         for (int y = 0; y < height; ++y)
//         {
//             for (int x = 0; x < width; ++x)
//             {
//                 int sum = 0, count = 0;
//                 for (int dy = -radius; dy <= radius; ++dy)
//                 {
//                     for (int dx = -radius; dx <= radius; ++dx)
//                     {
//                         int nx = x + dx, ny = y + dy;
//                         if (nx >= 0 && nx < width && ny >= 0 && ny < height)
//                         {
//                             sum += box_mask->data()[ny * width + nx];
//                             ++count;
//                         }
//                     }
//                 }
//                 blurred->data()[y * width + x] = static_cast<unsigned char>(sum / count);
//             }
//         }
//         blurred->info.pixelSizeBytes = 1;
//         blurred->info.format = linuxface::ImageFormat::GRAYSCALE;
//         return blurred;
//     }
//     return box_mask;
// }

/*GPT optimized.

// Create a mask for the crop size using the Image class
inline std::unique_ptr<Image> create_static_box_mask(const std::vector<double>& crop_size)
{
    int width = static_cast<int>(crop_size[0]);
    int height = static_cast<int>(crop_size[1]);
    double face_mask_blur = 0.7;
    std::vector<int> face_mask_padding = {0, 0, 0, 0}; // top, right, bottom, left

    int blur_amount = static_cast<int>(width * 0.5 * face_mask_blur);
    int radius = std::max(blur_amount / 2, 1);

    std::unique_ptr<Image> box_mask = std::make_unique<Image>(width * height);
    box_mask->info.width = width;
    box_mask->info.height = height;
    box_mask->info.pixelSizeBytes = 1;
    box_mask->info.format = linuxface::ImageFormat::GRAYSCALE;

    std::fill(box_mask->data(), box_mask->data() + width * height, 255);

    int top_padding = std::max(radius, static_cast<int>(height * face_mask_padding[0] / 100.0));
    int bottom_padding = std::max(radius, static_cast<int>(height * face_mask_padding[2] / 100.0));
    int right_padding = std::max(radius, static_cast<int>(width * face_mask_padding[1] / 100.0));
    int left_padding = std::max(radius, static_cast<int>(width * face_mask_padding[3] / 100.0));

    // Mask top and bottom areas to 0
    for (int y = 0; y < top_padding; ++y)
        std::fill(box_mask->data() + y * width, box_mask->data() + (y + 1) * width, 0);
    for (int y = height - bottom_padding; y < height; ++y)
        std::fill(box_mask->data() + y * width, box_mask->data() + (y + 1) * width, 0);

    // Mask left and right sides to 0
    for (int y = 0; y < height; ++y)
    {
        std::fill(box_mask->data() + y * width, box_mask->data() + y * width + left_padding, 0);
        std::fill(box_mask->data() + y * width + (width - right_padding), box_mask->data() + (y + 1) * width, 0);
    }

    // Early return if no blur needed
    if (blur_amount <= 0)
    {
        common::logWarn("Blur amount is zero, returning unblurred box mask.");
        return box_mask;
    }
    std::unique_ptr<Image> blurred = box_mask->deepCopy();
    unsigned char* src = box_mask->data();
    unsigned char* dst = blurred->data();

    // Generic 1D blur using sliding window
    auto blur_1d = [](const unsigned char* src, unsigned char* dst, int length, int count, int stride, int radius) {
        const int window = 2 * radius + 1;
        for (int i = 0; i < count; ++i) {
            int sum = 0;

            // Initial window sum
            for (int k = 0; k <= radius && k < length; ++k)
                sum += src[i * stride + k];

            for (int j = 0; j < length; ++j) {
                dst[i * stride + j] = static_cast<unsigned char>(sum / window);

                if (j - radius >= 0)
                    sum -= src[i * stride + j - radius];

                if (j + radius + 1 < length)
                    sum += src[i * stride + j + radius + 1];
            }
        }
    };

    // Blur top and bottom rows (horizontal)
    blur_1d(src, dst, width, top_padding, width, radius);
    blur_1d(src + (height - bottom_padding) * width, dst + (height - bottom_padding) * width, width, bottom_padding,
width, radius);

    // Blur left and right columns (vertical)
    for (int x = 0; x < left_padding; ++x)
        blur_1d(src + x, dst + x, height, 1, width, radius);
    for (int x = width - right_padding; x < width; ++x)
        blur_1d(src + x, dst + x, height, 1, width, radius);

    // Copy center (non-blurred) region from original
    for (int y = top_padding; y < height - bottom_padding; ++y)
    {
        std::copy(
            box_mask->data() + y * width + left_padding,
            box_mask->data() + y * width + (width - right_padding),
            blurred->data() + y * width + left_padding
        );
    }

    blurred->info.pixelSizeBytes = 1;
    blurred->info.format = linuxface::ImageFormat::GRAYSCALE;
    return blurred;
}
*/
/*GPT original version with only blur on padding areas.*/
/*
inline std::unique_ptr<Image> create_static_box_mask(const std::vector<double>& crop_size)
{
    int width = static_cast<int>(crop_size[0]);
    int height = static_cast<int>(crop_size[1]);
    double face_mask_blur = 0.45;
    std::vector<int> face_mask_padding = {0, 0, 20, 0}; // top, right, bottom, left

    int blur_amount = static_cast<int>(width * 0.5 * face_mask_blur);
    int radius = std::max(blur_amount / 2, 1);

    std::unique_ptr<Image> box_mask = std::make_unique<Image>(width * height);
    box_mask->info.width = width;
    box_mask->info.height = height;
    box_mask->info.pixelSizeBytes = 1;
    box_mask->info.format = linuxface::ImageFormat::GRAYSCALE;

    std::fill(box_mask->data(), box_mask->data() + width * height, 255);

    int top_padding = std::max(radius, static_cast<int>(height * face_mask_padding[0] / 100.0));
    int bottom_padding = std::max(radius, static_cast<int>(height * face_mask_padding[2] / 100.0));
    int right_padding = std::max(radius, static_cast<int>(width * face_mask_padding[1] / 100.0));
    int left_padding = std::max(radius, static_cast<int>(width * face_mask_padding[3] / 100.0));

    // Fill black areas
    for (int y = 0; y < top_padding; ++y)
        std::fill(box_mask->data() + y * width, box_mask->data() + (y + 1) * width, 0);
    for (int y = height - bottom_padding; y < height; ++y)
        std::fill(box_mask->data() + y * width, box_mask->data() + (y + 1) * width, 0);
    for (int y = 0; y < height; ++y) {
        std::fill(box_mask->data() + y * width, box_mask->data() + y * width + left_padding, 0);
        std::fill(box_mask->data() + y * width + (width - right_padding), box_mask->data() + (y + 1) * width, 0);
    }

    if (blur_amount <= 0)
        return box_mask;

    std::unique_ptr<Image> blurred = box_mask->deepCopy();
    unsigned char* src = box_mask->data();
    unsigned char* dst = blurred->data();

    // Blur only perimeter
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            // Skip center region
            if (y >= top_padding && y < height - bottom_padding &&
                x >= left_padding && x < width - right_padding)
                continue;

            int sum = 0, count = 0;
            for (int dy = -radius; dy <= radius; ++dy)
            {
                int ny = y + dy;
                if (ny < 0 || ny >= height) continue;

                for (int dx = -radius; dx <= radius; ++dx)
                {
                    int nx = x + dx;
                    if (nx < 0 || nx >= width) continue;

                    sum += src[ny * width + nx];
                    ++count;
                }
            }

            dst[y * width + x] = static_cast<unsigned char>(sum / count);
        }
    }

    blurred->info.pixelSizeBytes = 1;
    blurred->info.format = linuxface::ImageFormat::GRAYSCALE;
    return blurred;
}*/

inline void fastBoxBlur(const unsigned char* src, unsigned char* dst, int width, int height, int radius)
{
    std::vector<unsigned char> temp(width * height, 0);

    // Horizontal pass
    for (int y = 0; y < height; ++y)
    {
        int sum = 0;
        for (int x = -radius; x <= radius; ++x)
        {
            sum += src[y * width + std::clamp(x, 0, width - 1)];
        }
        for (int x = 0; x < width; ++x)
        {
            temp[y * width + x] = static_cast<unsigned char>(sum / (2 * radius + 1));
            const int left = x - radius;
            const int right = x + radius + 1;
            if (left >= 0)
            {
                sum -= src[y * width + left];
            }
            if (right < width)
            {
                sum += src[y * width + right];
            }
        }
    }

    // Vertical pass
    for (int x = 0; x < width; ++x)
    {
        int sum = 0;
        for (int y = -radius; y <= radius; ++y)
        {
            sum += temp[std::clamp(y, 0, height - 1) * width + x];
        }
        for (int y = 0; y < height; ++y)
        {
            dst[y * width + x] = static_cast<unsigned char>(sum / (2 * radius + 1));
            const int top = y - radius;
            const int bottom = y + radius + 1;
            if (top >= 0)
            {
                sum -= temp[top * width + x];
            }
            if (bottom < height)
            {
                sum += temp[bottom * width + x];
            }
        }
    }
}

inline std::unique_ptr<Image> createStaticBoxMask(const std::vector<double>& cropSize)
{
    const int width = static_cast<int>(cropSize[0]);
    const int height = static_cast<int>(cropSize[1]);
    const double faceMaskBlur = 0.6;
    std::vector<int> faceMaskPadding = {0, 0, 0, 0};
    const int blurAmount = static_cast<int>(width * 0.5 * faceMaskBlur);
    const int blurArea = std::max(blurAmount / 2, 1);

    std::unique_ptr<Image> boxMask = std::make_unique<Image>(width * height);
    boxMask->info.width = width;
    boxMask->info.height = height;
    boxMask->info.pixelSizeBytes = 1;
    boxMask->info.format = linuxface::ImageFormat::GRAYSCALE;
    std::fill(boxMask->data(), boxMask->data() + width * height, 255);

    const int top = std::max(blurArea, static_cast<int>(height * faceMaskPadding[0] / 100.0));
    const int bottom = std::max(blurArea, static_cast<int>(height * faceMaskPadding[2] / 100.0));
    const int right = std::max(blurArea, static_cast<int>(width * faceMaskPadding[1] / 100.0));
    const int left = std::max(blurArea, static_cast<int>(width * faceMaskPadding[3] / 100.0));

    // Zero out padding regions
    for (int y = 0; y < top; ++y)
    {
        std::fill(boxMask->data() + y * width, boxMask->data() + (y + 1) * width, 0);
    }
    for (int y = height - bottom; y < height; ++y)
    {
        std::fill(boxMask->data() + y * width, boxMask->data() + (y + 1) * width, 0);
    }
    for (int y = 0; y < height; ++y)
    {
        std::fill(boxMask->data() + y * width, boxMask->data() + y * width + left, 0);
        std::fill(boxMask->data() + y * width + (width - right), boxMask->data() + (y + 1) * width, 0);
    }

    if (blurAmount > 0)
    {
        std::unique_ptr<Image> blurred = boxMask->deepCopy();
        fastBoxBlur(boxMask->data(), blurred->data(), width, height, blurAmount / 2);
        blurred->info.pixelSizeBytes = 1;
        blurred->info.format = linuxface::ImageFormat::GRAYSCALE;
        return blurred;
    }

    return boxMask;
}

// Normalization traits for different types
template <typename T>
struct NormalizationTraits
{
    static constexpr T minValue() { return std::numeric_limits<T>::min(); }
    static constexpr T maxValue() { return std::numeric_limits<T>::max(); }
    static constexpr T zeroValue() { return T(0); }
};

// Specialization for floating point types
template <>
struct NormalizationTraits<float>
{
    static constexpr float minValue() { return 0.0f; }
    static constexpr float maxValue() { return 1.0f; }
    static constexpr float zeroValue() { return 0.0f; }
};

template <>
struct NormalizationTraits<double>
{
    static constexpr double minValue() { return 0.0; }
    static constexpr double maxValue() { return 1.0; }
    static constexpr double zeroValue() { return 0.0; }
};

// Statistics structure for normalization
template <typename T>
struct ImageStats
{
    T min_val;
    T max_val;
    double mean{0.0};
    size_t count{0};

    ImageStats() : min_val(std::numeric_limits<T>::max()), max_val(std::numeric_limits<T>::lowest()) {}

    void update(T value)
    {
        min_val = std::min(min_val, value);
        max_val = std::max(max_val, value);
        mean += value;
        count++;
    }

    void finalize()
    {
        if (count > 0)
        {
            mean /= count;
        }
    }
};

// Normalization functors
template <typename T, NormalizationType Type>
struct Normalizer
{
    T operator()(T value, const ImageStats<T>& /*stats*/) const
    {
        return value; // NONE case
    }
};

template <typename T>
struct Normalizer<T, NormalizationType::MINMAX>
{
    T operator()(T value, const ImageStats<T>& stats) const
    {
        if (stats.max_val == stats.min_val)
        {
            return NormalizationTraits<T>::zeroValue();
        }

        const double normalized =
            static_cast<double>(value - stats.min_val) / static_cast<double>(stats.max_val - stats.min_val);

        if constexpr (std::is_floating_point_v<T>)
        {
            return static_cast<T>(normalized);
        }
        else
        {
            return static_cast<T>(normalized * (NormalizationTraits<T>::maxValue() - NormalizationTraits<T>::minValue())
                                  + NormalizationTraits<T>::minValue());
        }
    }
};

template <typename T>
struct Normalizer<T, NormalizationType::ZERO_CENTER>
{
    T operator()(T value, const ImageStats<T>& stats) const
    {
        double centered = static_cast<double>(value) - stats.mean;

        if constexpr (std::is_floating_point_v<T>)
        {
            return static_cast<T>(centered);
        }
        else
        {
            // For integer types, clamp to valid range
            double range = static_cast<double>(NormalizationTraits<T>::maxValue())
                           - static_cast<double>(NormalizationTraits<T>::minValue());
            centered = std::clamp(centered, -range / 2.0, range / 2.0);
            return static_cast<T>(centered + (range / 2.0 + NormalizationTraits<T>::minValue()));
        }
    }
};

template <typename T>
struct ImageView
{
    T* data;
    unsigned long width;
    unsigned long height;
    unsigned char pixelBytes;
};


template <typename T, typename K, NormalizationType normalizationType = NormalizationType::NONE,
          ImageLayout outputLayout = ImageLayout::HWC>
void bilinearScaling(const ImageView<T>& src, ImageView<K>& dst)
{
    // Handle edge cases
    if (src.width == 0 || src.height == 0 || dst.width == 0 || dst.height == 0)
    {
        common::logError("Invalid image dimensions for scaling: src(%lux%lu), dst(%lux%lu)", src.width, src.height,
                         dst.width, dst.height);
        return;
    }

    // Pre-calculate scaling ratios for better performance
    const double xRatio = static_cast<double>(src.width) / dst.width;
    const double yRatio = static_cast<double>(src.height) / dst.height;

    ImageStats<K> stats;
    const bool needsStats = (normalizationType != NormalizationType::NONE);

    // Process in blocks for better cache locality
    const size_t blockSize = 64; // Process 64x64 blocks

    for (unsigned long blockY = 0; blockY < dst.height; blockY += blockSize)
    {
        for (unsigned long blockX = 0; blockX < dst.width; blockX += blockSize)
        {
            const unsigned long endY = std::min(blockY + blockSize, dst.height);
            const unsigned long endX = std::min(blockX + blockSize, dst.width);

            for (unsigned long y = blockY; y < endY; y++)
            {
                for (unsigned long x = blockX; x < endX; x++)
                {
                    // Calculate source coordinates using pixel center mapping
                    // Map destination pixel center to source pixel center
                    const double srcX = (x + 0.5) * xRatio - 0.5;
                    const double srcY = (y + 0.5) * yRatio - 0.5;

                    // Get integer and fractional parts
                    const int x1 = static_cast<int>(std::floor(srcX));
                    const int y1 = static_cast<int>(std::floor(srcY));
                    const int x2 = x1 + 1;
                    const int y2 = y1 + 1;

                    const double fracX = srcX - x1;
                    const double fracY = srcY - y1;

                    // Bilinear interpolation for each channel
                    for (unsigned char ch = 0; ch < dst.pixelBytes; ch++)
                    {
                        // Determine source channel
                        unsigned char srcCh = ch;
                        if (srcCh >= src.pixelBytes)
                        {
                            if (ch == 3 && dst.pixelBytes == 4 && src.pixelBytes < 4)
                            {
                                // Alpha channel for RGBA output when source has no alpha
                                const double alphaValue = 255.0;
                                K scaledValue;
                                if constexpr (std::is_floating_point_v<K>)
                                {
                                    scaledValue = static_cast<K>(alphaValue);
                                }
                                else
                                {
                                    const auto minVal = static_cast<double>(NormalizationTraits<K>::minValue());
                                    const auto maxVal = static_cast<double>(NormalizationTraits<K>::maxValue());
                                    scaledValue = static_cast<K>(std::clamp(alphaValue + 0.5, minVal, maxVal));
                                }
                                const size_t dstIdx =
                                    calculateDestIndex<outputLayout>(y, x, ch, dst.width, dst.height, dst.pixelBytes);
                                dst.data[dstIdx] = scaledValue;
                                if (needsStats)
                                {
                                    stats.update(scaledValue);
                                }
                                continue;
                            }
                            else
                            {
                                // Duplicate last available channel (e.g., grayscale to RGB)
                                srcCh = src.pixelBytes - 1;
                            }
                        }

                        // Handle boundary conditions with clamping
                        const int clampedX1 = std::clamp(x1, 0, static_cast<int>(src.width - 1));
                        const int clampedY1 = std::clamp(y1, 0, static_cast<int>(src.height - 1));
                        const int clampedX2 = std::clamp(x2, 0, static_cast<int>(src.width - 1));
                        const int clampedY2 = std::clamp(y2, 0, static_cast<int>(src.height - 1));

                        // Get pixel values with proper boundary handling
                        const unsigned long idx1 = (clampedY1 * src.width + clampedX1) * src.pixelBytes + srcCh;
                        const unsigned long idx2 = (clampedY1 * src.width + clampedX2) * src.pixelBytes + srcCh;
                        const unsigned long idx3 = (clampedY2 * src.width + clampedX1) * src.pixelBytes + srcCh;
                        const unsigned long idx4 = (clampedY2 * src.width + clampedX2) * src.pixelBytes + srcCh;

                        const auto p1 = static_cast<double>(src.data[idx1]);
                        const auto p2 = static_cast<double>(src.data[idx2]);
                        const auto p3 = static_cast<double>(src.data[idx3]);
                        const auto p4 = static_cast<double>(src.data[idx4]);

                        // Optimized bilinear interpolation
                        const double top = p1 + fracX * (p2 - p1);
                        const double bottom = p3 + fracX * (p4 - p3);
                        const double result = top + fracY * (bottom - top);

                        K scaledValue;
                        if constexpr (std::is_floating_point_v<K>)
                        {
                            scaledValue = static_cast<K>(result);
                        }
                        else
                        {
                            // Use traits for proper clamping range
                            const auto minVal = static_cast<double>(NormalizationTraits<K>::minValue());
                            const auto maxVal = static_cast<double>(NormalizationTraits<K>::maxValue());
                            scaledValue = static_cast<K>(std::clamp(result + 0.5, minVal, maxVal));
                        }
                        // Calculate destination index based on output layout
                        const size_t dstIdx =
                            calculateDestIndex<outputLayout>(y, x, ch, dst.width, dst.height, dst.pixelBytes);

                        dst.data[dstIdx] = scaledValue;

                        // Collect statistics if needed
                        if (needsStats)
                        {
                            stats.update(scaledValue);
                        }
                    }
                }
            }
        }
    }

    // Step 2: Apply normalization if needed
    if constexpr (normalizationType != NormalizationType::NONE)
    {
        stats.finalize();
        Normalizer<K, normalizationType> normalizer;

        const size_t totalPixels = dst.width * dst.height * dst.pixelBytes;
        for (size_t i = 0; i < totalPixels; i++)
        {
            dst.data[i] = normalizer(dst.data[i], stats);
        }
    }
}


template <typename T, typename K, NormalizationType normalizationType = NormalizationType::NONE,
          ImageLayout outputLayout = ImageLayout::HWC>
void areaAveragingScaling(const ImageView<T>& src, ImageView<K>& dst)
{
    // Handle edge cases
    if (src.width == 0 || src.height == 0 || dst.width == 0 || dst.height == 0)
    {
        common::logError("Invalid image dimensions for scaling: src(%lux%lu), dst(%lux%lu)", src.width, src.height,
                         dst.width, dst.height);
        return;
    }
    const double xScale = static_cast<double>(src.width) / dst.width;
    const double yScale = static_cast<double>(src.height) / dst.height;

    ImageStats<K> stats;
    const bool needsStats = (normalizationType != NormalizationType::NONE);

    for (unsigned long y = 0; y < dst.height; y++)
    {
        for (unsigned long x = 0; x < dst.width; x++)
        {
            // Calculate source region
            const double srcX1 = x * xScale;
            const double srcY1 = y * yScale;
            const double srcX2 = (x + 1) * xScale;
            const double srcY2 = (y + 1) * yScale;

            // Get integer bounds with proper boundary handling
            const int minX = static_cast<int>(std::floor(srcX1));
            const int minY = static_cast<int>(std::floor(srcY1));
            const int maxX = static_cast<int>(std::ceil(srcX2));
            const int maxY = static_cast<int>(std::ceil(srcY2));

            // Clamp to valid source image bounds
            const int clampedMinX = std::max(minX, 0);
            const int clampedMinY = std::max(minY, 0);
            const int clampedMaxX = std::min(maxX, static_cast<int>(src.width));
            const int clampedMaxY = std::min(maxY, static_cast<int>(src.height));

            // Average pixels in the source region
            for (unsigned char ch = 0; ch < dst.pixelBytes; ch++)
            {
                // Determine source channel
                unsigned char srcCh = ch;
                if (srcCh >= src.pixelBytes)
                {
                    if (ch == 3 && dst.pixelBytes == 4 && src.pixelBytes < 4)
                    {
                        // Alpha channel for RGBA output when source has no alpha
                        const double alphaValue = 255.0;
                        K scaledValue;
                        if constexpr (std::is_floating_point_v<K>)
                        {
                            scaledValue = static_cast<K>(alphaValue);
                        }
                        else
                        {
                            const auto minVal = static_cast<double>(NormalizationTraits<K>::minValue());
                            const auto maxVal = static_cast<double>(NormalizationTraits<K>::maxValue());
                            scaledValue = static_cast<K>(std::clamp(alphaValue + 0.5, minVal, maxVal));
                        }
                        const size_t dstIdx =
                            calculateDestIndex<outputLayout>(y, x, ch, dst.width, dst.height, dst.pixelBytes);
                        dst.data[dstIdx] = scaledValue;
                        if (needsStats)
                        {
                            stats.update(scaledValue);
                        }
                        continue;
                    }
                    else
                    {
                        // Duplicate last available channel (e.g., grayscale to RGB)
                        srcCh = src.pixelBytes - 1;
                    }
                }

                double sum = 0.0;
                double totalWeight = 0.0;

                for (int sy = clampedMinY; sy < clampedMaxY; sy++)
                {
                    for (int sx = clampedMinX; sx < clampedMaxX; sx++)
                    {
                        // Calculate the overlap area between source pixel and destination region
                        const auto pixelX1 = static_cast<double>(sx);
                        const auto pixelY1 = static_cast<double>(sy);
                        const auto pixelX2 = static_cast<double>(sx + 1);
                        const auto pixelY2 = static_cast<double>(sy + 1);

                        // Calculate intersection area
                        const double overlapX1 = std::max(pixelX1, srcX1);
                        const double overlapY1 = std::max(pixelY1, srcY1);
                        const double overlapX2 = std::min(pixelX2, srcX2);
                        const double overlapY2 = std::min(pixelY2, srcY2);

                        // Only process if there's actual overlap
                        if (overlapX2 > overlapX1 && overlapY2 > overlapY1)
                        {
                            const double weight = (overlapX2 - overlapX1) * (overlapY2 - overlapY1);

                            const unsigned long srcIdx = (sy * src.width + sx) * src.pixelBytes + srcCh;
                            sum += static_cast<double>(src.data[srcIdx]) * weight;
                            totalWeight += weight;
                        }
                    }
                }

                // Calculate final pixel value
                const double result = (totalWeight > 0.0) ? (sum / totalWeight) : 0.0;
                K scaledValue;
                if constexpr (std::is_floating_point_v<K>)
                {
                    scaledValue = static_cast<K>(result);
                }
                else
                {
                    // Use traits for proper clamping range
                    const auto minVal = static_cast<double>(NormalizationTraits<K>::minValue());
                    const auto maxVal = static_cast<double>(NormalizationTraits<K>::maxValue());
                    scaledValue = static_cast<K>(std::clamp(result + 0.5, minVal, maxVal));
                }
                // Calculate destination index based on output layout
                const size_t dstIdx = calculateDestIndex<outputLayout>(y, x, ch, dst.width, dst.height, dst.pixelBytes);
                dst.data[dstIdx] = scaledValue;

                // Collect statistics if needed
                if (needsStats)
                {
                    stats.update(scaledValue);
                }
            }
        }
    }
    // Step 2: Apply normalization if needed
    if constexpr (normalizationType != NormalizationType::NONE)
    {
        stats.finalize();
        Normalizer<K, normalizationType> normalizer;

        const size_t totalPixels = dst.width * dst.height * dst.pixelBytes;
        for (size_t i = 0; i < totalPixels; i++)
        {
            dst.data[i] = normalizer(dst.data[i], stats);
        }
    }
}

// Lanczos kernel implementation
class LanczosKernel
{
  public:
    static constexpr int RADIUS = 3; // Lanczos-3 for good quality/speed balance

    static double lanczos(double x)
    {
        if (x == 0.0)
        {
            return 1.0;
        }
        if (std::abs(x) >= RADIUS)
        {
            return 0.0;
        }

        const double piX = M_PI * x;
        const double piXOverRadius = piX / RADIUS;

        return (std::sin(piX) / piX) * (std::sin(piXOverRadius) / piXOverRadius);
    }
};


// Optimized separable Lanczos Scaling
template <typename T, typename K, NormalizationType normalizationType = NormalizationType::NONE,
          ImageLayout outputLayout = ImageLayout::HWC>
void lanczosScaling(const ImageView<T>& src, ImageView<K>& dst)
{
    // Handle edge cases
    if (src.width == 0 || src.height == 0 || dst.width == 0 || dst.height == 0)
    {
        common::logError("Invalid image dimensions for scaling: src(%lux%lu), dst(%lux%lu)", src.width, src.height,
                         dst.width, dst.height);
        return;
    }

    const double xScale = static_cast<double>(src.width) / dst.width;
    const double yScale = static_cast<double>(src.height) / dst.height;

    // For Scaling, we need to adjust the kernel support
    const double xSupport = std::max(1.0, xScale) * LanczosKernel::RADIUS;
    const double ySupport = std::max(1.0, yScale) * LanczosKernel::RADIUS;

    // Pre-calculate horizontal weights for each destination column
    struct HorizontalContrib
    {
        int startIdx{};
        int endIdx{};
        std::vector<double> weights;
    };

    std::vector<HorizontalContrib> horizContribs(dst.width);

    for (unsigned long x = 0; x < dst.width; x++)
    {
        const double center = (x + 0.5) * xScale - 0.5;
        const int startIdx = std::max(0, static_cast<int>(std::floor(center - xSupport)));
        const int endIdx = std::min(static_cast<int>(src.width - 1), static_cast<int>(std::ceil(center + xSupport)));

        horizContribs[x].startIdx = startIdx;
        horizContribs[x].endIdx = endIdx;
        horizContribs[x].weights.resize(endIdx - startIdx + 1);

        double weightSum = 0.0;
        for (int i = startIdx; i <= endIdx; i++)
        {
            const double weight = LanczosKernel::lanczos((i - center) / std::max(1.0, xScale));
            horizContribs[x].weights[i - startIdx] = weight;
            weightSum += weight;
        }

        // Normalize weights to prevent darkening/brightening
        if (weightSum > 0.0)
        {
            for (auto& weight : horizContribs[x].weights)
            {
                weight /= weightSum;
            }
        }
    }

    // Pre-calculate vertical weights for each destination row
    struct VerticalContrib
    {
        int startIdx{};
        int endIdx{};
        std::vector<double> weights;
    };

    std::vector<VerticalContrib> vertContribs(dst.height);

    for (unsigned long y = 0; y < dst.height; y++)
    {
        const double center = (y + 0.5) * yScale - 0.5;
        const int startIdx = std::max(0, static_cast<int>(std::floor(center - ySupport)));
        const int endIdx = std::min(static_cast<int>(src.height - 1), static_cast<int>(std::ceil(center + ySupport)));

        vertContribs[y].startIdx = startIdx;
        vertContribs[y].endIdx = endIdx;
        vertContribs[y].weights.resize(endIdx - startIdx + 1);

        double weightSum = 0.0;
        for (int i = startIdx; i <= endIdx; i++)
        {
            const double weight = LanczosKernel::lanczos((i - center) / std::max(1.0, yScale));
            vertContribs[y].weights[i - startIdx] = weight;
            weightSum += weight;
        }

        // Normalize weights
        if (weightSum > 0.0)
        {
            for (auto& weight : vertContribs[y].weights)
            {
                weight /= weightSum;
            }
        }
    }

    // Temporary buffer for horizontal pass (using double for precision)
    std::vector<double> tempBuffer(dst.width * src.height * dst.pixelBytes);

    // Horizontal pass - process each row
    for (unsigned long y = 0; y < src.height; y++)
    {
        for (unsigned long x = 0; x < dst.width; x++)
        {
            const auto& contrib = horizContribs[x];

            for (unsigned char ch = 0; ch < dst.pixelBytes; ch++)
            {
                // Determine source channel
                unsigned char srcCh = ch;
                if (srcCh >= src.pixelBytes)
                {
                    if (ch == 3 && dst.pixelBytes == 4 && src.pixelBytes < 4)
                    {
                        // Alpha channel for RGBA output when source has no alpha
                        const double alphaValue = 255.0;
                        const unsigned long tempIdx = (y * dst.width + x) * dst.pixelBytes + ch;
                        tempBuffer[tempIdx] = alphaValue;
                        continue;
                    }
                    else
                    {
                        // Duplicate last available channel (e.g., grayscale to RGB)
                        srcCh = src.pixelBytes - 1;
                    }
                }

                double sum = 0.0;

                for (int i = contrib.startIdx; i <= contrib.endIdx; i++)
                {
                    const unsigned long srcIdx = (y * src.width + i) * src.pixelBytes + srcCh;
                    sum += static_cast<double>(src.data[srcIdx]) * contrib.weights[i - contrib.startIdx];
                }

                const unsigned long tempIdx = (y * dst.width + x) * dst.pixelBytes + ch;
                tempBuffer[tempIdx] = sum;
            }
        }
    }

    // Vertical pass - process each column
    for (unsigned long y = 0; y < dst.height; y++)
    {
        const auto& contrib = vertContribs[y];

        for (unsigned long x = 0; x < dst.width; x++)
        {
            for (unsigned char ch = 0; ch < dst.pixelBytes; ch++)
            {
                double sum = 0.0;

                for (int i = contrib.startIdx; i <= contrib.endIdx; i++)
                {
                    const unsigned long tempIdx = (i * dst.width + x) * dst.pixelBytes + ch;
                    sum += tempBuffer[tempIdx] * contrib.weights[i - contrib.startIdx];
                }

                // Convert to output type with proper clamping
                K scaledValue;
                if constexpr (std::is_floating_point_v<K>)
                {
                    scaledValue = static_cast<K>(sum);
                }
                else
                {
                    const auto minVal = static_cast<double>(std::numeric_limits<K>::min());
                    const auto maxVal = static_cast<double>(std::numeric_limits<K>::max());
                    scaledValue = static_cast<K>(std::clamp(sum + 0.5, minVal, maxVal));
                }

                // Calculate destination index based on output layout
                const size_t dstIdx = calculateDestIndex<outputLayout>(y, x, ch, dst.width, dst.height, dst.pixelBytes);
                dst.data[dstIdx] = scaledValue;
            }
        }
    }

    // Apply normalization if needed
    if constexpr (normalizationType == NormalizationType::MINMAX)
    {
        const size_t totalPixels = dst.width * dst.height * dst.pixelBytes;
        K minVal = dst.data[0];
        K maxVal = dst.data[0];

        for (size_t i = 1; i < totalPixels; i++)
        {
            minVal = std::min(minVal, dst.data[i]);
            maxVal = std::max(maxVal, dst.data[i]);
        }

        if (maxVal > minVal)
        {
            const double scale = static_cast<double>(std::numeric_limits<K>::max()) / (maxVal - minVal);
            for (size_t i = 0; i < totalPixels; i++)
            {
                dst.data[i] = static_cast<K>((dst.data[i] - minVal) * scale);
            }
        }
    }
    else if constexpr (normalizationType == NormalizationType::ZERO_CENTER)
    {
        common::logWarn("NON TESTED: Applying zero-centering normalization");
        const size_t totalPixels = dst.width * dst.height * dst.pixelBytes;
        ImageStats<K> stats;

        // Calculate mean
        for (size_t i = 0; i < totalPixels; i++)
        {
            stats.update(dst.data[i]);
        }
        stats.finalize();

        // Apply zero-centering
        Normalizer<K, NormalizationType::ZERO_CENTER> normalizer;
        for (size_t i = 0; i < totalPixels; i++)
        {
            dst.data[i] = normalizer(dst.data[i], stats);
        }
    }
}

// Fast box filter for extreme Scaling (when speed is critical)
template <typename T, typename K, ImageLayout outputLayout = ImageLayout::HWC>
void fastBoxScaling(const ImageView<T>& src, ImageView<K>& dst)
{
    const double xScale = static_cast<double>(src.width) / dst.width;
    const double yScale = static_cast<double>(src.height) / dst.height;

    // Use integer arithmetic when possible for speed
    const bool useIntegerMath = (xScale == std::floor(xScale) && yScale == std::floor(yScale));

    if (useIntegerMath)
    {
        const int xStep = static_cast<int>(xScale);
        const int yStep = static_cast<int>(yScale);
        const int totalSamples = xStep * yStep;

        for (unsigned long y = 0; y < dst.height; y++)
        {
            for (unsigned long x = 0; x < dst.width; x++)
            {
                const unsigned long srcStartX = x * xStep;
                const unsigned long srcStartY = y * yStep;

                for (unsigned char ch = 0; ch < dst.pixelBytes; ch++)
                {
                    // Determine source channel
                    unsigned char srcCh = ch;
                    if (srcCh >= src.pixelBytes)
                    {
                        if (ch == 3 && dst.pixelBytes == 4 && src.pixelBytes < 4)
                        {
                            // Alpha channel for RGBA output when source has no alpha
                            const K alphaValue = 255;
                            const size_t dstIdx =
                                calculateDestIndex<outputLayout>(y, x, ch, dst.width, dst.height, dst.pixelBytes);
                            dst.data[dstIdx] = alphaValue;
                            continue;
                        }
                        else
                        {
                            // Duplicate last available channel (e.g., grayscale to RGB)
                            srcCh = src.pixelBytes - 1;
                        }
                    }

                    int sum = 0;

                    for (int dy = 0; dy < yStep; dy++)
                    {
                        for (int dx = 0; dx < xStep; dx++)
                        {
                            const unsigned long srcIdx =
                                ((srcStartY + dy) * src.width + (srcStartX + dx)) * src.pixelBytes + srcCh;
                            sum += static_cast<int>(src.data[srcIdx]);
                        }
                    }
                    // Calculate destination index based on output layout
                    const size_t dstIdx =
                        calculateDestIndex<outputLayout>(y, x, ch, dst.width, dst.height, dst.pixelBytes);
                    dst.data[dstIdx] = static_cast<K>(sum / totalSamples);
                }
            }
        }
    }
    else
    {
        // Fall back to floating-point version for non-integer scales
        for (unsigned long y = 0; y < dst.height; y++)
        {
            for (unsigned long x = 0; x < dst.width; x++)
            {
                const double srcX1 = x * xScale;
                const double srcY1 = y * yScale;
                const double srcX2 = (x + 1) * xScale;
                const double srcY2 = (y + 1) * yScale;

                const int minX = static_cast<int>(srcX1);
                const int minY = static_cast<int>(srcY1);
                const int maxX = std::min(static_cast<int>(std::ceil(srcX2)), static_cast<int>(src.width));
                const int maxY = std::min(static_cast<int>(std::ceil(srcY2)), static_cast<int>(src.height));

                for (unsigned char ch = 0; ch < dst.pixelBytes; ch++)
                {
                    // Determine source channel
                    unsigned char srcCh = ch;
                    if (srcCh >= src.pixelBytes)
                    {
                        if (ch == 3 && dst.pixelBytes == 4 && src.pixelBytes < 4)
                        {
                            // Alpha channel for RGBA output when source has no alpha
                            const K alphaValue = 255;
                            const size_t dstIdx =
                                calculateDestIndex<outputLayout>(y, x, ch, dst.width, dst.height, dst.pixelBytes);
                            dst.data[dstIdx] = alphaValue;
                            continue;
                        }
                        else
                        {
                            // Duplicate last available channel (e.g., grayscale to RGB)
                            srcCh = src.pixelBytes - 1;
                        }
                    }

                    double sum = 0.0;
                    int count = 0;

                    for (int sy = minY; sy < maxY; sy++)
                    {
                        for (int sx = minX; sx < maxX; sx++)
                        {
                            const unsigned long srcIdx = (sy * src.width + sx) * src.pixelBytes + srcCh;
                            sum += static_cast<double>(src.data[srcIdx]);
                            count++;
                        }
                    }

                    // Calculate destination index based on output layout
                    const size_t dstIdx =
                        calculateDestIndex<outputLayout>(y, x, ch, dst.width, dst.height, dst.pixelBytes);
                    dst.data[dstIdx] = static_cast<K>(count > 0 ? sum / count : 0);
                }
            }
        }
    }
}

// Bicubic kernel helper
inline float cubicHermite(float a, float b, float c, float d, float t)
{
    const float a3 = -a / 2.0f + (3.0f * b) / 2.0f - (3.0f * c) / 2.0f + d / 2.0f;
    const float b2 = a - (5.0f * b) / 2.0f + 2.0f * c - d / 2.0f;
    const float c1 = -a / 2.0f + c / 2.0f;
    const float d0 = b;
    return a3 * t * t * t + b2 * t * t + c1 * t + d0;
}

// Bicubic scaling for RGB/Grayscale images
template <typename T, typename K, NormalizationType normalizationType = NormalizationType::NONE,
          ImageLayout outputLayout = ImageLayout::HWC>
void bicubicScaling(const ImageView<T>& src, ImageView<K>& dst)
{
    // Handle edge cases
    if (src.width == 0 || src.height == 0 || dst.width == 0 || dst.height == 0)
    {
        common::logError("Invalid image dimensions for scaling: src(%lux%lu), dst(%lux%lu)", src.width, src.height,
                         dst.width, dst.height);
        return;
    }

    const double xRatio = static_cast<double>(src.width) / dst.width;
    const double yRatio = static_cast<double>(src.height) / dst.height;

    ImageStats<K> stats;
    const bool needsStats = (normalizationType != NormalizationType::NONE);

    for (unsigned long y = 0; y < dst.height; ++y)
    {
        for (unsigned long x = 0; x < dst.width; ++x)
        {
            const double srcX = (x + 0.5) * xRatio - 0.5;
            const double srcY = (y + 0.5) * yRatio - 0.5;

            const int xInt = static_cast<int>(std::floor(srcX));
            const int yInt = static_cast<int>(std::floor(srcY));
            const double fracX = srcX - xInt;
            const double fracY = srcY - yInt;

            for (unsigned char ch = 0; ch < dst.pixelBytes; ++ch)
            {
                double col[4];
                for (int m = -1; m <= 2; ++m)
                {
                    double row[4];
                    for (int n = -1; n <= 2; ++n)
                    {
                        const int px = std::clamp(xInt + n, 0, static_cast<int>(src.width) - 1);
                        const int py = std::clamp(yInt + m, 0, static_cast<int>(src.height) - 1);

                        // Determine source channel
                        unsigned char srcCh = ch;
                        if (srcCh >= src.pixelBytes)
                        {
                            if (ch == 3 && dst.pixelBytes == 4 && src.pixelBytes < 4)
                            {
                                // Alpha channel for RGBA output when source has no alpha
                                row[n + 1] = 255.0;
                                continue;
                            }
                            else
                            {
                                // Duplicate last available channel (e.g., grayscale to RGB)
                                srcCh = src.pixelBytes - 1;
                            }
                        }
                        row[n + 1] = static_cast<double>(src.data[(py * src.width + px) * src.pixelBytes + srcCh]);
                    }
                    col[m + 1] = cubicHermite(row[0], row[1], row[2], row[3], fracX);
                }
                const double value = cubicHermite(col[0], col[1], col[2], col[3], fracY);

                K scaledValue;
                if constexpr (std::is_floating_point_v<K>)
                {
                    scaledValue = static_cast<K>(value);
                }
                else
                {
                    const auto minVal = static_cast<double>(NormalizationTraits<K>::minValue());
                    const auto maxVal = static_cast<double>(NormalizationTraits<K>::maxValue());
                    scaledValue = static_cast<K>(std::clamp(value + 0.5, minVal, maxVal));
                }
                // Calculate destination index based on output layout
                const size_t dstIdx = calculateDestIndex<outputLayout>(y, x, ch, dst.width, dst.height, dst.pixelBytes);
                dst.data[dstIdx] = scaledValue;

                if (needsStats)
                {
                    stats.update(scaledValue);
                }
            }
        }
    }

    // Apply normalization if needed
    if constexpr (normalizationType != NormalizationType::NONE)
    {
        stats.finalize();
        const Normalizer<K, normalizationType> normalizer;
        const size_t totalPixels = dst.width * dst.height * dst.pixelBytes;
        for (size_t i = 0; i < totalPixels; i++)
        {
            dst.data[i] = normalizer(dst.data[i], stats);
        }
    }
}

// Transform a vector of math_utils::Point using a 2x3 affine matrix (row-major)
inline std::vector<math_utils::Point<>>
transformPointsAffine(const std::vector<math_utils::Point<>>& points, const double m[6])
{
    std::vector<math_utils::Point<>> result;
    result.reserve(points.size());
    for (const auto& pt : points)
    {
        const auto x = static_cast<double>(pt.x);
        const auto y = static_cast<double>(pt.y);
        const double xNew = m[0] * x + m[1] * y + m[2];
        const double yNew = m[3] * x + m[4] * y + m[5];
        result.emplace_back(static_cast<long>(std::round(xNew)), static_cast<long>(std::round(yNew)));
    }
    return result;
}

// Transform a vector of (x, y) pairs using a 2x3 affine matrix (row-major)
inline std::vector<std::pair<double, double>>
transformPointsAffine(const std::vector<std::pair<double, double>>& points, const double m[6])
{
    std::vector<std::pair<double, double>> result;
    result.reserve(points.size());
    for (const auto& pt : points)
    {
        const double x = pt.first;
        const double y = pt.second;
        const double xNew = m[0] * x + m[1] * y + m[2];
        const double yNew = m[3] * x + m[4] * y + m[5];
        result.emplace_back(xNew, yNew);
    }
    return result;
}

inline void paintCircle(std::unique_ptr<Image>& image, const math_utils::Point3D& center, float radius, Pixel color)
{
    const int cx = static_cast<int>(std::round(center.x));
    const int cy = static_cast<int>(std::round(center.y));
    const float r_squared = radius * radius;
    const int r_ceil = static_cast<int>(std::ceil(radius));
    const int w = static_cast<int>(image->info.width);
    const int h = static_cast<int>(image->info.height);

    // Always paint the center pixel if in bounds
    if (cx >= 0 && cx < w && cy >= 0 && cy < h)
    {
        image->ppx(cx, cy, color);
    }

    for (int y = -r_ceil; y <= r_ceil; ++y)
    {
        for (int x = -r_ceil; x <= r_ceil; ++x)
        {
            const float dist_squared = static_cast<float>(x * x + y * y);
            if (dist_squared <= r_squared)
            {
                const int px = cx + x;
                const int py = cy + y;
                if (px >= 0 && px < w && py >= 0 && py < h)
                {
                    image->ppx(px, py, color);
                }
            }
        }
    }
}


// Helper: Convert HWC float* (height, width, channels) to CHW float* (channels, height, width)
// src: input float array in HWC order, shape [height, width, channels]
// dst: output float array in CHW order, shape [channels, height, width]
// width, height: image dimensions
// channels: number of channels (usually 3 for RGB)
template <typename T>
void hwcToChw(const T* src, T* dst, unsigned long width, unsigned long height, unsigned long channels)
{
    for (unsigned long h = 0; h < height; ++h)
    {
        for (unsigned long w = 0; w < width; ++w)
        {
            for (unsigned long c = 0; c < channels; ++c)
            {
                // HWC index: (h * width + w) * channels + c
                // CHW index: c * (height * width) + h * width + w
                dst[c * (height * width) + h * width + w] = src[(h * width + w) * channels + c];
            }
        }
    }
}
template <typename T>
void chwToHwc(const T* src, T* dst, unsigned long width, unsigned long height, unsigned long channels)
{
    for (unsigned long h = 0; h < height; ++h)
    {
        for (unsigned long w = 0; w < width; ++w)
        {
            for (unsigned long c = 0; c < channels; ++c)
            {
                // CHW index: c * (height * width) + h * width + w
                // HWC index: (h * width + w) * channels + c
                dst[(h * width + w) * channels + c] = src[c * (height * width) + h * width + w];
            }
        }
    }
}

template <NormalizationType normalizationType = NormalizationType::NONE>
std::unique_ptr<Image> convertToRawImage(float* src, unsigned long width, unsigned long height)
{
    if (src == nullptr || width == 0 || height == 0)
    {
        common::logError("Invalid parameters for converting to raw image");
        return nullptr;
    }

    // Convert CHW (channels, height, width) to HWC (height, width, channels)
    const size_t dataSize = width * height * 3; // Assuming RGB format
    std::vector<float> hwcData(dataSize);
    chwToHwc(src, hwcData.data(), width, height, 3);

    auto image = std::make_unique<Image>(dataSize);
    image->info.width = width;
    image->info.height = height;
    image->info.pixelSizeBytes = 3;
    image->info.format = ImageFormat::RGB;
    image->info.filename = "raw_image_from_float";
    image->info.x = 0;
    image->info.y = 0;

    if constexpr (normalizationType == NormalizationType::MINMAX)
    {
        // Assume input is in [0,1], scale to [0,255] for visualization
        for (size_t i = 0; i < dataSize; ++i)
        {
            image->data()[i] = static_cast<unsigned char>(std::clamp(hwcData[i] * 255.0f, 0.0f, 255.0f));
        }
    }
    else if constexpr (normalizationType == NormalizationType::ZERO_CENTER)
    {
        // Assume input is zero-centered in [-1,1], scale to [0,255] for visualization
        for (size_t i = 0; i < dataSize; ++i)
        {
            const float val = (hwcData[i] + 1.0f) * 0.5f * 255.0f; // Map [-1,1] to [0,255]
            image->data()[i] = static_cast<unsigned char>(std::clamp(val, 0.0f, 255.0f));
        }
    }
    else
    {
        // Fill the image data directly
        for (size_t i = 0; i < dataSize; ++i)
        {
            image->data()[i] = static_cast<unsigned char>(hwcData[i]);
        }
    }

    return image;
}
} // namespace linuxface::image_utils

#endif // IMAGE_UTILS_H
