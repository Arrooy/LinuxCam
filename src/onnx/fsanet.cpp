#include "LinuxFace/onnx/fsanet.h"

#include "LinuxFace/math_utils.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

// TODO: ALLOCATE IN GPU.
// SEEMS LIKE OTHER EXAMPLES; ARE USING ALLOCATED CPU MEMORY.
std::vector<Ort::Value> FsanetDetector::transform(const std::unique_ptr<Image>& image)
{
    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());

    // Get pointer to tensor data.
    float* tensor_data = input_tensor.GetTensorMutableData<float>();
    auto tensor_padding = TensorPadding::fsanet();
    image->toTensor(tensor_data, tensor_padding, input_width_, input_height_, NormalizationType::MINMAX);

    // Use initializer list for efficiency
    std::vector<Ort::Value> input_result;
    input_result.emplace_back(std::move(input_tensor));
    return input_result;
}

void FsanetDetector::detect(const std::unique_ptr<Image>& image, Face& face)
{
    Profiler::getInstance().start("FSANET", "Pose detection");
    // Crop image to face bounding box
    const std::unique_ptr<Image> crop_image = image->crop(face.getBoundingBox().rect);
    // Convert from image to tensor.
    Ort::Value input_tensor = std::move(this->transform(crop_image)[0]);
    try
    {
        auto output_tensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &input_tensor,
                                                     1, output_node_names_.data(), 1);

        const float* angles_ptr = output_tensors.front().GetTensorMutableData<float>();
        // TODO: We could add an average here, to smooth the pose.
        if (angles_ptr)
        {
            face.setFacePose(FacePose{angles_ptr[0], angles_ptr[1], angles_ptr[2]});
        }
    }
    catch (const Ort::Exception& e)
    {
        common::log_error("FSANet: %s", e.what());
        exit(-1);
    }

    Profiler::getInstance().stop("FSANET", "Pose detection");
}
