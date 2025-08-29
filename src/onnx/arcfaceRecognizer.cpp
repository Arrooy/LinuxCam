#include "LinuxFace/onnx/arcfaceRecognizer.h"

#include <cmath>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

ArcfaceRecognizer::ArcfaceRecognizer(const std::string& onnxModelPath) : OnnxDetector(onnxModelPath)
{
}

std::unique_ptr<Image>
ArcfaceRecognizer::preprocess(const Image& inputImg, const std::vector<math_utils::Point<>>& faceLandmark5)
{
    const int targetSize = 112;
    auto [aligned, affine] =
        image_utils::affineFaceTransform(inputImg, faceLandmark5, image_utils::TEMPLATE_112, targetSize);
    if (aligned)
    {
        return std::move(aligned);
    }
    common::logWarn("ArcfaceRecognizer: Failed to align face, using fallback scaling.");
    // Fallback: just scale the whole image
    return inputImg.scale(targetSize, targetSize);
}

Ort::Value ArcfaceRecognizer::transform(const std::unique_ptr<Image>& imgRs)
{
    input_node_dims[0] = 1;
    input_node_dims[1] = imgRs->isColorImage() ? 3 : 1;
    input_node_dims[2] = imgRs->info.height;
    input_node_dims[3] = imgRs->info.width;
    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    auto* tensorData = inputTensor.GetTensorMutableData<float>();
    // Use MINMAX normalization as a common default for ArcFace
    TensorPadding padding = TensorPadding::noPadding();
    imgRs->toTensor(tensorData, padding, imgRs->info.width, imgRs->info.height, NormalizationType::MINMAX);
    return inputTensor;
}

bool ArcfaceRecognizer::recognize(const Image& inputImg, const std::vector<math_utils::Point<>>& faceLandmark5,
                                  std::vector<float>& embedding)
{
    Profiler::getInstance().start("ArcfaceRecognizer", "recognize");
    if (!ready_)
    {
        return false;
    }
    const std::unique_ptr<Image> cropImage = preprocess(inputImg, faceLandmark5);
    const Ort::Value inputTensor = transform(cropImage);
    const Ort::RunOptions runOptions;
    auto outputTensors = detector_session_->Run(runOptions, input_node_names_.data(), &inputTensor, 1,
                                                output_node_names_.data(), output_node_names_str_.size());
    auto* pdata = outputTensors[0].GetTensorMutableData<float>();
    embedding.clear();
    embedding.assign(pdata, pdata + 512);
    float norm = 0.0f;
    for (const auto& val : embedding)
    {
        norm += val * val;
    }
    norm = std::sqrt(norm);
    for (auto& val : embedding)
    {
        val /= norm;
    }
    Profiler::getInstance().stop("ArcfaceRecognizer", "recognize");
    return true;
}
