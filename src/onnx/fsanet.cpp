#include "FunnyFace/onnx/fsanet.h"

#include "FunnyFace/math_utils.h"
#include "FunnyFace/profiler.h"

using namespace funnyface;


Ort::Value FsanetDetector::transform(const std::unique_ptr<Image>& image)
{
    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());

    // Get pointer to tensor data.
    float* tensor_data = input_tensor.GetTensorMutableData<float>();
    image->toTensor(tensor_data, TensorPadding::fsanet(), input_width_, input_height_);

    return input_tensor;
}

void FsanetDetector::detect(const std::unique_ptr<Image>& image, Face& face)
{
    Profiler::getInstance().start("FSANET", "Pose detection");
    // Crop image to face bounding box
    const std::unique_ptr<Image> crop_image = image->crop(face.getBoundingBox().rect);
    // Convert from image to tensor.
    Ort::Value input_tensor = this->transform(crop_image);
    try
    {
        auto output_tensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &input_tensor,
                                                     1, output_node_names_.data(), 1);

        const float* angles_ptr = output_tensors.front().GetTensorMutableData<float>();
        // TODO: We could add an average here, to smooth the pose.
        face.setFacePose(FacePose{angles_ptr[0], angles_ptr[1], angles_ptr[2]});
    }
    catch (const Ort::Exception& e)
    {
        common::log_error("FSANet: %s", e.what());
        exit(-1);
    }

    Profiler::getInstance().stop("FSANET", "Pose detection");
}
