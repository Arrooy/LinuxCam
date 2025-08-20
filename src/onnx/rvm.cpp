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
    size_t const rec_index = 0;
    for (const auto* const outputName : output_node_names)
    {
        io_binding.BindOutput(outputName, memory_info);

        // only add r1o,r2o,r3o,r4o
        if (outputName[0] != 'r')
        {
            continue;
        }

        const auto& shape = recShapes[recIndex];

        size_t totalSize = 1;
        for (auto dim : shape)
        {
            totalSize *= dim;
        }

        if (rec_cpu_data_.size() <= recIndex)
        {
            rec_cpu_data_.resize(recIndex + 1);
        }
        rec_cpu_data_[recIndex] = std::vector<float>(totalSize, 0.0f);

        // CPU path: Use traditional approach with persistent data
        Ort::Value recTensor = Ort::Value::CreateTensor<float>(memory_info, rec_cpu_data_[recIndex].data(), totalSize,
                                                               shape.data(), shape.size());
        rec_.push_back(std::move(recTensor));

        recIndex++;
    }
}

bool RobustVideoMatting::IsImageCompatible(const std::unique_ptr<Image>& image) const
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

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(allocator, input_node_dims.data(), input_node_dims.size());

    padding_ = TensorPadding::noPadding();

    const std::vector<Ort::Value> input_tensors;
    // Get pointer to tensor data.
    auto* tensor_data = input_tensor.GetTensorMutableData<float>();

    downsample_ = std::min(512.0f / std::max(image->info.width, image->info.height), 1.0f);
    // Note no resize.
    image->toTensor(tensorData, padding_, image->info.width, image->info.height, NormalizationType::MINMAX);

    return input_tensor;
}

void RobustVideoMatting::Detect(const std::unique_ptr<Image>& image, std::unique_ptr<Image>& frg,
                                std::unique_ptr<Image>& matte)
{
    Profiler::getInstance().start("RVM", "Matting detection");

    // Convert from image to tensor.
    const Ort::Value input_tensor = this->transform(image);
    try
    {
        io_binding.BindInput(input_node_names[0], input_tensor);

        int const index = 0;
        for (const auto& inputName : input_node_names)
        {
            // only add r1i,r2i,r3i,r4i
            if (inputName[0] != 'r')
            {
                continue;
            }
            io_binding.BindInput(inputName, rec_[index++]);
        }

        // downsampling tensor
        constexpr std::array<int64_t, 1> Shape = {1};
        const std::vector<float> tensor_data(1, downsample_);

        const Ort::Value downsample_tensor = Ort::Value::CreateTensor<float>(memory_info, &downsample_,
                                                                            1,             // data length
                                                                            Shape.data(),  // shape pointer
                                                                            Shape.size()); // shape dimension count
        io_binding.BindInput("downsample_ratio", downsample_tensor);

        detector_session->Run(Ort::RunOptions{nullptr}, io_binding);

        std::vector<Ort::Value> output_values = io_binding.GetOutputValues();

        // Get fgr (foreground) and pha (alpha/matte) - first two outputs
        auto& fgr_tensor = outputValues[0];
        auto& pha_tensor = outputValues[1];

        // Process matte (pha) tensor
        const auto* pha_data = phaTensor.GetTensorData<float>();
        if ((pha_data != nullptr) && matte)
        {
            std::vector<int64_t> pha_shape = phaTensor.GetTensorTypeAndShapeInfo().GetShape();
            const int output_height = static_cast<int>(phaShape[2]);
            const int output_width = static_cast<int>(phaShape[3]);

            // common::logInfo("pha shape: %d, %d, %d, %d", pha_shape[0], pha_shape[1], pha_shape[2], pha_shape[3]);
            // Note that pha_shape[1] is 1. (Grayscale)
            matte->fromTensor(phaData, phaShape, output_width, output_height, padding_, NormalizationType::MINMAX);
        }

        // Process foreground (fgr) tensor if needed
        const auto* fgr_data = fgrTensor.GetTensorData<float>();
        if ((fgr_data != nullptr) && frg)
        {
            std::vector<int64_t> fgr_shape = fgrTensor.GetTensorTypeAndShapeInfo().GetShape();
            const int output_height = static_cast<int>(fgrShape[2]);
            const int output_width = static_cast<int>(fgrShape[3]);
            // Note that fgr_shape[1] is 3. (RGB)
            // common::logInfo("frg shape: %d, %d, %d, %d", fgr_shape[0], fgr_shape[1], fgr_shape[2], fgr_shape[3]);
            frg->fromTensor(fgrData, fgrShape, output_width, output_height, padding_, NormalizationType::MINMAX);
        }
        // Update recurrent states for next iteration (outputs 2-5 are r1o, r2o, r3o, r4o)
        // These become the next frame's r1i, r2i, r3i, r4i inputs
        size_t rec_index = 0;
        for (size_t i = 2; i < output_values.size() && rec_index < rec_.size(); ++i)
        {
            auto& new_rec_tensor = outputValues[i];

            // CPU: Copy new recurrent state to our CPU data storage
            const auto* new_rec_data = newRecTensor.GetTensorData<float>();
            auto rec_shape = newRecTensor.GetTensorTypeAndShapeInfo().GetShape();

            size_t const rec_size = 1;
            for (auto dim : recShape)
            {
                recSize *= dim;
            }

            // Resize CPU storage if needed
            if (rec_cpu_data_[rec_index].size() != rec_size)
            {
                rec_cpu_data_[rec_index].resize(rec_size);
            }

            std::memcpy(rec_cpu_data_[rec_index].data(), newRecData, rec_size * sizeof(float));

            // Recreate the tensor with updated data for next frame
            // Get actual shape from the output tensor
            std::vector<int64_t> tensor_shape(rec_shape.begin(), recShape.end());

            rec_[rec_index] =
                Ort::Value::CreateTensor<float>(memory_info, rec_cpu_data_[rec_index].data(),
                                                rec_cpu_data_[rec_index].size(), tensorShape.data(), tensorShape.size());

            rec_index++;
        }
    }
    catch (const Ort::Exception& e)
    {
        common::logError("RobustVideoMatting: %s", e.what());
        exit(-1);
    }

    Profiler::getInstance().stop("RVM", "Matting detection");
}
