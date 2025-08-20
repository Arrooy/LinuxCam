#include "LinuxFace/onnx/mediaPipe_FaceLandmarks.h"

#include <vector>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"
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
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(allocator, input_node_dims.data(), input_node_dims.size());
    auto* tensor_data = input_tensor.GetTensorMutableData<float>();
    padding = TensorPadding::scrfd();
    // No padding, normalization as needed (MINMAX for now)
    image->toTensor(tensorData, padding, 192, 192, NormalizationType::MINMAX);
    // auto test = image_utils::convertToRawImage<NormalizationType::MINMAX>(tensor_data, 192, 192);
    // if(test)
    // {
    //     if(!test->saveToDisk("media_pipe_input_tensor.ppm"))
    //     {
    //         common::logInfo("MediaPipeFaceLandmarks: Not Saved test image to disk.");
    //     }
    // }
    // common::logInfo("MediaPipeFaceLandmarks: Input image dimensions: %ldx%ld", image->info.width,
    // image->info.height); common::logInfo("MediaPipeFaceLandmarks: Input tensor prepared with dimensions: %ldx%ld",
    // input_node_dims[3], input_node_dims[2]); image->saveToDisk("media_pipe_input_image.ppm");
    return input_tensor;
}

MediaPipeFaceLandmarks::Result MediaPipeFaceLandmarks::Detect(const std::unique_ptr<Image>& image)
{
    Result result;
    if (!ready || !image)
    {
        return result;
    }
    Profiler::getInstance().start("MediaPipeFaceLandmarks", "detect landmarks");
    const Ort::Value input_tensor = transform(image);
    std::vector<const char*> input_names = {"image"};
    std::vector<const char*> output_names = {"scores", "landmarks"};
    auto output_tensors =
        detector_session->Run(Ort::RunOptions{nullptr}, inputNames.data(), &input_tensor, 1, outputNames.data(), 2);
    // scores: float[1]
    auto score_tensor = std::move(outputTensors[0]);
    if (!scoreTensor.IsTensor() || scoreTensor.GetTensorTypeAndShapeInfo().GetShape()[0] != 1)
    {
        common::logError("MediaPipeFaceLandmarks: Score tensor is not valid.");
        auto shape = scoreTensor.GetTensorTypeAndShapeInfo().GetShape();
        for (const auto& dim : shape)
        {
            common::logError("MediaPipeFaceLandmarks: Score tensor dimension: %ld", dim);
        }
        return result; // Return empty result if score is not available
    }
    auto* score_ptr = scoreTensor.GetTensorMutableData<float>();
    if (score_ptr == nullptr)
    {
        result.score = 0.0f; // Handle case where score is not available
    }
    else
    {
        result.score = scorePtr[0];
    }
    // landmarks: float[1,468,3]
    auto* lmk_ptr = outputTensors[1].GetTensorMutableData<float>();
    if (lmk_ptr == nullptr)
    {
        common::logError("MediaPipeFaceLandmarks: Landmarks tensor is null.");
        return result; // Return empty result if landmarks are not available
    }
    result.landmarks.resize(468, std::vector<float>(3, 0.0f));

    for (int i = 0; i < 468; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            result.landmarks[i][j] = lmkPtr[i * 3 + j];
        }
    }
    Profiler::getInstance().stop("MediaPipeFaceLandmarks", "detect landmarks");
    return result;
}
