#ifndef FSANETDETECTOR_H
#define FSANETDETECTOR_H

#include "LinuxFace/detectors.h"
#include "LinuxFace/onnx/onnxDetector.h"

namespace linuxface
{

class FsanetDetector : public OnnxDetector
{
  public:
    explicit FsanetDetector(const std::string& onnxModelPath) : OnnxDetector(onnxModelPath) {};
    ~FsanetDetector() = default;
    Ort::Value transform(const std::unique_ptr<Image>& image) override;
    void detect(const std::unique_ptr<Image>& image, Face& face);

  private:
    // Add padding to input image.
    static constexpr const float Pad = 0.3f;
    static constexpr const int InputWidth = 64;
    static constexpr const int InputHeight = 64;
};
} // namespace linuxface

#endif // FSANETDETECTOR_H
