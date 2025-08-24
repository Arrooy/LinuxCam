
#ifndef DLIBDETECTORS_H
#define DLIBDETECTORS_H

#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_processing/generic_image.h>
#include <dlib/image_processing/shape_predictor.h>
#include <dlib/pixel.h>

#include "LinuxFace/common.h"
#include "LinuxFace/detectors.h"
#include "LinuxFace/face.h"

// Tested and it works. The detection is not very good,
// but it works. Good lighting is required. and release compilation.

#include <memory>

namespace linuxface
{

struct DlibImageWrapper
{
    explicit DlibImageWrapper(const std::unique_ptr<Image>& image) : image_(image) {}

    long nr() const { return image_->info.height; }
    long nc() const { return image_->info.width; }

    long numRows() const { return image_->info.height; }
    long numColumns() const { return image_->info.width; }

    long widthStep() const { return nc() * image_->info.pixelSizeBytes; }

    dlib::rgb_pixel operator()(long row, long col) const
    {
        const Pixel px = (*image_)(row, col);
        return {px.r, px.g, px.b};
    }

    const std::unique_ptr<Image>& image_;
};

class DlibFaceDetector : public FaceDetector
{
  public:
    DlibFaceDetector();
    ~DlibFaceDetector() = default;
    std::vector<Face> detect(const std::unique_ptr<Image>& image) override;

  private:
    dlib::frontal_face_detector detector_;
};

// DlibShapeDetector: detects facial landmarks using dlib's shape_predictor
class DlibShapeDetector : public ShapeDetector
{
  public:
    explicit DlibShapeDetector(std::string modelPath = "../models/shape_predictor_68_face_landmarks.dat");
    ~DlibShapeDetector();
    // Given an image and bounding boxes, returns Face objects with landmarks
    std::vector<Face>
    detect(const std::unique_ptr<Image>& image, std::vector<math_utils::Rect<float>>& facesRect) override;

  private:
    std::unique_ptr<dlib::shape_predictor> predictor_;
    std::string model_path_;
};
} // namespace linuxface

// Dlib generic image API support for DlibImageWrapper
namespace dlib
{
inline long num_rows(const DlibImageWrapper& img)
{
    return img.numRows();
}
inline long num_columns(const DlibImageWrapper& img)
{
    return img.numColumns();
}
inline long width_step(const DlibImageWrapper& img)
{
    return img.widthStep();
}
inline const void* image_data(const DlibImageWrapper& img)
{
    return static_cast<const void*>(img.image_->data());
}
} // namespace dlib
#endif // DLIBDETECTORS_H
