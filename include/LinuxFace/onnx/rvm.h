#ifndef RVM_H
#define RVM_H
#include "LinuxFace/Image/tensor_padding.h"
#include "LinuxFace/onnx/onnxDetector.h"

/**
 * Model source https://github.com/PeterL1n/RobustVideoMatting
 * https://github.com/PeterL1n/RobustVideoMatting/releases/download/v1.0.0/rvm_mobilenetv3_fp32.onnx
 */
namespace linuxface
{
class RobustVideoMatting : public OnnxDetector
{

  public:
    explicit RobustVideoMatting(const std::string& onnxModelPath);
    ~RobustVideoMatting() = default;
    Ort::Value transform(const std::unique_ptr<Image>& image) override;

    void detect(const std::unique_ptr<Image>& image, std::unique_ptr<Image>& frg, std::unique_ptr<Image>& matte);

    void initialize();

    bool isImageCompatible(const std::unique_ptr<Image>& image) const;

  private:
    // The downsample must make the downsampled_resolution between 256px and 512px.
    float downsample_ = 0.25f;
    static constexpr const int InputWidth = 512;
    static constexpr const int InputHeight = 512;

    unsigned long lastWidth_{0u};
    unsigned long lastHeight_{0u};

    std::vector<Ort::Value> rec_;
    std::vector<std::vector<float>> rec_cpu_data_;
    TensorPadding padding_;
};
} // namespace linuxface
#endif
