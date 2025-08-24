#ifndef PFLD_H
#define PFLD_H

#include "LinuxFace/Image/tensor_padding.h"
#include "LinuxFace/detectors.h"
#include "LinuxFace/onnx/onnxDetector.h"

namespace linuxface
{
class PFLDDetector : public OnnxDetector
{
  public:
    explicit PFLDDetector(const std::string& onnxModelPath);
    ~PFLDDetector() = default;
    // Receives the full image and a Face, fills landmarks in the Face
    void detect(const std::unique_ptr<Image>& image, Face& face);

    void detectSimilar(const std::unique_ptr<Image>& image, Face& face);
    void detectOpenCv(const std::unique_ptr<Image>& image, Face& face);
  private:
    Ort::Value transform(const std::unique_ptr<Image>& image) override;
    TensorPadding pfld_padding_;

    static math_utils::Point<double>
    alignedToOriginalCoords(double xAligned, double yAligned, double cropLeft, double cropTop, double minX, double minY,
                            double angleRad, const math_utils::Point<double>& eyeCenter, double scale);
};
} // namespace linuxface

#endif // PFLD_H
