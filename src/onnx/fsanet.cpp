#include "LinuxFace/onnx/fsanet.h"

#include "LinuxFace/math_utils.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

Ort::Value FsanetDetector::transform(const std::unique_ptr<Image>& image)
{
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(allocator, input_node_dims.data(), input_node_dims.size());

    // Get pointer to tensor data.
    auto* tensor_data = input_tensor.GetTensorMutableData<float>();
    auto tensor_padding = TensorPadding::fsanet();
    image->toTensor(tensorData, tensor_padding, InputWidth, InputHeight, NormalizationType::MINMAX);

    return input_tensor;
}

void FsanetDetector::Detect(const std::unique_ptr<Image>& image, Face& face)
{
    Profiler::getInstance().start("FSANET", "Pose detection");
    // Crop image to face bounding box
    const std::unique_ptr<Image> crop_image = image->crop(face.getBoundingBox().rect);
    // Convert from image to tensor.
    const Ort::Value input_tensor = this->transform(cropImage);
    try
    {
        auto output_tensors = detector_session->Run(Ort::RunOptions{nullptr}, input_node_names.data(), &input_tensor, 1,
                                                   output_node_names.data(), 1);

        const float* angles_ptr = outputTensors.front().GetTensorMutableData<float>() = nullptr;
        // TODO(arroyo): We could add an average here, to smooth the pose.
        if (angles_ptr != nullptr)
        {
            face.setFacePose(FacePose{angles_ptr[0], angles_ptr[1], angles_ptr[2]});
        }
    }
    catch (const Ort::Exception& e)
    {
        common::logError("FSANet: %s", e.what());
        exit(-1);
    }

    Profiler::getInstance().stop("FSANET", "Pose detection");
}
