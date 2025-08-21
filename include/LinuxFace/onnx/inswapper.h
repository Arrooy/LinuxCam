#ifndef INSWAPPER_H
#define INSWAPPER_H

#include "LinuxFace/onnx/onnxDetector.h"
#include "LinuxFace/Image/tensor_padding.h"

namespace linuxface
{

class InSwapper : public OnnxDetector
{
  public:
   explicit InSwapper(const std::string& onnxModelPath);
   ~InSwapper() = default;
   Ort::Value transform(const std::unique_ptr<Image>& image) override;

   bool swap(const std::vector<float>& srcEmbedding,
             const std::vector<math_utils::Point<>>& dstLandmarks,
             const Image& dstFace, Image& outImage);

  private:
   static constexpr const int InputWidth = 128;
   static constexpr const int InputHeight = 128;
   TensorPadding padding_;
};

} // namespace linuxface

#endif // INSWAPPER_H
