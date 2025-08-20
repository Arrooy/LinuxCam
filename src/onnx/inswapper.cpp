#include "LinuxFace/onnx/inswapper.h"

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

InSwapper::InSwapper(const std::string& onnxModelPath) : OnnxDetector(onnxModelPath)
{
    // You can add model-specific checks or logging here if needed
    ready = true;
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
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(allocator, input_node_dims.data(), input_node_dims.size());
    padding_ = TensorPadding::noPadding();

    auto* tensorData = inputTensor.GetTensorMutableData<float>();
    image->toTensor(tensorData, padding_, InputWidth, InputHeight, NormalizationType::MINMAX);

    return inputTensor;
}

bool InSwapper::swap(const std::vector<float>& srcEmbedding, const std::vector<math_utils::Point<>>& dstLandmarks,
                     const Image& dstFace, Image& outImage)
{
    Profiler::getInstance().start("InSwapper", "Swap");
    if (!ready)
    {
        return false;
    }

    const int targetSize = InputWidth;
    auto [aligned, affine] = image_utils::affineFaceTransform(dstFace, dstLandmarks, image_utils::template, targetSize);
    if (!aligned)
    {
        return false;
    }
    // 2. Prepare ONNX input tensors
    auto dstTensor = transform(aligned);
    std::vector<int64_t> embDims = {1, 512};
    // TODO(arroyo): try using allocator instead.
    Ort::Value srcTensor = Ort::Value::CreateTensor<float>(memory_info, const_cast<float*>(srcEmbedding.data()), 512,
                                                           embDims.data(), embDims.size());
    std::vector<Ort::Value> inputTensors;
    inputTensors.push_back(std::move(srcTensor));
    inputTensors.push_back(std::move(srcTensor));
    // 3. Run ONNX inference
    const Ort::RunOptions runOptions;
    std::vector<const char*> inputNames = {"target", "source"};
    std::vector<const char*> outputNames = {"output"};
    auto outputTensors =
        detector_session->Run(runOptions, inputNames.data(), inputTensors.data(), 2, outputNames.data(), 1);
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
    outImage.saveToDisk("swapped_face_raw.ppm");
    Profiler::getInstance().stop("InSwapper", "Swap");
    return true;
}
