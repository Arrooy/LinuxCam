#include "LinuxFace/onnx/inswapper.h"

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

InSwapper::InSwapper(const std::string& onnx_model_path) : OnnxDetector(onnx_model_path)
{
    // You can add model-specific checks or logging here if needed
    ready_ = true;
}

std::vector<Ort::Value> InSwapper::transform(const std::unique_ptr<Image>& image)
{
    // [batch, channels, height, width]
    if (input_node_dims[2] == -1 || input_node_dims[3] == -1)
    {
        input_node_dims[0] = 1;
        input_node_dims[1] = 3;
        input_node_dims[2] = input_height_;
        input_node_dims[3] = input_width_;
    }
    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    padding_ = TensorPadding::no_padding();

    float* tensor_data = input_tensor.GetTensorMutableData<float>();
    image->toTensor(tensor_data, padding_, input_width_, input_height_, NormalizationType::MINMAX);

    std::vector<Ort::Value> input_result;
    input_result.emplace_back(std::move(input_tensor));
    return input_result;
}

bool InSwapper::swap(const std::vector<float>& src_embedding, const std::vector<math_utils::Point>& dst_landmarks,
                     const Image& dst_face, Image& out_image)
{
    Profiler::getInstance().start("InSwapper", "Swap");
    if (!ready_)
    {
        return false;
    }
    static const double template_128[5][2] = {
        {0.34191607, 0.46157411},
        {0.65653393, 0.45983393},
        {0.50022500, 0.64050536},
        {0.37097589, 0.82469196},
        {0.63151696, 0.82325089}
    };
    const int target_size = input_width_;
    std::unique_ptr<Image> aligned = image_utils::align_face_affine(dst_face, dst_landmarks, template_128, target_size);
    if (!aligned)
    {
        return false;
    }
    // 2. Prepare ONNX input tensors
    auto dst_tensor = transform(aligned);
    std::vector<int64_t> emb_dims = {1, 512};
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value src_tensor = Ort::Value::CreateTensor<float>(memory_info, const_cast<float*>(src_embedding.data()), 512,
                                                            emb_dims.data(), emb_dims.size());
    std::vector<Ort::Value> input_tensors;
    input_tensors.push_back(std::move(dst_tensor[0]));
    input_tensors.push_back(std::move(src_tensor));
    // 3. Run ONNX inference
    Ort::RunOptions runOptions;
    std::vector<const char*> input_names = {"target", "source"};
    std::vector<const char*> output_names = {"output"};
    auto output_tensors =
        detector_session_->Run(runOptions, input_names.data(), input_tensors.data(), 2, output_names.data(), 1);
    float* out_data = output_tensors[0].GetTensorMutableData<float>();
    // 4. Convert output tensor to Image
    out_image.resize(input_width_ * input_height_ * 3);
    out_image.info.width = input_width_;
    out_image.info.height = input_height_;
    out_image.info.format = ImageFormat::RGB;
    out_image.info.pixelSizeBytes = 3;
    TensorPadding pad = TensorPadding::no_padding();
    out_image.fromTensor(out_data, {1, 3, input_height_, input_width_}, input_width_, input_height_, pad,
                         NormalizationType::MINMAX);
    Profiler::getInstance().stop("InSwapper", "Swap");
    return true;
}
