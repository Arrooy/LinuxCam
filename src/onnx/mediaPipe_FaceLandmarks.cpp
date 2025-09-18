#include "LinuxFace/onnx/mediaPipe_FaceLandmarks.h"

#include <unordered_map>
#include <vector>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"
using namespace linuxface;

MediaPipeFaceLandmarks::MediaPipeFaceLandmarks(const std::string& onnxModelPath) : OnnxDetector(onnxModelPath)
{
    // Model expects input [1,3,192,192] named "image"
    // Output: "scores" [1], "landmarks" [1,468,3]
}

Ort::Value MediaPipeFaceLandmarks::transform(const std::unique_ptr<Image>& image)
{
    // Ensure input_node_dims uses concrete dimensions (replace -1 with actual values)
    input_node_dims = {batch_size_ == -1 ? 1 : batch_size_, channels_, height_, width_};
    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    auto* tensorData = inputTensor.GetTensorMutableData<float>();

    // MediaPipe preprocessing with 25% padding around the face
    padding_ = TensorPadding::mediapipe();
    image->toTensor(tensorData, padding_, width_, height_, NormalizationType::MINMAX);
    return inputTensor;
}

MediaPipeFaceLandmarks::Result MediaPipeFaceLandmarks::detect(const std::unique_ptr<Image>& image)
{
    Result result;
    if (!ready_ || !image)
    {
        return result;
    }
    Profiler::getInstance().start("MediaPipeFaceLandmarks", "detect landmarks");
    const Ort::Value inputTensor = transform(image);
    std::vector<const char*> inputNames = {"image"};
    std::vector<const char*> outputNames = {"scores", "landmarks"};
    auto outputTensors =
        detector_session_->Run(Ort::RunOptions{nullptr}, inputNames.data(), &inputTensor, 1, outputNames.data(), 2);
    std::unordered_map<std::string, Ort::Value> outputs;
    for (size_t i = 0; i < outputNames.size(); ++i)
    {
        outputs[outputNames[i]] = std::move(outputTensors[i]);
    }
    // scores: float[1]
    auto scoreTensor = std::move(outputs["scores"]);
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
    auto* scorePtr = scoreTensor.GetTensorMutableData<float>();
    if (scorePtr == nullptr)
    {
        result.score = 0.0f; // Handle case where score is not available
    }
    else
    {
        result.score = scorePtr[0];
    }
    // landmarks: float[batch_size, num_landmarks, 3]
    auto* lmkPtr = outputs["landmarks"].GetTensorMutableData<float>();
    if (lmkPtr == nullptr)
    {
        common::logError("MediaPipeFaceLandmarks: Landmarks tensor is null.");
        return result; // Return empty result if landmarks are not available
    }

    // Get the actual shape of the landmarks tensor
    auto landmarksShape = outputs["landmarks"].GetTensorTypeAndShapeInfo().GetShape();
    if (landmarksShape.size() != 3 || landmarksShape[2] != 3)
    {
        common::logError("MediaPipeFaceLandmarks: Unexpected landmarks tensor shape.");
        return result;
    }

    size_t numLandmarks = static_cast<size_t>(landmarksShape[1]);
    result.landmarks.resize(numLandmarks, std::vector<float>(3, 0.0f));

    for (size_t i = 0; i < numLandmarks; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            result.landmarks[i][j] = lmkPtr[i * 3 + j];
        }
    }
    Profiler::getInstance().stop("MediaPipeFaceLandmarks", "detect landmarks");
    return result;
}
