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

MediaPipeFaceLandmarks::Result MediaPipeFaceLandmarks::detectAligned(const std::unique_ptr<Image>& image)
{
    Result result;
    if (!ready_ || !image)
    {
        return result;
    }
    Profiler::getInstance().start("MediaPipeFaceLandmarks", "detect landmarks");

    const Ort::Value inputTensor = transform(image);

    std::vector<const char*> inputNames = {"image"};
    std::vector<const char*> outputNames = {"landmarks", "scores"};

    auto outputTensors =
        detector_session_->Run(Ort::RunOptions{nullptr}, inputNames.data(), &inputTensor, 1, outputNames.data(), 2);

    std::unordered_map<std::string, Ort::Value> outputs;
    for (size_t i = 0; i < outputNames.size(); ++i)
    {
        outputs[outputNames[i]] = std::move(outputTensors[i]);
    }

    // Robust extraction for each output
    if (outputs.count("scores") && outputs["scores"].IsTensor())
    {
        if (!extract(std::move(outputs["scores"]), result.score))
        {
            common::logError("MediaPipeFaceLandmarks: Failed to extract score.");
            return result; // Return empty result if score extraction fails
        }
    }
    else
    {
        common::logError("MediaPipeFaceLandmarks: 'scores' output missing or not a tensor.");
        return result;
    }

    if (outputs.count("tracking") && outputs["tracking"].IsTensor())
    {
        if (!extract(std::move(outputs["tracking"]), result.tracking))
        {
            common::logError("MediaPipeFaceLandmarks: Failed to extract tracking.");
        }
    }
    else
    {
        // common::logError("MediaPipeFaceLandmarks: 'tracking' output missing or not a tensor.");
    }

    if (outputs.count("landmarks") && outputs["landmarks"].IsTensor())
    {
        // landmarks: float[batch_size, num_landmarks, 3]
        extractLandmarks(std::move(outputs["landmarks"]), result.landmarks);
    }
    else
    {
        common::logError("MediaPipeFaceLandmarks: 'landmarks' output missing or not a tensor.");
    }
    Profiler::getInstance().stop("MediaPipeFaceLandmarks", "detect landmarks");
    return result;
}

bool MediaPipeFaceLandmarks::extractLandmarks(Ort::Value landmarksTensor, std::vector<std::vector<float>>& landmarks)
{
    // landmarks: float[batch_size, num_landmarks, 3]
    auto* lmkPtr = landmarksTensor.GetTensorMutableData<float>();
    if (lmkPtr == nullptr)
    {
        common::logError("MediaPipeFaceLandmarks: Landmarks tensor is null.");
        return false; // Return empty result if landmarks are not available
    }

    // Get the actual shape of the landmarks tensor
    auto landmarksShape = landmarksTensor.GetTensorTypeAndShapeInfo().GetShape();
    if (landmarksShape.size() != 3 || landmarksShape[2] != 3)
    {
        common::logError("MediaPipeFaceLandmarks: Unexpected landmarks tensor shape.");
        return false;
    }

    size_t numLandmarks = static_cast<size_t>(landmarksShape[1]);
    landmarks.resize(numLandmarks, std::vector<float>(3, 0.0f));

    for (size_t i = 0; i < numLandmarks; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            landmarks[i][j] = lmkPtr[i * 3 + j];
        }
    }
    return true;
}

Face MediaPipeFaceLandmarks::detect(const std::unique_ptr<Image>& image, const Face& face)
{
    if (!ready_ || !image || !face.isValid() || !face.hasLandmarks())
    {
        // return invalid face
        return face;
    }

    // Choose the appropriate template based on model dimensions
    auto templatePoints = image_utils::TEMPLATE_192_ALT;
    if (width_ == 256)
    {
        templatePoints = image_utils::TEMPLATE_256;
    }
    else if (width_ != 192)
    {
        // For other sizes, scale the 192 template
        double localTemplate[5][2];
        double scaleFactor = static_cast<double>(width_) / 192.0;
        for (int i = 0; i < 5; ++i)
        {
            localTemplate[i][0] = image_utils::TEMPLATE_192_ALT[i][0] * scaleFactor;
            localTemplate[i][1] = image_utils::TEMPLATE_192_ALT[i][1] * scaleFactor;
        }
        templatePoints = localTemplate;
        common::logWarn("Using scaled template for model size %d, consider adding a dedicated template", width_);
    }

    auto landmarks5pt = face.getFivePointLandmarksArcFaceOrder2D();
    if (landmarks5pt.size() != 5)
    {
        common::logError("Face does not have 5-point landmarks for alignment.");
        return face;
    }

    // Affine align using model's expected dimensions
    auto [aligned_image, affine] =
        image_utils::similarityFaceTransform(*image, landmarks5pt, templatePoints, width_, true);

    if (!aligned_image)
    {
        common::logError("Failed to wrap face image for MediaPipe landmarks detection");
        return face;
    }
    aligned_image->saveToDisk("aligned.ppm");
    auto result = detectAligned(aligned_image);
    if (result.score <= 0.5)
    {
        common::logWarn("MediaPipe landmarks detection score too low: %f", result.score);
        return face;
    }

    // Map landmarks back to original image
    double invM[6];
    if (!math_utils::invertAffine(affine.data(), invM))
    {
        linuxface::common::logError("Failed to invert affine for MediaPipe unalignment");
        return face;
    }

    // 256 model outputs normalized coords, 192 model outputs absolute coords
    double w = 1;
    double h = 1;
    if (width_ == 192)
    {
        w = aligned_image->info.width;
        h = aligned_image->info.height;
    }

    std::vector<std::pair<double, double>> aligned_pts;
    std::vector<float> aligned_z;
    for (const auto& landmark : result.landmarks)
    {
        aligned_pts.emplace_back(landmark[0] * w, landmark[1] * h);
        aligned_z.push_back(landmark[2]);
    }
    auto unaligned_pts = image_utils::transformPointsAffine(aligned_pts, invM);
    std::vector<FaceLandmark> final_landmarks;
    for (size_t i = 0; i < unaligned_pts.size(); ++i)
    {
        final_landmarks.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            {unaligned_pts[i].first, unaligned_pts[i].second, aligned_z[i]},
            result.score
        });
    }
    return Face(final_landmarks, face.getBoundingBox());
}
