#include "LinuxFace/onnx/rvm.h"

#include "LinuxFace/profiler.h"

using namespace linuxface;

/**
 * This onnx model is different, it uses its output[-1] for its next input.
 * Since we don't want to move data between CPU-GPU, lets use io bindings.
 * (The NN recycles recurrent states)
 */

RobustVideoMatting::RobustVideoMatting(const std::string& onnxModelPath) : OnnxDetector(onnxModelPath)
{
    initialize();
}

void RobustVideoMatting::initialize()
{
    rec_.clear();
    rec_cpu_data_.clear();

    // Define recurrent shapes based on input dimensions
    const std::vector<std::vector<int64_t>> recShapes = {
        {1, 1, 1, 1},
        {1, 1, 1, 1},
        {1, 1, 1, 1},
        {1, 1, 1, 1}
    };

    // initialize rec vector
    size_t recIndex = 0;
    for (const auto* const outputName : output_node_names_)
    {
        io_binding_.BindOutput(outputName, memory_info_);

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
        Ort::Value recTensor = Ort::Value::CreateTensor<float>(memory_info_, rec_cpu_data_[recIndex].data(), totalSize,
                                                               shape.data(), shape.size());
        rec_.push_back(std::move(recTensor));

        recIndex++;
    }
}

bool RobustVideoMatting::isImageCompatible(const std::unique_ptr<Image>& image) const
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

    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());

    padding_ = TensorPadding::noPadding();

    const std::vector<Ort::Value> inputTensors;
    // Get pointer to tensor data.
    auto* tensorData = inputTensor.GetTensorMutableData<float>();

    downsample_ = std::min(512.0f / std::max(image->info.width, image->info.height), 1.0f);
    // Note no resize.
    image->toTensor(tensorData, padding_, image->info.width, image->info.height, NormalizationType::MINMAX);

    return inputTensor;
}

void RobustVideoMatting::detect(const std::unique_ptr<Image>& image, std::unique_ptr<Image>& frg,
                                std::unique_ptr<Image>& matte)
{
    Profiler::getInstance().start("RVM", "Matting detection");

    // Convert from image to tensor.
    const Ort::Value inputTensor = this->transform(image);
    try
    {
        io_binding_.BindInput(input_node_names_[0], inputTensor);

        int index = 0;
        for (const auto& inputName : input_node_names_)
        {
            // only add r1i,r2i,r3i,r4i
            if (inputName[0] != 'r')
            {
                continue;
            }
            io_binding_.BindInput(inputName, rec_[index++]);
        }

        // downsampling tensor
        constexpr std::array<int64_t, 1> Shape = {1};
        const std::vector<float> tensorData(1, downsample_);

        const Ort::Value downsampleTensor = Ort::Value::CreateTensor<float>(memory_info_, &downsample_,
                                                                            1,             // data length
                                                                            Shape.data(),  // shape pointer
                                                                            Shape.size()); // shape dimension count
        io_binding_.BindInput("downsample_ratio", downsampleTensor);

        detector_session_->Run(Ort::RunOptions{nullptr}, io_binding_);

        std::vector<Ort::Value> outputValues = io_binding_.GetOutputValues();

        // Get fgr (foreground) and pha (alpha/matte) - first two outputs
        auto& fgrTensor = outputValues[0];
        auto& phaTensor = outputValues[1];

        // Process matte (pha) tensor
        const auto* phaData = phaTensor.GetTensorData<float>();
        if ((phaData != nullptr) && matte)
        {
            std::vector<int64_t> phaShape = phaTensor.GetTensorTypeAndShapeInfo().GetShape();
            const int outputHeight = static_cast<int>(phaShape[2]);
            const int outputWidth = static_cast<int>(phaShape[3]);

            // common::logInfo("pha shape: %d, %d, %d, %d", pha_shape[0], pha_shape[1], pha_shape[2], pha_shape[3]);
            // Note that pha_shape[1] is 1. (Grayscale)
            matte->fromTensor(phaData, phaShape, outputWidth, outputHeight, padding_, NormalizationType::MINMAX);
        }

        // Process foreground (fgr) tensor if needed
        const auto* fgrData = fgrTensor.GetTensorData<float>();
        if ((fgrData != nullptr) && frg)
        {
            std::vector<int64_t> fgrShape = fgrTensor.GetTensorTypeAndShapeInfo().GetShape();
            const int outputHeight = static_cast<int>(fgrShape[2]);
            const int outputWidth = static_cast<int>(fgrShape[3]);
            // Note that fgr_shape[1] is 3. (RGB)
            // common::logInfo("frg shape: %d, %d, %d, %d", fgr_shape[0], fgr_shape[1], fgr_shape[2], fgr_shape[3]);
            frg->fromTensor(fgrData, fgrShape, outputWidth, outputHeight, padding_, NormalizationType::MINMAX);
        }
        // Update recurrent states for next iteration (outputs 2-5 are r1o, r2o, r3o, r4o)
        // These become the next frame's r1i, r2i, r3i, r4i inputs
        size_t recIndex = 0;
        for (size_t i = 2; i < outputValues.size() && recIndex < rec_.size(); ++i)
        {
            auto& newRecTensor = outputValues[i];

            // CPU: Copy new recurrent state to our CPU data storage
            const auto* newRecData = newRecTensor.GetTensorData<float>();
            auto recShape = newRecTensor.GetTensorTypeAndShapeInfo().GetShape();

            size_t recSize = 1;
            for (auto dim : recShape)
            {
                recSize *= dim;
            }

            // Resize CPU storage if needed
            if (rec_cpu_data_[recIndex].size() != recSize)
            {
                rec_cpu_data_[recIndex].resize(recSize);
            }

            std::memcpy(rec_cpu_data_[recIndex].data(), newRecData, recSize * sizeof(float));

            // Recreate the tensor with updated data for next frame
            // Get actual shape from the output tensor
            std::vector<int64_t> tensorShape(recShape.begin(), recShape.end());

            rec_[recIndex] =
                Ort::Value::CreateTensor<float>(memory_info_, rec_cpu_data_[recIndex].data(),
                                                rec_cpu_data_[recIndex].size(), tensorShape.data(), tensorShape.size());

            recIndex++;
        }
    }
    catch (const Ort::Exception& e)
    {
        common::logError("RobustVideoMatting: %s", e.what());
        exit(-1);
    }

    Profiler::getInstance().stop("RVM", "Matting detection");
}
