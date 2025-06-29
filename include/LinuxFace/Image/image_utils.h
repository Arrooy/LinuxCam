#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

#include "LinuxFace/Image/image.h"
#include "LinuxFace/math_utils.h"
#include <memory>
#include <vector>

namespace linuxface {
namespace image_utils {

// Align face using 5 landmarks and a template (returns nullptr if not possible)
inline std::unique_ptr<Image> align_face_affine(const Image& input_img, const std::vector<math_utils::Point>& landmarks,
                                                const float template_points[5][2], int target_size)
{
    if (landmarks.size() != 5)
        return nullptr;
    std::vector<math_utils::Point> template_pts;
    for (int i = 0; i < 5; ++i)
    {
        template_pts.emplace_back(static_cast<long>(template_points[i][0] * target_size),
                                 static_cast<long>(template_points[i][1] * target_size));
    }
    float src[10], dst[10];
    for (int i = 0; i < 5; ++i)
    {
        src[2 * i] = static_cast<float>(landmarks[i].x);
        src[2 * i + 1] = static_cast<float>(landmarks[i].y);
        dst[2 * i] = static_cast<float>(template_pts[i].x);
        dst[2 * i + 1] = static_cast<float>(template_pts[i].y);
    }
    float M[6] = {0};
    math_utils::estimate_affine_2d(src, dst, 5, M);
    return input_img.affineWarp(M, target_size, target_size);
}

} // namespace image_utils
} // namespace linuxface

#endif // IMAGE_UTILS_H
