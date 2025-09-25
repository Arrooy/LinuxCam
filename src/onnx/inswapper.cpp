#include "LinuxFace/onnx/inswapper.h"

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

InSwapper::InSwapper(const std::string& onnxModelPath) : OnnxDetector(onnxModelPath)
{
    if (input_node_dims.size() != 4)
    {
        common::logError("InSwapper only support 4D input");
        ready_ = false;
    }
}

Ort::Value InSwapper::transform(const std::unique_ptr<Image>& image)
{
    // [batch, channels, height, width]
    if (input_node_dims[2] == -1 || input_node_dims[3] == -1)
    {
    input_node_dims[0] = 1;
    input_node_dims[1] = 3;
    input_node_dims[2] = InputHeight;
    input_node_dims[3] = InputWidth;
    }
    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    padding_ = TensorPadding::noPadding();

    auto* tensorData = inputTensor.GetTensorMutableData<float>();
    image->toTensor(tensorData, padding_, InputWidth, InputHeight, NormalizationType::MINMAX);

    return inputTensor;
}

std::pair<bool, std::array<double, 6>> InSwapper::swap(const std::vector<float>& srcEmbedding, const std::vector<math_utils::Point<>>& dstLandmarks,
                     const Image& dstFace, Image& outImage)
{
    Profiler::getInstance().start("InSwapper", "Swap");
    if (!ready_)
    {
        return {false, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0}};
    }

    // Check for valid input parameters
    if (srcEmbedding.empty() || srcEmbedding.size() != 512)
    {
        common::logError(("InSwapper: Invalid embedding size. Expected 512, got " + std::to_string(srcEmbedding.size())).c_str());
        return {false, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0}};
    }

    if (dstLandmarks.size() != 5)
    {
        common::logError(("InSwapper: Invalid landmark count. Expected 5, got " + std::to_string(dstLandmarks.size())).c_str());
        return {false, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0}};
    }
    // TODO: test with similarity face transform
    const int targetSize = InputWidth;
    auto [aligned, affine] =
        image_utils::similarityFaceTransform(dstFace, dstLandmarks, image_utils::TEMPLATE_128, targetSize);

    if (!aligned)
    {
        return {false, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0}};
    }
    // 2. Prepare ONNX input tensors
    auto dstTensor = transform(aligned);
    std::vector<int64_t> embDims = {1, 512};
    Ort::Value srcTensor = Ort::Value::CreateTensor<float>(memory_info_, const_cast<float*>(srcEmbedding.data()), 512,
                                                           embDims.data(), embDims.size());
    std::vector<Ort::Value> inputTensors;
    inputTensors.push_back(std::move(dstTensor));
    inputTensors.push_back(std::move(srcTensor));
    // 3. Run ONNX inference
    const Ort::RunOptions runOptions;
    std::vector<const char*> inputNames = {"target", "source"};
    std::vector<const char*> outputNames = {"output"};
    auto outputTensors =
        detector_session_->Run(runOptions, inputNames.data(), inputTensors.data(), 2, outputNames.data(), 1);
    auto* outData = outputTensors[0].GetTensorMutableData<float>();
    // 4. Convert output tensor to Image
    outImage.resize(InputWidth * InputHeight * 3, false);
    outImage.info.width = InputWidth;
    outImage.info.height = InputHeight;
    outImage.info.format = ImageFormat::RGB;
    outImage.info.pixelSizeBytes = 3;
    const TensorPadding pad = TensorPadding::noPadding();
    outImage.fromTensor(outData, {1, 3, InputHeight, InputWidth}, InputWidth, InputHeight, pad,
                         NormalizationType::MINMAX);
    Profiler::getInstance().stop("InSwapper", "Swap");
    return {true, affine};
}
