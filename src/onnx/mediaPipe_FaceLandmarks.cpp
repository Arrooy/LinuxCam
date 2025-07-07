#include "LinuxFace/onnx/mediaPipe_FaceLandmarks.h"

#include <vector>

#include "LinuxFace/common.h"

using namespace linuxface;

MediaPipeFaceLandmarks::MediaPipeFaceLandmarks(const std::string& onnx_model_path) : OnnxDetector(onnx_model_path)
{
    // Model expects input [1,3,192,192] named "image"
    // Output: "scores" [1], "landmarks" [1,468,3]
    ready_ = true;
}

Ort::Value MediaPipeFaceLandmarks::transform(const std::unique_ptr<Image>& image)
{
    // Ensure input_node_dims is [1,3,192,192]
    input_node_dims = {1, 3, 192, 192};
    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    float* tensor_data = input_tensor.GetTensorMutableData<float>();
    padding_ = TensorPadding::no_padding();
    // No padding, normalization as needed (MINMAX for now)
    image->toTensor(tensor_data, padding_, 192, 192, NormalizationType::MINMAX);
    return input_tensor;
}

MediaPipeFaceLandmarks::Result MediaPipeFaceLandmarks::detect(const std::unique_ptr<Image>& image)
{
    Result result;
    if (!ready_ || !image)
    {
        return result;
    }
    Ort::Value input_tensor = transform(image);
    std::vector<const char*> input_names = {"image"};
    std::vector<const char*> output_names = {"scores", "landmarks"};
    auto output_tensors =
        detector_session_->Run(Ort::RunOptions{nullptr}, input_names.data(), &input_tensor, 1, output_names.data(), 2);
    // scores: float[1]
    float* score_ptr = output_tensors[0].GetTensorMutableData<float>();
    result.score = score_ptr[0];
    // landmarks: float[1,468,3]
    float* lmk_ptr = output_tensors[1].GetTensorMutableData<float>();
    result.landmarks.resize(468, std::vector<float>(3, 0.0f));
    for (int i = 0; i < 468; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            result.landmarks[i][j] = lmk_ptr[i * 3 + j];
        }
    }
    return result;
}
