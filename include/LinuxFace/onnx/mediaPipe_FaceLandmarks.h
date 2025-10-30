#ifndef MEDIAPIPE_FACELANDMARKS_H
#define MEDIAPIPE_FACELANDMARKS_H

#include "LinuxFace/Image/tensor_padding.h"
#include "LinuxFace/face.h"
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
        float tracking{};
        std::vector<std::vector<float>> landmarks; // [468][3]
    };

    // Detect landmarks on an aligned face image
    Result detectAligned(const std::unique_ptr<Image>& image);

    // Detect landmarks on a full image given the target face
    Face detect(const std::unique_ptr<Image>& image, Face& face);

    // Get the model's expected input dimensions
    int getInputWidth() const { return width_; }
    int getInputHeight() const { return height_; }

    TensorPadding padding_;
  private:
    template <typename T>
    bool extract(Ort::Value tensor, T& value);
    bool extractLandmarks(Ort::Value landmarksTensor, std::vector<std::vector<float>>& landmarks);
};


template <typename T>
bool MediaPipeFaceLandmarks::extract(Ort::Value tensor, T& value)
{
    if (!tensor.IsTensor()) {
        common::logError("MediaPipeFaceLandmarks: Tensor is not a tensor.");
        value = 0.0f;
        return false;
    }
    auto shape = tensor.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.empty() || shape[0] != 1) {
        common::logError("MediaPipeFaceLandmarks: Tensor shape is invalid or empty.");
        for (const auto& dim : shape) {
            common::logError("MediaPipeFaceLandmarks: Tensor dimension is: %ld", dim);
        }
        value = 0.0f;
        return false;
    }
    auto* valuePtr = tensor.GetTensorMutableData<float>();
    if (valuePtr == nullptr) {
        common::logError("MediaPipeFaceLandmarks: Tensor data pointer is null.");
        value = 0.0f;
        return false;
    }
    value = *valuePtr;
    return true;
}

} // namespace linuxface
#endif // MEDIAPIPE_FACELANDMARKS_H
