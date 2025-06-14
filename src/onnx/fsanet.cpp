#include "FunnyFace/onnx/fsanet.h"

#include "FunnyFace/math_utils.h"

using namespace funnyface;


Ort::Value FsanetDetector::transform(const std::unique_ptr<Image>& image)
{
    // if (width_ != input_width_ || height_ != input_height_)
    // {
    //     common::log_error("Input shape does not match model input shape.");
    //     common::log_info("input_width_ to %dx%d", input_width_, input_height_);
    //     common::log_info("width_ to %dx%d", width_, height_);
    // }

    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());

    // Get pointer to tensor data.
    float* tensor_data = input_tensor.GetTensorMutableData<float>();
    image->toTensor(tensor_data, pad_, input_width_, input_height_);

    return input_tensor;
}

void FsanetDetector::detect(const std::unique_ptr<Image>& image)
{
    // Convert from image to tensor.
    Ort::Value input_tensor = this->transform(image);
    try
    {
        input_node_names_.reserve(input_node_names_str_.size());
        output_node_names_.reserve(output_node_names_str_.size());
        int i = 0;
        for (const auto& name : input_node_names_str_)
        {
            input_node_names_[i++] = name.c_str();
            // common::log_info("FSANet: input_tensor: %s", name.c_str());
        }
        i = 0;
        for (const auto& name : output_node_names_str_)
        {
            output_node_names_[i++] = name.c_str();
            // common::log_info("FSANet: output_tensor: %s", name.c_str());
        }

        auto output_tensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &input_tensor,
                                                     1, output_node_names_.data(), 1);

        const float* angles_ptr = output_tensors.front().GetTensorMutableData<float>();

        // euler_angles.yaw = angles_ptr[0];
        // euler_angles.pitch = angles_ptr[1];
        // euler_angles.roll = angles_ptr[2];
        // euler_angles.flag = true;
        // common::log_info("FSANet: euler_angles.yaw = %f, euler_angles.pitch = %f, euler_angles.roll = %f",
        //                  angles_ptr[0], angles_ptr[1], angles_ptr[2]);
        image->draw_axis_inplace(angles_ptr[0], angles_ptr[1], angles_ptr[2], 50, 2);
    }
    catch (const Ort::Exception& e)
    {
        common::log_error("FSANet: %s", e.what());exit(-1);
    }
}
