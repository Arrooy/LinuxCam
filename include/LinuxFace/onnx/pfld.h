#ifndef PFLD_H
#define PFLD_H

#include "LinuxFace/detectors.h"
#include "LinuxFace/onnx/onnxDetector.h"

namespace linuxface
{
class PFLDDetector : public OnnxDetector
{
  public:
    explicit PFLDDetector(const std::string& onnx_model_path);
    ~PFLDDetector() = default;
    // Receives the full image and a Face, fills landmarks in the Face
    void detect(const std::unique_ptr<Image>& image, Face& face);

    void detectSimilar(const std::unique_ptr<Image>& image, Face& face);
    void detectOpenCv(const std::unique_ptr<Image>& image, Face& face);
  private:
    Ort::Value transform(const std::unique_ptr<Image>& image) override;
    TensorPadding pfld_padding_;


    math_utils::Point<double>
    alignedToOriginalCoords(double x_aligned, double y_aligned, double crop_left, double crop_top, double minX,
                            double minY, double angleRad, const math_utils::Point<double>& eye_center, double scale);
};
} // namespace linuxface

#endif // PFLD_H
