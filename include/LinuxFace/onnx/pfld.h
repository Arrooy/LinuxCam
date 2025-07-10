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
  private:
    Ort::Value transform(const std::unique_ptr<Image>& image) override;
};
} // namespace linuxface

#endif // PFLD_H
