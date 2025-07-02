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

// Align face using 5 landmarks and a template (returns nullptr if not possible)
inline std::unique_ptr<Image> align_face_affine(const Image& input_img, const std::vector<math_utils::Point>& landmarks,
                                                const double template_points[5][2], int target_size)
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
        src[2 * i] = static_cast<double>(landmarks[i].x);
        src[2 * i + 1] = static_cast<double>(landmarks[i].y);
        dst[2 * i] = static_cast<double>(template_pts[i].x);
        dst[2 * i + 1] = static_cast<double>(template_pts[i].y);
    }
    double M[6] = {0};
    math_utils::estimate_affine_2d(src, dst, 5, M);
    return input_img.affineWarp(M, target_size, target_size);
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
inline std::unique_ptr<Image> create_static_box_mask(const std::vector<double>& crop_size)
{
    int width = static_cast<int>(crop_size[0]);
    int height = static_cast<int>(crop_size[1]);
    double face_mask_blur = 0.3;
    std::vector<int> face_mask_padding = {0, 0, 0, 0};
    int blur_amount = static_cast<int>(width * 0.5 * face_mask_blur);
    int blur_area = std::max(blur_amount / 2, 1);
    std::unique_ptr<Image> box_mask = std::make_unique<Image>(width * height);
    box_mask->info.width = width;
    box_mask->info.height = height;
    box_mask->info.pixelSizeBytes = 1;
    box_mask->info.format = linuxface::ImageFormat::GRAYSCALE;
    std::fill(box_mask->data(), box_mask->data() + width * height, 255);
    int top_padding = std::max(blur_area, static_cast<int>(height * face_mask_padding[0] / 100.0));
    int bottom_padding = std::max(blur_area, static_cast<int>(height * face_mask_padding[2] / 100.0));
    int right_padding = std::max(blur_area, static_cast<int>(width * face_mask_padding[1] / 100.0));
    int left_padding = std::max(blur_area, static_cast<int>(width * face_mask_padding[3] / 100.0));
    for (int y = 0; y < top_padding; ++y)
    {
        std::fill(box_mask->data() + y * width, box_mask->data() + (y + 1) * width, 0);
    }
    for (int y = height - bottom_padding; y < height; ++y)
    {
        std::fill(box_mask->data() + y * width, box_mask->data() + (y + 1) * width, 0);
    }
    for (int y = 0; y < height; ++y)
    {
        std::fill(box_mask->data() + y * width, box_mask->data() + y * width + left_padding, 0);
    }
    for (int y = 0; y < height; ++y)
    {
        std::fill(box_mask->data() + y * width + (width - right_padding), box_mask->data() + (y + 1) * width, 0);
    }
    if (blur_amount > 0)
    {
        std::unique_ptr<Image> blurred = box_mask->deepCopy();
        int radius = blur_amount / 2;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int sum = 0, count = 0;
                for (int dy = -radius; dy <= radius; ++dy)
                {
                    for (int dx = -radius; dx <= radius; ++dx)
                    {
                        int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && nx < width && ny >= 0 && ny < height)
                        {
                            sum += box_mask->data()[ny * width + nx];
                            ++count;
                        }
                    }
                }
                blurred->data()[y * width + x] = static_cast<unsigned char>(sum / count);
            }
        }
        blurred->info.pixelSizeBytes = 1;
        blurred->info.format = linuxface::ImageFormat::GRAYSCALE;
        return blurred;
    }
    return box_mask;
}

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
/*GPT original version with only blur on padding areas.
inline std::unique_ptr<Image> create_static_box_mask(const std::vector<double>& crop_size)
{
    int width = static_cast<int>(crop_size[0]);
    int height = static_cast<int>(crop_size[1]);
    double face_mask_blur = 0.3;
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
}

*/


} // namespace image_utils
} // namespace linuxface

#endif // IMAGE_UTILS_H
