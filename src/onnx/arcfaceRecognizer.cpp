#include "LinuxFace/onnx/arcfaceRecognizer.h"

#include <cmath>

#include "LinuxFace/common.h"

using namespace linuxface;

ArcfaceRecognizer::ArcfaceRecognizer(const std::string& onnx_model_path) : OnnxDetector(onnx_model_path)
{
}

std::unique_ptr<Image>
ArcfaceRecognizer::preprocess(const Image& input_img, const std::vector<math_utils::Point>& face_landmark_5)
{
    const int target_size = 112;
    if (face_landmark_5.size() == 5)
    {
        // ArcFace 112x112 normalized landmark positions
        static const float template_112[5][2] = {
            {0.34191607f, 0.46157411f},
            {0.65653393f, 0.45983393f},
            {0.50022500f, 0.64050536f},
            {0.37097589f, 0.82469196f},
            {0.63151696f, 0.82325089f}
        };
        std::vector<math_utils::Point> template_points;
        for (int i = 0; i < 5; ++i)
        {
            template_points.emplace_back(static_cast<long>(template_112[i][0] * target_size),
                                         static_cast<long>(template_112[i][1] * target_size));
        }
        // Estimate affine transform from face_landmark_5 to template_points
        // Use least squares for 2x3 affine matrix: A * src = dst
        // src: 5x2, dst: 5x2
        float src[10], dst[10];
        for (int i = 0; i < 5; ++i)
        {
            src[2 * i] = static_cast<float>(face_landmark_5[i].x);
            src[2 * i + 1] = static_cast<float>(face_landmark_5[i].y);
            dst[2 * i] = static_cast<float>(template_points[i].x);
            dst[2 * i + 1] = static_cast<float>(template_points[i].y);
        }
        // Solve for affine: dst = M * src
        // We'll use a simple least squares fit for 2x3 affine
        float M[6] = {0};
        math_utils::estimate_affine_2d(src, dst, 5, M);
        return input_img.affineWarp(M, target_size, target_size);
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
    return true;
}
