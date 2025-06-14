#ifndef FSANETDETECTOR_H
#define FSANETDETECTOR_H

#include "FunnyFace/detectors.h"
#include "FunnyFace/onnx/onnxDetector.h"

namespace funnyface
{

class FsanetDetector : public OnnxDetector
{
  public:
    explicit FsanetDetector(const std::string& onnx_path) : OnnxDetector(onnx_path) {};
    ~FsanetDetector() = default;
    Ort::Value transform(const std::unique_ptr<Image>& image) override;
    void detect(const std::unique_ptr<Image>& image);

  private:

    // Add padding to input image.
    static constexpr const float pad_ = 0.3f;
    static constexpr const int input_width_ = 64;
    static constexpr const int input_height_ = 64;
};
} // namespace funnyface

#endif // FSANETDETECTOR_H
