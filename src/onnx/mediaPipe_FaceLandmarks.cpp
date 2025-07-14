#include "LinuxFace/onnx/mediaPipe_FaceLandmarks.h"

#include <vector>

#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"
#include "LinuxFace/Image/image_utils.h"
using namespace linuxface;

MediaPipeFaceLandmarks::MediaPipeFaceLandmarks(const std::string& onnx_model_path) : OnnxDetector(onnx_model_path)
{
    // Model expects input [1,3,192,192] named "image"
    // Output: "scores" [1], "landmarks" [1,468,3]
}

Ort::Value MediaPipeFaceLandmarks::transform(const std::unique_ptr<Image>& image)
{
    // Ensure input_node_dims is [1,3,192,192]
    input_node_dims = {1, 3, 192, 192};
    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    float* tensor_data = input_tensor.GetTensorMutableData<float>();
    padding_ = TensorPadding::scrfd();
    // No padding, normalization as needed (MINMAX for now)
    image->toTensor(tensor_data, padding_, 192, 192, NormalizationType::MINMAX);
    auto test = image_utils::convertToRawImage<NormalizationType::MINMAX>(tensor_data, 192, 192);
    if(test)
    {
        if(!test->saveToDisk("media_pipe_input_tensor.ppm"))
        {
            common::log_info("MediaPipeFaceLandmarks: Not Saved test image to disk.");
        }
    }
    return input_tensor;
}

MediaPipeFaceLandmarks::Result MediaPipeFaceLandmarks::detect(const std::unique_ptr<Image>& image)
{
    Result result;
    if (!ready_ || !image)
    {
        return result;
    }
    Profiler::getInstance().start("MediaPipeFaceLandmarks", "detect landmarks");
    Ort::Value input_tensor = transform(image);
    std::vector<const char*> input_names = {"image"};
    std::vector<const char*> output_names = {"scores", "landmarks"};
    auto output_tensors =
        detector_session_->Run(Ort::RunOptions{nullptr}, input_names.data(), &input_tensor, 1, output_names.data(), 2);
    // scores: float[1]
    auto score_tensor = std::move(output_tensors[0]);
    if(score_tensor.IsTensor() == false ||
       score_tensor.GetTensorTypeAndShapeInfo().GetShape()[0] != 1)
    {
        common::log_error("MediaPipeFaceLandmarks: Score tensor is not valid.");
        auto shape = score_tensor.GetTensorTypeAndShapeInfo().GetShape();
        for(const auto& dim : shape)
        {
            common::log_error("MediaPipeFaceLandmarks: Score tensor dimension: %ld", dim);
        }
        return result; // Return empty result if score is not available
    }
    float* score_ptr = score_tensor.GetTensorMutableData<float>();
    if (score_ptr == nullptr)
    {
        result.score = 0.0f; // Handle case where score is not available
    }
    else
    {
        result.score = score_ptr[0];
    }
    // landmarks: float[1,468,3]
    float* lmk_ptr = output_tensors[1].GetTensorMutableData<float>();
    if(lmk_ptr == nullptr)
    {
        common::log_error("MediaPipeFaceLandmarks: Landmarks tensor is null.");
        return result; // Return empty result if landmarks are not available
    }
    result.landmarks.resize(468, std::vector<float>(3, 0.0f));

    for (int i = 0; i < 468; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            result.landmarks[i][j] = lmk_ptr[i * 3 + j];
        }
    }
    Profiler::getInstance().stop("MediaPipeFaceLandmarks", "detect landmarks");
    return result;
}
