#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

#include <memory>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/math_utils.h"

namespace linuxface
{
namespace image_utils
{

const double template_112[5][2] = {
    {0.34191607, 0.46157411},
    {0.65653393, 0.45983393},
    {0.50022500, 0.64050536},
    {0.37097589, 0.82469196},
    {0.63151696, 0.82325089}
};

const double template_128[5][2] = {
    {0.36167656, 0.40387734},
    {0.63696719, 0.40235469},
    {0.50019687, 0.56044219},
    {0.38710391, 0.72160547},
    {0.61507734, 0.72034453}
};

const double template_192[5][2] = {
    {0.40625,  0.390625},
    {0.59375,  0.390625},
    {0.5,      0.46875 },
    {0.442708, 0.598958},
    {0.557292, 0.598958}
};

const double template_512[5][2] = {
    {0.37691676, 0.46864664},
    {0.62285697, 0.46912813},
    {0.50123859, 0.61331904},
    {0.39308822, 0.72541100},
    {0.61150205, 0.72490465}
};


// Align or unalign face using 5 landmarks and a template (returns nullptr if not possible)
// If align_to_template is true: aligns landmarks to template (like align_face_affine)
// If false: unaligns template to landmarks (like unalign_face_affine)
inline std::unique_ptr<Image>
affine_face_transform(const Image& input_img, const std::vector<math_utils::Point>& landmarks,
                      const double template_points[5][2], int target_size, bool align_to_template = true)
{
    if (landmarks.size() != 5)
    {
        return nullptr;
    }
    std::vector<math_utils::Point> template_pts;
    for (int i = 0; i < 5; ++i)
    {
        template_pts.emplace_back(static_cast<long>(template_points[i][0] * target_size),
                                  static_cast<long>(template_points[i][1] * target_size));
    }
    double src[10], dst[10];
    for (int i = 0; i < 5; ++i)
    {
        if (align_to_template)
        {
            src[2 * i] = static_cast<double>(landmarks[i].x);
            src[2 * i + 1] = static_cast<double>(landmarks[i].y);
            dst[2 * i] = static_cast<double>(template_pts[i].x);
            dst[2 * i + 1] = static_cast<double>(template_pts[i].y);
        }
        else
        {
            src[2 * i] = static_cast<double>(template_pts[i].x);
            src[2 * i + 1] = static_cast<double>(template_pts[i].y);
            dst[2 * i] = static_cast<double>(landmarks[i].x);
            dst[2 * i + 1] = static_cast<double>(landmarks[i].y);
        }
    }
    double M[6] = {0};
    math_utils::estimate_affine_2d(src, dst, 5, M);
    return input_img.affineWarpBilinear(M, target_size, target_size);
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
// Create a mask for the crop size using the Image class (no OpenCV)

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
        common::log_warn("Blur amount is zero, returning unblurred box mask.");
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

inline void fast_box_blur(unsigned char* src, unsigned char* dst, int width, int height, int radius)
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
            int left = x - radius;
            int right = x + radius + 1;
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
            int top = y - radius;
            int bottom = y + radius + 1;
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

inline std::unique_ptr<Image> create_static_box_mask(const std::vector<double>& crop_size)
{
    int width = static_cast<int>(crop_size[0]);
    int height = static_cast<int>(crop_size[1]);
    double face_mask_blur = 0.6;
    std::vector<int> face_mask_padding = {0, 0, 0, 0};
    int blur_amount = static_cast<int>(width * 0.5 * face_mask_blur);
    int blur_area = std::max(blur_amount / 2, 1);

    std::unique_ptr<Image> box_mask = std::make_unique<Image>(width * height);
    box_mask->info.width = width;
    box_mask->info.height = height;
    box_mask->info.pixelSizeBytes = 1;
    box_mask->info.format = linuxface::ImageFormat::GRAYSCALE;
    std::fill(box_mask->data(), box_mask->data() + width * height, 255);

    int top = std::max(blur_area, static_cast<int>(height * face_mask_padding[0] / 100.0));
    int bottom = std::max(blur_area, static_cast<int>(height * face_mask_padding[2] / 100.0));
    int right = std::max(blur_area, static_cast<int>(width * face_mask_padding[1] / 100.0));
    int left = std::max(blur_area, static_cast<int>(width * face_mask_padding[3] / 100.0));

    // Zero out padding regions
    for (int y = 0; y < top; ++y)
    {
        std::fill(box_mask->data() + y * width, box_mask->data() + (y + 1) * width, 0);
    }
    for (int y = height - bottom; y < height; ++y)
    {
        std::fill(box_mask->data() + y * width, box_mask->data() + (y + 1) * width, 0);
    }
    for (int y = 0; y < height; ++y)
    {
        std::fill(box_mask->data() + y * width, box_mask->data() + y * width + left, 0);
        std::fill(box_mask->data() + y * width + (width - right), box_mask->data() + (y + 1) * width, 0);
    }

    if (blur_amount > 0)
    {
        std::unique_ptr<Image> blurred = box_mask->deepCopy();
        fast_box_blur(box_mask->data(), blurred->data(), width, height, blur_amount / 2);
        blurred->info.pixelSizeBytes = 1;
        blurred->info.format = linuxface::ImageFormat::GRAYSCALE;
        return blurred;
    }

    return box_mask;
}

// Normalization traits for different types
template <typename T>
struct NormalizationTraits
{
    static constexpr T min_value() { return std::numeric_limits<T>::min(); }
    static constexpr T max_value() { return std::numeric_limits<T>::max(); }
    static constexpr T zero_value() { return T(0); }
};

// Specialization for floating point types
template <>
struct NormalizationTraits<float>
{
    static constexpr float min_value() { return 0.0f; }
    static constexpr float max_value() { return 1.0f; }
    static constexpr float zero_value() { return 0.0f; }
};

template <>
struct NormalizationTraits<double>
{
    static constexpr double min_value() { return 0.0; }
    static constexpr double max_value() { return 1.0; }
    static constexpr double zero_value() { return 0.0; }
};

// Statistics structure for normalization
template <typename T>
struct ImageStats
{
    T min_val;
    T max_val;
    double mean;
    size_t count;

    ImageStats()
        : min_val(std::numeric_limits<T>::max()), max_val(std::numeric_limits<T>::lowest()), mean(0.0), count(0)
    {
    }

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
    T operator()(T value, const ImageStats<T>& stats) const
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
            return NormalizationTraits<T>::zero_value();
        }

        double normalized =
            static_cast<double>(value - stats.min_val) / static_cast<double>(stats.max_val - stats.min_val);

        if constexpr (std::is_floating_point_v<T>)
        {
            return static_cast<T>(normalized);
        }
        else
        {
            return static_cast<T>(normalized
                                      * (NormalizationTraits<T>::max_value() - NormalizationTraits<T>::min_value())
                                  + NormalizationTraits<T>::min_value());
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
            double range = static_cast<double>(NormalizationTraits<T>::max_value())
                           - static_cast<double>(NormalizationTraits<T>::min_value());
            centered = std::clamp(centered, -range / 2.0, range / 2.0);
            return static_cast<T>(centered + (range / 2.0 + NormalizationTraits<T>::min_value()));
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


template <typename T, NormalizationType normalizationType = NormalizationType::NONE>
void bilinearScaling(const ImageView<T>& src, ImageView<T>& dst)
{
    // Handle edge cases
    if (src.width == 0 || src.height == 0 || dst.width == 0 || dst.height == 0)
    {
        common::log_error("Invalid image dimensions for scaling: src(%lux%lu), dst(%lux%lu)", src.width, src.height,
                          dst.width, dst.height);
        return;
    }

    // Pre-calculate scaling ratios for better performance
    const double xRatio = static_cast<double>(src.width) / dst.width;
    const double yRatio = static_cast<double>(src.height) / dst.height;

    ImageStats<T> stats;
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

                    const unsigned long dstIdx = (y * dst.width + x) * src.pixelBytes;

                    // Bilinear interpolation for each channel
                    for (unsigned char ch = 0; ch < src.pixelBytes; ch++)
                    {
                        // Handle boundary conditions with clamping
                        const int clampedX1 = std::clamp(x1, 0, static_cast<int>(src.width - 1));
                        const int clampedY1 = std::clamp(y1, 0, static_cast<int>(src.height - 1));
                        const int clampedX2 = std::clamp(x2, 0, static_cast<int>(src.width - 1));
                        const int clampedY2 = std::clamp(y2, 0, static_cast<int>(src.height - 1));

                        // Get pixel values with proper boundary handling
                        const unsigned long idx1 = (clampedY1 * src.width + clampedX1) * src.pixelBytes + ch;
                        const unsigned long idx2 = (clampedY1 * src.width + clampedX2) * src.pixelBytes + ch;
                        const unsigned long idx3 = (clampedY2 * src.width + clampedX1) * src.pixelBytes + ch;
                        const unsigned long idx4 = (clampedY2 * src.width + clampedX2) * src.pixelBytes + ch;

                        const double p1 = src.data[idx1];
                        const double p2 = src.data[idx2];
                        const double p3 = src.data[idx3];
                        const double p4 = src.data[idx4];

                        // Optimized bilinear interpolation
                        const double top = p1 + fracX * (p2 - p1);
                        const double bottom = p3 + fracX * (p4 - p3);
                        const double result = top + fracY * (bottom - top);

                        T scaledValue;
                        if constexpr (std::is_floating_point_v<T>)
                        {
                            scaledValue = static_cast<T>(result);
                        }
                        else
                        {
                            // Use traits for proper clamping range
                            const double minVal = static_cast<double>(NormalizationTraits<T>::min_value());
                            const double maxVal = static_cast<double>(NormalizationTraits<T>::max_value());
                            scaledValue = static_cast<T>(std::clamp(result + 0.5, minVal, maxVal));
                        }
                        dst.data[dstIdx + ch] = scaledValue;

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
        Normalizer<T, normalizationType> normalizer;

        const size_t totalPixels = dst.width * dst.height * src.pixelBytes;
        for (size_t i = 0; i < totalPixels; i++)
        {
            dst.data[i] = normalizer(dst.data[i], stats);
        }
    }
}


template <typename T, NormalizationType normalizationType = NormalizationType::NONE>
void areaAveragingScaling(const ImageView<T>& src, ImageView<T>& dst)
{
    // Handle edge cases
    if (src.width == 0 || src.height == 0 || dst.width == 0 || dst.height == 0)
    {
        common::log_error("Invalid image dimensions for scaling: src(%lux%lu), dst(%lux%lu)", src.width, src.height,
                          dst.width, dst.height);
        return;
    }
    const double xScale = static_cast<double>(src.width) / dst.width;
    const double yScale = static_cast<double>(src.height) / dst.height;

    ImageStats<T> stats;
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

            const unsigned long dstIdx = (y * dst.width + x) * src.pixelBytes;

            // Average pixels in the source region
            for (unsigned char ch = 0; ch < src.pixelBytes; ch++)
            {
                double sum = 0.0;
                double totalWeight = 0.0;

                for (int sy = clampedMinY; sy < clampedMaxY; sy++)
                {
                    for (int sx = clampedMinX; sx < clampedMaxX; sx++)
                    {
                        // Calculate the overlap area between source pixel and destination region
                        const double pixelX1 = static_cast<double>(sx);
                        const double pixelY1 = static_cast<double>(sy);
                        const double pixelX2 = static_cast<double>(sx + 1);
                        const double pixelY2 = static_cast<double>(sy + 1);

                        // Calculate intersection area
                        const double overlapX1 = std::max(pixelX1, srcX1);
                        const double overlapY1 = std::max(pixelY1, srcY1);
                        const double overlapX2 = std::min(pixelX2, srcX2);
                        const double overlapY2 = std::min(pixelY2, srcY2);

                        // Only process if there's actual overlap
                        if (overlapX2 > overlapX1 && overlapY2 > overlapY1)
                        {
                            const double weight = (overlapX2 - overlapX1) * (overlapY2 - overlapY1);

                            const unsigned long srcIdx = (sy * src.width + sx) * src.pixelBytes + ch;
                            sum += static_cast<double>(src.data[srcIdx]) * weight;
                            totalWeight += weight;
                        }
                    }
                }

                // Calculate final pixel value
                double result = (totalWeight > 0.0) ? (sum / totalWeight) : 0.0;
                T scaledValue;
                if constexpr (std::is_floating_point_v<T>)
                {
                    scaledValue = static_cast<T>(result);
                }
                else
                {
                    // Use traits for proper clamping range
                    const double minVal = static_cast<double>(NormalizationTraits<T>::min_value());
                    const double maxVal = static_cast<double>(NormalizationTraits<T>::max_value());
                    scaledValue = static_cast<T>(std::clamp(result + 0.5, minVal, maxVal));
                }
                dst.data[dstIdx + ch] = scaledValue;

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
        Normalizer<T, normalizationType> normalizer;

        const size_t totalPixels = dst.width * dst.height * src.pixelBytes;
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

    static inline double lanczos(double x)
    {
        if (x == 0.0)
        {
            return 1.0;
        }
        if (std::abs(x) >= RADIUS)
        {
            return 0.0;
        }

        const double pi_x = M_PI * x;
        const double pi_x_over_radius = pi_x / RADIUS;

        return (std::sin(pi_x) / pi_x) * (std::sin(pi_x_over_radius) / pi_x_over_radius);
    }
};

// Optimized separable Lanczos Scaling
template <typename T, NormalizationType normalizationType = NormalizationType::NONE>
void lanczosScaling(const ImageView<T>& src, ImageView<T>& dst)
{
    // Handle edge cases
    if (src.width == 0 || src.height == 0 || dst.width == 0 || dst.height == 0)
    {
        common::log_error("Invalid image dimensions for scaling: src(%lux%lu), dst(%lux%lu)", src.width, src.height,
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
        int startIdx;
        int endIdx;
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
        int startIdx;
        int endIdx;
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
    std::vector<double> tempBuffer(dst.width * src.height * src.pixelBytes);

    // Horizontal pass - process each row
    for (unsigned long y = 0; y < src.height; y++)
    {
        for (unsigned long x = 0; x < dst.width; x++)
        {
            const auto& contrib = horizContribs[x];

            for (unsigned char ch = 0; ch < src.pixelBytes; ch++)
            {
                double sum = 0.0;

                for (int i = contrib.startIdx; i <= contrib.endIdx; i++)
                {
                    const unsigned long srcIdx = (y * src.width + i) * src.pixelBytes + ch;
                    sum += static_cast<double>(src.data[srcIdx]) * contrib.weights[i - contrib.startIdx];
                }

                const unsigned long tempIdx = (y * dst.width + x) * src.pixelBytes + ch;
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
            for (unsigned char ch = 0; ch < src.pixelBytes; ch++)
            {
                double sum = 0.0;

                for (int i = contrib.startIdx; i <= contrib.endIdx; i++)
                {
                    const unsigned long tempIdx = (i * dst.width + x) * src.pixelBytes + ch;
                    sum += tempBuffer[tempIdx] * contrib.weights[i - contrib.startIdx];
                }

                // Convert to output type with proper clamping
                T scaledValue;
                if constexpr (std::is_floating_point_v<T>)
                {
                    scaledValue = static_cast<T>(sum);
                }
                else
                {
                    const double minVal = static_cast<double>(std::numeric_limits<T>::min());
                    const double maxVal = static_cast<double>(std::numeric_limits<T>::max());
                    scaledValue = static_cast<T>(std::clamp(sum + 0.5, minVal, maxVal));
                }

                const unsigned long dstIdx = (y * dst.width + x) * src.pixelBytes + ch;
                dst.data[dstIdx] = scaledValue;
            }
        }
    }

    // Apply normalization if needed
    if constexpr (normalizationType == NormalizationType::MINMAX)
    {
        const size_t totalPixels = dst.width * dst.height * dst.pixelBytes;
        T minVal = dst.data[0];
        T maxVal = dst.data[0];

        for (size_t i = 1; i < totalPixels; i++)
        {
            minVal = std::min(minVal, dst.data[i]);
            maxVal = std::max(maxVal, dst.data[i]);
        }

        if (maxVal > minVal)
        {
            const double scale = static_cast<double>(std::numeric_limits<T>::max()) / (maxVal - minVal);
            for (size_t i = 0; i < totalPixels; i++)
            {
                dst.data[i] = static_cast<T>((dst.data[i] - minVal) * scale);
            }
        }
    }
    else if constexpr (normalizationType == NormalizationType::ZERO_CENTER)
    {
        common::log_warn("NON TESTED: Applying zero-centering normalization");
        const size_t totalPixels = dst.width * dst.height * src.pixelBytes;
        ImageStats<T> stats;

        // Calculate mean
        for (size_t i = 0; i < totalPixels; i++)
        {
            stats.update(dst.data[i]);
        }
        stats.finalize();

        // Apply zero-centering
        Normalizer<T, NormalizationType::ZERO_CENTER> normalizer;
        for (size_t i = 0; i < totalPixels; i++)
        {
            dst.data[i] = normalizer(dst.data[i], stats);
        }
    }
}

// Fast box filter for extreme Scaling (when speed is critical)
template <typename T>
void fastBoxScaling(const ImageView<T>& src, ImageView<T>& dst)
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

                for (unsigned char ch = 0; ch < src.pixelBytes; ch++)
                {
                    int sum = 0;

                    for (int dy = 0; dy < yStep; dy++)
                    {
                        for (int dx = 0; dx < xStep; dx++)
                        {
                            const unsigned long srcIdx =
                                ((srcStartY + dy) * src.width + (srcStartX + dx)) * src.pixelBytes + ch;
                            sum += static_cast<int>(src.data[srcIdx]);
                        }
                    }

                    const unsigned long dstIdx = (y * dst.width + x) * src.pixelBytes + ch;
                    dst.data[dstIdx] = static_cast<T>(sum / totalSamples);
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

                for (unsigned char ch = 0; ch < src.pixelBytes; ch++)
                {
                    double sum = 0.0;
                    int count = 0;

                    for (int sy = minY; sy < maxY; sy++)
                    {
                        for (int sx = minX; sx < maxX; sx++)
                        {
                            const unsigned long srcIdx = (sy * src.width + sx) * src.pixelBytes + ch;
                            sum += static_cast<double>(src.data[srcIdx]);
                            count++;
                        }
                    }

                    const unsigned long dstIdx = (y * dst.width + x) * src.pixelBytes + ch;
                    dst.data[dstIdx] = static_cast<T>(count > 0 ? sum / count : 0);
                }
            }
        }
    }
}


inline void paintCircle(std::unique_ptr<Image>& image, const math_utils::Point& center, float radius, Pixel color)
{
    // Bresenham's circle algorithm
    int x = static_cast<int>(radius);
    int y = 0;
    int err = 0;

    while (x >= y)
    {
        // Paint the 8 octants of the circle
        image->ppx(center.x + x, center.y + y, color);
        image->ppx(center.x + y, center.y + x, color);
        image->ppx(center.x - y, center.y + x, color);
        image->ppx(center.x - x, center.y + y, color);
        image->ppx(center.x - x, center.y - y, color);
        image->ppx(center.x - y, center.y - x, color);
        image->ppx(center.x + y, center.y - x, color);
        image->ppx(center.x + x, center.y - y, color);

        if (err <= 0)
        {
            err += 2 * ++y + 1;
        }
        if (err > 0)
        {
            err -= 2 * --x + 1;
        }
    }
}

} // namespace image_utils
} // namespace linuxface

#endif // IMAGE_UTILS_H
