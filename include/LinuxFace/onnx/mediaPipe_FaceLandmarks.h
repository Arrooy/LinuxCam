#ifndef MEDIAPIPE_FACELANDMARKS_H
#define MEDIAPIPE_FACELANDMARKS_H

#include "LinuxFace/Image/tensor_padding.h"
#include "LinuxFace/onnx/onnxDetector.h"

namespace linuxface
{

class MediaPipeFaceLandmarks : public OnnxDetector
{
  public:
    explicit MediaPipeFaceLandmarks(const std::string& onnxModelPath);
    ~MediaPipeFaceLandmarks() = default;

    // Prepares the input tensor for the model
    Ort::Value transform(const std::unique_ptr<Image>& image) override;

    // Runs inference and returns the raw output (score and landmarks)
    struct Result
    {
        float score{};
        std::vector<std::vector<float>> landmarks; // [468][3]
    };
    // Detect landmarks from a face image (expects cropped face, 192x192)
    Result detect(const std::unique_ptr<Image>& image);

    TensorPadding padding_;
};

} // namespace linuxface
#endif // MEDIAPIPE_FACELANDMARKS_H
