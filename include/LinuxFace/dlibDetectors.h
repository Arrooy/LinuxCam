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

namespace linuxface
{
struct DlibImageWrapper
{
  public:
    explicit DlibImageWrapper(const std::unique_ptr<Image>& image) : image(image) {}

    long nr() const { return image->info.height; }
    long nc() const { return image->info.width; }

    long numRows() const { return image->info.height; }
    long numColumns() const { return image->info.width; }

    long widthStep() const { return nc() * image->info.pixelSizeBytes; }

    dlib::rgb_pixel operator()(long row, long col) const
    {
        const Pixel px = (*image)(row, col);
        return {px.r, px.g, px.b};
    }

    const std::unique_ptr<Image>& image;
};

inline long numRows(const DlibImageWrapper& img)
{
    return img.numRows();
}

inline long numColumns(const DlibImageWrapper& img)
{
    return img.numColumns();
}

inline long widthStep(const DlibImageWrapper& img)
{
    return img.widthStep();
}

inline const void* imageData(const DlibImageWrapper& img)
{
    return static_cast<const void*>(img.image->data());
}

inline void* imageData(DlibImageWrapper& img)
{
    return static_cast<void*>(img.image->data());
}

inline void setImageSize(DlibImageWrapper& /*img*/, long /*rows*/, long /*cols*/)
{
    common::logError("DlibImageWrapper::set_image_size is not implemented!!");
}

inline void swap(DlibImageWrapper& /*a*/, DlibImageWrapper& /*b*/) noexcept
{
    common::logError("DlibImageWrapper::swap is not implemented!!");
}

class DlibFaceDetector : public FaceDetector
{
  public:
    DlibFaceDetector();
    ~DlibFaceDetector() = default;
    std::vector<Face> detect(const std::unique_ptr<Image>& image) override;

  private:
    dlib::frontal_face_detector detector_{};
};


// DlibShapeDetector: detects facial landmarks using dlib's shape_predictor
class DlibShapeDetector : public ShapeDetector
{
  public:
    explicit DlibShapeDetector(const std::string& modelPath = "../models/shape_predictor_68_face_landmarks.dat");
    ~DlibShapeDetector();
    // Given an image and bounding boxes, returns Face objects with landmarks
    std::vector<Face>
    detect(const std::unique_ptr<Image>& image, const std::vector<math_utils::Rect<float>>& facesRect) override;

  private:
    std::unique_ptr<dlib::shape_predictor> predictor_{};
    std::string model_path_;
};

} // namespace linuxface
#endif // DLIBDETECTORS_H
