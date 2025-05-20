#include "dlibDetectors.h"

using namespace funnyface;

namespace dlib
{
    template <>
    struct image_traits<DlibImageWrapper>
    {
        typedef rgb_pixel pixel_type;
        static const bool has_simd = false;
    };
} // namespace dlib


DlibFaceDetector::DlibFaceDetector()
{
    detector_ = dlib::get_frontal_face_detector();
}

DlibFaceDetector::~DlibFaceDetector(){}

std::vector<math_utils::Rect> DlibFaceDetector::detect(const Image& image)
{
    // Add adapt image
    DlibImageWrapper dlib_image(image);

    // Detect faces
    std::vector<dlib::rectangle> rects_detected = detector_(dlib_image);

    // Convert to math_utils::Rect
    std::vector<math_utils::Rect> rects;
    for (const auto& rect : rects_detected)
    {
        rects.push_back(math_utils::Rect(rect.left(), rect.top(), rect.right(), rect.bottom()));
    }

    return rects;
}
