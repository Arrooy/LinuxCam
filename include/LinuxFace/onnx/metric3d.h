#ifndef METRIC3D_H
#define METRIC3D_H

#include "LinuxFace/Image/tensor_padding.h"
#include "LinuxFace/onnx/onnxDetector.h"

// Source https://github.com/YvanYin/Metric3D/tree/main
//  tested python inference, small model doesnt work well.
//  large model works nice but inference time is too big.

namespace linuxface
{

class DepthImage; // Forward declaration

class Metric3D : public OnnxDetector
{
  public:
    explicit Metric3D(const std::string& onnx_model_path);

    Ort::Value transform(const std::unique_ptr<Image>& image) override;

    std::unique_ptr<DepthImage> detectDepth(const std::unique_ptr<Image>& image);

  private:
    int target_width_;
    int target_height_;
    TensorPadding padding_; // Store padding info for tensor operations
};

} // namespace linuxface

#endif // METRIC3D_H
