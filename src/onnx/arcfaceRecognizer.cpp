#include "LinuxFace/onnx/arcfaceRecognizer.h"

#include <cmath>

#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"
#include "LinuxFace/Image/image_utils.h"

using namespace linuxface;

ArcfaceRecognizer::ArcfaceRecognizer(const std::string& onnx_model_path) : OnnxDetector(onnx_model_path)
{
}

std::unique_ptr<Image>
ArcfaceRecognizer::preprocess(const Image& input_img, const std::vector<math_utils::Point>& face_landmark_5)
{
    static const double template_112[5][2] = {
        {0.34191607, 0.46157411},
        {0.65653393, 0.45983393},
        {0.50022500, 0.64050536},
        {0.37097589, 0.82469196},
        {0.63151696, 0.82325089}
    };
    const int target_size = 112;
    auto aligned = image_utils::align_face_affine(input_img, face_landmark_5, template_112, target_size);
    if (aligned)
    {
        return aligned;
    }
    // Fallback: just scale the whole image
    return input_img.scale(target_size, target_size);
}

Ort::Value ArcfaceRecognizer::transform(const Image& img_rs)
{
    input_node_dims[0] = 1;
    input_node_dims[1] = img_rs.isColorImage() ? 3 : 1;
    input_node_dims[2] = img_rs.info.height;
    input_node_dims[3] = img_rs.info.width;
    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    float* tensor_data = input_tensor.GetTensorMutableData<float>();
    // Use MINMAX normalization as a common default for ArcFace
    TensorPadding padding = TensorPadding::no_padding();
    img_rs.toTensor(tensor_data, padding, img_rs.info.width, img_rs.info.height, NormalizationType::MINMAX);
    return input_tensor;
}

bool ArcfaceRecognizer::recognize(const Image& input_img, const std::vector<math_utils::Point>& face_landmark_5,
                                  std::vector<float>& embedding)
{
    Profiler::getInstance().start("ArcfaceRecognizer", "recognize");
    if (!ready_)
    {
        return false;
    }
    std::unique_ptr<Image> crop_image = preprocess(input_img, face_landmark_5);
    Ort::Value input_tensor = transform(*crop_image);
    Ort::RunOptions runOptions;
    auto output_tensors = detector_session_->Run(runOptions, input_node_names_.data(), &input_tensor, 1,
                                                 output_node_names_.data(), output_node_names_str_.size());
    float* pdata = output_tensors[0].GetTensorMutableData<float>();

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
