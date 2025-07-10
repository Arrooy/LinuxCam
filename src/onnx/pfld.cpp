#include "LinuxFace/onnx/pfld.h"

#include "LinuxFace/common.h"

using namespace linuxface;

PFLDDetector::PFLDDetector(const std::string& onnx_model_path) : OnnxDetector(onnx_model_path)
{
    // Model should have 1 output: (1, 212) for 106 landmarks (x, y)
    if (output_node_names_str_.empty() || input_node_dims.size() < 4)
    {
        ready_ = false;
        common::log_error("PFLDDetector: Invalid model or input dims");
    }
}

Ort::Value PFLDDetector::transform(const std::unique_ptr<Image>& image)
{
    int target_height = static_cast<int>(input_node_dims.at(2));
    int target_width = static_cast<int>(input_node_dims.at(3));
    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    float* tensor_data = input_tensor.GetTensorMutableData<float>();
    TensorPadding padding = TensorPadding::no_padding();
    image->toTensor(tensor_data, padding, target_width, target_height, NormalizationType::MINMAX);
    return input_tensor;
}
// TODO: TEST THIS.
void PFLDDetector::detect(const std::unique_ptr<Image>& image, Face& face)
{
    // Crop the face region from the input image
    auto face_crop = image->crop(face.getBoundingBox().rect);
    if (!face_crop) return;
    Ort::Value input_tensor = this->transform(face_crop);
    auto output_tensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &input_tensor, 1,
                                                 output_node_names_.data(), output_node_names_str_.size());
    if (output_tensors.empty()) return;
    Ort::Value& landmarks_tensor = output_tensors.at(0); // (1, 212)
    const float* data = landmarks_tensor.GetTensorData<float>();
    int num_landmarks = 106;
    face.loadNewFaceLandmarks({});
    float w = static_cast<float>(face_crop->info.width);
    float h = static_cast<float>(face_crop->info.height);
    std::vector<FaceLandmark> pfld_landmarks;
    pfld_landmarks.reserve(num_landmarks);
    for (unsigned int i = 0; i < num_landmarks; ++i)
    {
        float x = std::min(std::max(0.f, data[2 * i]), 1.0f);
        float y = std::min(std::max(0.f, data[2 * i + 1]), 1.0f);
        pfld_landmarks.push_back(FaceLandmark{i, math_utils::Point(x * w, y * h)});
    }
    face.loadNewFaceLandmarks(std::move(pfld_landmarks));
}
