#include "LinuxFace/dlibDetectors.h"

#include <dlib/image_processing/shape_predictor.h>
#include <utility>

#include "LinuxFace/profiler.h"
using linuxface::DlibFaceDetector;
using linuxface::DlibShapeDetector;
using linuxface::Face;
using linuxface::Image;


namespace dlib
{
template <>
struct ImageTraits<linuxface::DlibImageWrapper>
{
    typedef rgb_pixel pixel_type;
    static const bool HAS_SIMD = false;
};
} // namespace dlib


DlibFaceDetector::DlibFaceDetector()
{
    detector_ = dlib::get_frontal_face_detector();
}


std::vector<Face> DlibFaceDetector::detect(const std::unique_ptr<Image>& image)
{
    Profiler::getInstance().start("DLIB", "Face Detector");
    const DlibImageWrapper dlib_image(image);

    // Detect faces
    const std::vector<dlib::rectangle> rects_detected = detector_(dlib_image);

    std::vector<Face> faces;
    for (const auto& rect : rectsDetected)
    {
        faces.emplace_back(FaceBoundingBox(static_cast<float>(rect.left()), static_cast<float>(rect.top()),
                                           static_cast<float>(rect.right()), static_cast<float>(rect.bottom())));
    }

    Profiler::getInstance().stop("DLIB", "Face Detector");
    return faces;
}


// DlibShapeDetector: detects facial landmarks using dlib's shape_predictor
DlibShapeDetector::DlibShapeDetector(std::string model_path) : model_path_(std::move(model_path))
{
    predictor_ = std::make_unique<dlib::shape_predictor>();
    try
    {
        dlib::deserialize(model_path_) >> (*predictor_);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error(std::string("Failed to load shape predictor: ") + e.what());
    }
}

DlibShapeDetector::~DlibShapeDetector() = default;

std::vector<Face>
DlibShapeDetector::detect(const std::unique_ptr<Image>& image, const std::vector<math_utils::Rect<float>>& faces_rect)
{
    std::vector<Face> faces;
    if (faces_rect.empty())
    {
        return faces;
    }
    const DlibImageWrapper dlib_image(image);
    for (const auto& rect : facesRect)
    {
        // Convert to dlib rectangle
        const dlib::rectangle dlibRect(static_cast<long>(rect.l), static_cast<long>(rect.t), static_cast<long>(rect.r),
                                       static_cast<long>(rect.b));
        // Predict landmarks
        dlib::full_object_detection shape = (*predictor_)(dlibImage, dlibRect);
        std::vector<FaceLandmark> landmarks;
        for (unsigned int i = 0; i < shape.num_parts(); ++i)
        {
            FaceLandmark lm;
            lm.i = i;
            lm.p = math_utils::Point3D(shape.part(i).x(), shape.part(i).y(), 0.0); // Dlib is 2D, z=0
            landmarks.push_back(lm);
        }
        // Construct Face with landmarks and bounding box
        faces.emplace_back(landmarks, FaceBoundingBox(rect.l, rect.t, rect.r, rect.b));
    }
    return faces;
}
