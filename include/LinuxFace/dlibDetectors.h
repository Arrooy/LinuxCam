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
    DlibImageWrapper(const std::unique_ptr<Image>& image) : image_(image) {}

    long nr() const { return image_->info.height; }
    long nc() const { return image_->info.width; }

    long num_rows() const { return image_->info.height; }
    long num_columns() const { return image_->info.width; }

    long width_step() const { return nc() * image_->info.pixelSizeBytes; }

    dlib::rgb_pixel operator()(long row, long col) const
    {
        Pixel px = (*image_)(row, col);
        return dlib::rgb_pixel(px.r, px.g, px.b);
    }

    const std::unique_ptr<Image>& image_;
};

inline long num_rows(const DlibImageWrapper& img)
{
    return img.num_rows();
}

inline long num_columns(const DlibImageWrapper& img)
{
    return img.num_columns();
}

inline long width_step(const DlibImageWrapper& img)
{
    return img.width_step();
}

inline const void* image_data(const DlibImageWrapper& img)
{
    return static_cast<const void*>(img.image_->data());
}

inline void* image_data(DlibImageWrapper& img)
{
    return static_cast<void*>(img.image_->data());
}

inline void set_image_size(DlibImageWrapper& img, long rows, long cols)
{
    common::log_error("DlibImageWrapper::set_image_size is not implemented!!");
}

inline void swap(DlibImageWrapper& a, DlibImageWrapper& b)
{
    common::log_error("DlibImageWrapper::swap is not implemented!!");
}

class DlibFaceDetector : public FaceDetector
{
  public:
    DlibFaceDetector();
    ~DlibFaceDetector() = default;
    virtual std::vector<Face> detect(const std::unique_ptr<Image>& image) override;

  private:
    dlib::frontal_face_detector detector_;
};


// DlibShapeDetector: detects facial landmarks using dlib's shape_predictor
class DlibShapeDetector : public ShapeDetector
{
  public:
    explicit DlibShapeDetector(const std::string& model_path = "../models/shape_predictor_68_face_landmarks.dat");
    ~DlibShapeDetector();
    // Given an image and bounding boxes, returns Face objects with landmarks
    virtual std::vector<Face>
    detect(const std::unique_ptr<Image>& image, const std::vector<math_utils::Rect<float>>& faces_rect) override;

  private:
    std::unique_ptr<dlib::shape_predictor> predictor_;
    std::string model_path_;
};

} // namespace linuxface
#endif // DLIBDETECTORS_H
