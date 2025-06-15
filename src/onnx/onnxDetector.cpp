#include "FunnyFace/onnx/onnxDetector.h"

#include "FunnyFace/math_utils.h"

using namespace funnyface;

OnnxDetector::OnnxDetector(const std::string& onnx_model_path)
    : env_(ORT_LOGGING_LEVEL_INFO, "OnnxDetector"), session_options_{}, detector_session_{nullptr}, allocator_{}
{
    session_options_.SetIntraOpNumThreads(1);
    // session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
    session_options_.SetLogSeverityLevel(4);

    // OrtCUDAProviderOptions cuda_options{};
    // cuda_options.device_id = 0;
    // cuda_options.arena_extend_strategy = 0;
    // cuda_options.gpu_mem_limit = SIZE_MAX;
    // cuda_options.cudnn_conv_algo_search = OrtCudnnConvAlgoSearch::EXHAUSTIVE;
    // cuda_options.do_copy_in_default_stream = 1;

    // session_options_.AppendExecutionProvider_CUDA(cuda_options);

    // Create ONNX Runtime detector_session_ for the detector
    detector_session_ = std::make_unique<Ort::Session>(env_, onnx_model_path.c_str(), session_options_);

    ready_ = readModelInputSize();
}

bool OnnxDetector::readModelInputSize()
{
    Ort::TypeInfo input_type_info = detector_session_->GetInputTypeInfo(0);
    auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
    input_node_dims = tensor_info.GetShape();
    for (size_t i = 0; i < input_node_dims.size(); ++i)
    {
        if (i == 1)
        {
            batch_size_ = input_node_dims[i];
        }
        else if (i == 2)
        {
            channels_ = input_node_dims[i];
        }
        else if (i == 2)
        {
            width_ = input_node_dims[i];
        }
        else if (i == 2)
        {
            height_ = input_node_dims[i];
        }
    }
    auto names = detector_session_->GetInputNames();
    for (const auto& name : names)
    {
        input_node_names_str_.push_back(name);
        common::log_info("OnnxDetector::readModelInputSize - input name: %s", name.c_str());
    }

    common::log_info("OnnxDetector::readModelInputSize - batch_size_ = %d, channels_ = %d, width_ = %d, height_ = %d",
                     batch_size_, channels_, width_, height_);
    if (width_ == 0 || height_ == 0)
    {
        common::log_error("OnnxDetector::readModelInputSize - width_ or height_ is 0");
        return false;
    }
    int output_count = detector_session_->GetOutputCount();
    names = detector_session_->GetOutputNames();
    for (int i = 0; i < output_count; i++)
    {
        Ort::TypeInfo ouput_type_info = detector_session_->GetOutputTypeInfo(i);
        auto output_tensor_info = ouput_type_info.GetTensorTypeAndShapeInfo();
        std::vector<int64_t> output_dims = output_tensor_info.GetShape();
        output_node_names_str_.push_back(names[i]);
        common::log_info("OnnxDetector::readModelInputSize - Output name: %s", names[i].c_str());
        for (size_t i = 0; i < output_dims.size(); ++i)
        {
            common::log_info("OnnxDetector::readModelInputSize - Detected shape: %d", output_dims[i]);
        }
    }
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
    return true;
}
