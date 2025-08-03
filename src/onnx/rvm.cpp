#include "LinuxFace/onnx/rvm.h"

#include "LinuxFace/profiler.h"

using namespace linuxface;

/**
 * This onnx model is different, it uses its output[-1] for its next input.
 * Since we don't want to move data between CPU-GPU, lets use io bindings.
 * (The NN recycles recurrent states)
 */

RobustVideoMatting::RobustVideoMatting(const std::string& onnx_model_path) : OnnxDetector(onnx_model_path)
{
    initialize();
}

void RobustVideoMatting::initialize()
{
    rec_.clear();
    rec_cpu_data_.clear();

    // Define recurrent shapes based on input dimensions
    const std::vector<std::vector<int64_t>> rec_shapes = {
        {1, 1, 1, 1},
        {1, 1, 1, 1},
        {1, 1, 1, 1},
        {1, 1, 1, 1}
    };

    // initialize rec vector
    size_t rec_index = 0;
    for (const auto output_name : output_node_names_)
    {
        io_binding_.BindOutput(output_name, memory_info_);

        // only add r1o,r2o,r3o,r4o
        if (output_name[0] != 'r')
        {
            continue;
        }

        const auto& shape = rec_shapes[rec_index];

        size_t total_size = 1;
        for (auto dim : shape)
        {
            total_size *= dim;
        }


        if (rec_cpu_data_.size() <= rec_index)
        {
            rec_cpu_data_.resize(rec_index + 1);
        }
        rec_cpu_data_[rec_index] = std::vector<float>(total_size, 0.0f);

        // CPU path: Use traditional approach with persistent data
        Ort::Value rec_tensor = Ort::Value::CreateTensor<float>(memory_info_, rec_cpu_data_[rec_index].data(),
                                                                total_size, shape.data(), shape.size());
        rec_.push_back(std::move(rec_tensor));

        rec_index++;
    }
}

bool RobustVideoMatting::isImageCompatible(const std::unique_ptr<Image>& image)
{
    if (lastHeight_ == 0 && lastWidth_ == 0)
    {
        return true;
    }
    return image->info.height == lastHeight_ && image->info.width == lastWidth_;
}

Ort::Value RobustVideoMatting::transform(const std::unique_ptr<Image>& image)
{
    // Create tensor with fixed dimensions instead of dynamic input_node_dims with -1
    if (input_node_dims[2] == -1 || input_node_dims[3] == -1)
    {
        // [batch, channels, height, width]
        input_node_dims[0] = 1;
        input_node_dims[1] = 3;
        input_node_dims[2] = image->info.height;
        input_node_dims[3] = image->info.width;
        lastHeight_ = image->info.height;
        lastWidth_ = image->info.width;
    }

    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());

    padding_ = TensorPadding::no_padding();

    std::vector<Ort::Value> input_tensors;
    // Get pointer to tensor data.
    float* tensor_data = input_tensor.GetTensorMutableData<float>();

    downsample_ = std::min(512.0f / std::max(image->info.width, image->info.height), 1.0f);
    // Note no resize.
    image->toTensor(tensor_data, padding_, image->info.width, image->info.height, NormalizationType::MINMAX);

    return input_tensor;
}

void RobustVideoMatting::detect(const std::unique_ptr<Image>& image, std::unique_ptr<Image>& frg,
                                std::unique_ptr<Image>& matte)
{
    Profiler::getInstance().start("RVM", "Matting detection");

    // Convert from image to tensor.
    Ort::Value input_tensor = this->transform(image);
    try
    {
        io_binding_.BindInput(input_node_names_[0], input_tensor);

        int index = 0;
        for (const auto& input_name : input_node_names_)
        {
            // only add r1i,r2i,r3i,r4i
            if (input_name[0] != 'r')
            {
                continue;
            }
            io_binding_.BindInput(input_name, rec_[index++]);
        }

        // downsampling tensor
        constexpr std::array<int64_t, 1> shape = {1};
        std::vector<float> tensor_data(1, downsample_);

        Ort::Value downsample_tensor = Ort::Value::CreateTensor<float>(memory_info_, &downsample_,
                                                                       1,             // data length
                                                                       shape.data(),  // shape pointer
                                                                       shape.size()); // shape dimension count
        io_binding_.BindInput("downsample_ratio", downsample_tensor);


        detector_session_->Run(Ort::RunOptions{nullptr}, io_binding_);

        std::vector<Ort::Value> output_values = io_binding_.GetOutputValues();

        // Get fgr (foreground) and pha (alpha/matte) - first two outputs
        auto& fgr_tensor = output_values[0];
        auto& pha_tensor = output_values[1];

        // Process matte (pha) tensor
        const float* pha_data = pha_tensor.GetTensorData<float>();
        if (pha_data && matte)
        {
            std::vector<int64_t> pha_shape = pha_tensor.GetTensorTypeAndShapeInfo().GetShape();
            int output_height = static_cast<int>(pha_shape[2]);
            int output_width = static_cast<int>(pha_shape[3]);

            // common::log_info("pha shape: %d, %d, %d, %d", pha_shape[0], pha_shape[1], pha_shape[2], pha_shape[3]);
            // Note that pha_shape[1] is 1. (Grayscale)
            matte->fromTensor(pha_data, pha_shape, output_width, output_height, padding_, NormalizationType::MINMAX);
        }

        // Process foreground (fgr) tensor if needed
        const float* fgr_data = fgr_tensor.GetTensorData<float>();
        if (fgr_data && frg)
        {
            std::vector<int64_t> fgr_shape = fgr_tensor.GetTensorTypeAndShapeInfo().GetShape();
            int output_height = static_cast<int>(fgr_shape[2]);
            int output_width = static_cast<int>(fgr_shape[3]);
            // Note that fgr_shape[1] is 3. (RGB)
            // common::log_info("frg shape: %d, %d, %d, %d", fgr_shape[0], fgr_shape[1], fgr_shape[2], fgr_shape[3]);
            frg->fromTensor(fgr_data, fgr_shape, output_width, output_height, padding_, NormalizationType::MINMAX);
        }
        // Update recurrent states for next iteration (outputs 2-5 are r1o, r2o, r3o, r4o)
        // These become the next frame's r1i, r2i, r3i, r4i inputs
        size_t rec_index = 0;
        for (size_t i = 2; i < output_values.size() && rec_index < rec_.size(); ++i)
        {
            auto& new_rec_tensor = output_values[i];

            // CPU: Copy new recurrent state to our CPU data storage
            const float* new_rec_data = new_rec_tensor.GetTensorData<float>();
            auto rec_shape = new_rec_tensor.GetTensorTypeAndShapeInfo().GetShape();

            size_t rec_size = 1;
            for (auto dim : rec_shape)
            {
                rec_size *= dim;
            }

            // Resize CPU storage if needed
            if (rec_cpu_data_[rec_index].size() != rec_size)
            {
                rec_cpu_data_[rec_index].resize(rec_size);
            }

            std::memcpy(rec_cpu_data_[rec_index].data(), new_rec_data, rec_size * sizeof(float));

            // Recreate the tensor with updated data for next frame
            // Get actual shape from the output tensor
            std::vector<int64_t> tensor_shape(rec_shape.begin(), rec_shape.end());

            rec_[rec_index] = Ort::Value::CreateTensor<float>(memory_info_, rec_cpu_data_[rec_index].data(),
                                                              rec_cpu_data_[rec_index].size(), tensor_shape.data(),
                                                              tensor_shape.size());

            rec_index++;
        }
    }
    catch (const Ort::Exception& e)
    {
        common::log_error("RobustVideoMatting: %s", e.what());
        exit(-1);
    }

    Profiler::getInstance().stop("RVM", "Matting detection");
}
