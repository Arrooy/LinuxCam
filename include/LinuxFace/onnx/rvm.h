#ifndef RVM_H
#define RVM_H
#include "LinuxFace/onnx/onnxDetector.h"

namespace linuxface
{
class RobustVideoMatting : public OnnxDetector
{

  public:
    explicit RobustVideoMatting(const std::string& onnx_model_path);
    ~RobustVideoMatting() = default;
    std::vector<Ort::Value> transform(const std::unique_ptr<Image>& image) override;

    void detect(const std::unique_ptr<Image>& image, std::unique_ptr<Image>& frg, std::unique_ptr<Image>& matte);

    void initialize();
  private:
    // The downsample must make the downsampled_resolution between 256px and 512px.
    float downsample_ = 0.25f;
    static constexpr const int input_width_ = 512;
    static constexpr const int input_height_ = 512;

    std::vector<Ort::Value> rec_;
    std::vector<std::vector<float>> rec_cpu_data_;
    TensorPadding padding_;
};
} // namespace linuxface
#endif
