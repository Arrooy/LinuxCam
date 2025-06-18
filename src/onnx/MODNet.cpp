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
        input_node_dims[2] = input_height_;
        input_node_dims[3] = input_width_;
    }
    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    // TODO: Rename TensorPadding to TensorImageTransformation or whatever.
    padding_ = TensorPadding::no_padding();

    // Get pointer to tensor data.
    float* tensor_data = input_tensor.GetTensorMutableData<float>();
    image->toTensor(tensor_data, padding_, input_width_, input_height_, NormalizationType::ZERO_CENTER);

    return input_tensor;
}

void MODNetDetector::detect(const std::unique_ptr<Image>& image, std::unique_ptr<Image>& matte)
{
    Profiler::getInstance().start("MODNetDetector", "Matting detection");

    // Convert from image to tensor.
    Ort::Value input_tensor = this->transform(image);
    try
    {
        auto output_tensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &input_tensor,
                                                     1, output_node_names_.data(), 1);

        const float* mate = output_tensors.front().GetTensorMutableData<float>();

        if (mate)
        {
            matte->fromTensor(mate, input_width_, input_height_, padding_, NormalizationType::MINMAX);
        }else
        {
            common::log_error("MODNetDetector: Failed to get output tensor");
        }

    }
    catch (const Ort::Exception& e)
    {
        common::log_error("MODNetDetector: %s", e.what());
        exit(-1);
    }

    Profiler::getInstance().stop("MODNetDetector", "Matting detection");
}
