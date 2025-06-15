#include "FunnyFace/dlibDetectors.h"

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

std::vector<FaceBoundingBox> DlibFaceDetector::detect(const std::unique_ptr<Image>& image)
{
    // Add adapt image
    DlibImageWrapper dlib_image(image);

    // Detect faces
    std::vector<dlib::rectangle> rects_detected = detector_(dlib_image);

    // Convert to FaceBoundingBox
    std::vector<FaceBoundingBox> rects_bb;
    for (const auto& rect : rects_detected)
    {
        rects_bb.push_back(FaceBoundingBox(rect.left(), rect.top(), rect.right(), rect.bottom()));
    }

    return rects_bb;
}
