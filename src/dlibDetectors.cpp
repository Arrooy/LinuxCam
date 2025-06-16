#include "FunnyFace/dlibDetectors.h"
#include "FunnyFace/profiler.h"
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

std::vector<Face> DlibFaceDetector::detect(const std::unique_ptr<Image>& image)
{
    Profiler::getInstance().start("DLIB", "Face Detector");
    // Add adapt image
    DlibImageWrapper dlib_image(image);

    // Detect faces
    std::vector<dlib::rectangle> rects_detected = detector_(dlib_image);

    // Convert to Face
    std::vector<Face> rects_bb;
    for (const auto& rect : rects_detected)
    {
        rects_bb.push_back(Face(FaceBoundingBox(rect.left(), rect.top(), rect.right(), rect.bottom())));
    }

    Profiler::getInstance().stop("DLIB", "Face Detector");
    return rects_bb;
}
