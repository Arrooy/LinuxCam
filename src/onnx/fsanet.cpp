#include "LinuxFace/onnx/fsanet.h"

#include "LinuxFace/math_utils.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

Ort::Value FsanetDetector::transform(const std::unique_ptr<Image>& image)
{
    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());

    // Get pointer to tensor data.
    auto* tensorData = inputTensor.GetTensorMutableData<float>();
    auto tensorPadding = TensorPadding::fsanet();
    image->toTensor(tensorData, tensorPadding, InputWidth, InputHeight, NormalizationType::MINMAX);

    return inputTensor;
}

void FsanetDetector::detect(const std::unique_ptr<Image>& image, Face& face)
{
    Profiler::getInstance().start("FSANET", "Pose detection");
    // Crop image to face bounding box
    const std::unique_ptr<Image> cropImage = image->crop(face.getBoundingBox().rect);
    // Convert from image to tensor.
    const Ort::Value inputTensor = this->transform(cropImage);
    try
    {
        auto outputTensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &inputTensor, 1,
                                                    output_node_names_.data(), 1);

    const float* anglesPtr = outputTensors.front().GetTensorMutableData<float>();
        // TODO(arroyo): We could add an average here, to smooth the pose.
        if (anglesPtr != nullptr)
        {
            face.setFacePose(FacePose{anglesPtr[0], anglesPtr[1], anglesPtr[2]});
        }
    }
    catch (const Ort::Exception& e)
    {
        common::logError("FSANet: %s", e.what());
        exit(-1);
    }

    Profiler::getInstance().stop("FSANET", "Pose detection");
}
