#ifndef MODNET_H
#define MODNET_H
#include "LinuxFace/onnx/onnxDetector.h"
#include "LinuxFace/Image/tensor_padding.h"
namespace linuxface
{
// Source https://github.com/ZHKKKe/MODNet
// Model: https://drive.google.com/file/d/1cgycTQlYXpTh26gB9FTnthE7AvruV8hd/view
// Working fine. good framerate (90fps)
// Problems with resolutions that differ from input 640x480.
// TODO(runner): Fix problems with HD.

class MODNetDetector : public OnnxDetector
{
  public:
   explicit MODNetDetector(const std::string& onnxModelPath)
       : OnnxDetector(onnxModelPath){};
   ~MODNetDetector() = default;
   Ort::Value transform(const std::unique_ptr<Image>& image) override;
   void detect(const std::unique_ptr<Image>& image,
               std::unique_ptr<Image>& matte);

  private:
   static constexpr const int InputWidth = 512;
   static constexpr const int InputHeight = 512;

   TensorPadding padding_;
};

} // namespace linuxface
#endif
