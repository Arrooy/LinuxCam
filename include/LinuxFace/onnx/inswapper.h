#ifndef INSWAPPER_H
#define INSWAPPER_H

#include "LinuxFace/onnx/onnxDetector.h"

namespace linuxface
{

class InSwapper : public OnnxDetector
{
  public:
    explicit InSwapper(const std::string& onnx_model_path);
    ~InSwapper() = default;
    std::vector<Ort::Value> transform(const std::unique_ptr<Image>& image) override;

    bool swap(const std::vector<float>& src_embedding, const std::vector<math_utils::Point>& dst_landmarks, const Image& dst_face, Image& out_image);

  private:
    static constexpr const int input_width_ = 128;
    static constexpr const int input_height_ = 128;
    TensorPadding padding_;
};

} // namespace linuxface

#endif // INSWAPPER_H
