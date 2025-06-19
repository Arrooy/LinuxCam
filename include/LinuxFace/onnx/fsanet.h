#ifndef FSANETDETECTOR_H
#define FSANETDETECTOR_H

#include "LinuxFace/detectors.h"
#include "LinuxFace/onnx/onnxDetector.h"

namespace linuxface
{

class FsanetDetector : public OnnxDetector
{
  public:
    explicit FsanetDetector(const std::string& onnx_model_path) : OnnxDetector(onnx_model_path) {};
    ~FsanetDetector() = default;
    std::vector<Ort::Value> transform(const std::unique_ptr<Image>& image) override;
    void detect(const std::unique_ptr<Image>& image, Face& face);

  private:

    // Add padding to input image.
    static constexpr const float pad_ = 0.3f;
    static constexpr const int input_width_ = 64;
    static constexpr const int input_height_ = 64;
};
} // namespace linuxface

#endif // FSANETDETECTOR_H
