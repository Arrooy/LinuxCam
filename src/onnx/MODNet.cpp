#include "LinuxFace/onnx/MODNet.h"

#include "LinuxFace/profiler.h"

using namespace linuxface;


Ort::Value MODNetDetector::transform(const std::unique_ptr<Image>& image)
{
    // Create tensor with fixed dimensions instead of dynamic input_node_dims with -1
    if (input_node_dims[2] == -1 || input_node_dims[3] == -1)
    {
        // [batch, channels, height, width]
        input_node_dims[0] = 1;
        input_node_dims[1] = 3;
        input_node_dims[2] = InputHeight;
        input_node_dims[3] = InputWidth;
    }
    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    padding_ = TensorPadding::noPadding();

    // Get pointer to tensor data.
    float* tensorData = inputTensor.GetTensorMutableData<float>();
    image->toTensor(tensorData, padding_, InputWidth, InputHeight, NormalizationType::ZERO_CENTER);

    return inputTensor;
}

void MODNetDetector::detect(const std::unique_ptr<Image>& image, std::unique_ptr<Image>& matte)
{
    Profiler::getInstance().start("MODNetDetector", "Matting detection");

    // Convert from image to tensor.
    Ort::Value inputTensor = this->transform(image);
    try
    {
        auto outputTensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &inputTensor, 1,
                                                    output_node_names_.data(), 1);
        auto& outputTensor = outputTensors.front();
        const float* mate = outputTensor.GetTensorMutableData<float>();

        if (mate != nullptr)
        {
            std::vector<int64_t> matteShape = outputTensor.GetTensorTypeAndShapeInfo().GetShape();
            matte->fromTensor(mate, matteShape, InputWidth, InputHeight, padding_, NormalizationType::MINMAX);
        }
        else
        {
            common::logError("MODNetDetector: Failed to get output tensor");
        }
    }
    catch (const Ort::Exception& e)
    {
        common::logError("MODNetDetector: %s", e.what());
        exit(-1);
    }

    Profiler::getInstance().stop("MODNetDetector", "Matting detection");
}
