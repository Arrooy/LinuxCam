#ifndef INSWAPPER_H
#define INSWAPPER_H

#include "LinuxFace/Image/tensor_padding.h"
#include "LinuxFace/onnx/onnxDetector.h"
#include "LinuxFace/face.h"

namespace linuxface
{

class InSwapper : public OnnxDetector
{
  public:
    explicit InSwapper(const std::string& onnxModelPath);
    ~InSwapper() = default;
    Ort::Value transform(const std::unique_ptr<Image>& image) override;

    std::pair<bool, std::array<double, 6>> swap(const std::vector<float>& srcEmbedding,
                                                 const Image& dstFace, Face& dstFaceObj, Image& outImage);

  private:
    static constexpr const int InputWidth = 128;
    static constexpr const int InputHeight = 128;
    TensorPadding padding_;
};

} // namespace linuxface

#endif // INSWAPPER_H
