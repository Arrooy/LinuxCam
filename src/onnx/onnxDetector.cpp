#include "FunnyFace/onnx/onnxDetector.h"

#include "FunnyFace/math_utils.h"

using namespace funnyface;

OnnxDetector::OnnxDetector(const std::string& onnx_model_path)
    : env_(ORT_LOGGING_LEVEL_INFO, "OnnxDetector"), session_options_{}, detector_session_{nullptr}, allocator_{}
{
    // session_options_.SetInterOpNumThreads(2);  // e.g., parallel execution of independent ops
    // session_options_.SetIntraOpNumThreads(4);  // e.g., threads used inside each op
    session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    session_options_.SetLogSeverityLevel(4);
    // TODO: Add way of disabling cuda.
    // Try to add CUDA provider with better error handling
    bool cuda_available = false;
    try
    {
        // Check if CUDA libraries are available
        common::log_info("OnnxDetector: Attempting to initialize CUDA provider...");

        OrtCUDAProviderOptions cuda_options{};
        cuda_options.device_id = 0;
        cuda_options.arena_extend_strategy = 0;
        cuda_options.gpu_mem_limit = 3ULL * 1024 * 1024 * 1024; // Limit memory to 3Gb
        cuda_options.do_copy_in_default_stream = 1;

        session_options_.AppendExecutionProvider_CUDA(cuda_options);
        cuda_available = true;
        common::log_info("OnnxDetector: CUDA provider added successfully with 2GB memory limit");
    }
    catch (const Ort::Exception& e)
    {
        common::log_warn("OnnxDetector: ONNX Runtime CUDA error: %s. Falling back to CPU.", e.what());
    }
    catch (const std::exception& e)
    {
        common::log_warn("OnnxDetector: CUDA initialization failed: %s. Falling back to CPU.", e.what());
    }
    catch (...)
    {
        common::log_warn("OnnxDetector: Unknown CUDA initialization error. Falling back to CPU.");
    }

    try
    {
        // Create ONNX Runtime detector_session_ for the detector
        detector_session_ = std::make_unique<Ort::Session>(env_, onnx_model_path.c_str(), session_options_);

        if (cuda_available)
        {
            common::log_info("OnnxDetector: Model loaded successfully with CUDA acceleration");
        }
        else
        {
            common::log_info("OnnxDetector: Model loaded successfully with CPU execution");
        }
    }
    catch (const Ort::Exception& e)
    {
        common::log_error("OnnxDetector: Failed to create ONNX session: %s", e.what());
        ready_ = false;
        return;
    }

    ready_ = readModelInputSize();
}

bool OnnxDetector::readModelInputSize()
{
    Ort::TypeInfo input_type_info = detector_session_->GetInputTypeInfo(0);
    auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
    input_node_dims = tensor_info.GetShape();
    for (size_t i = 0; i < input_node_dims.size(); ++i)
    {
        if (i == 0)
        {
            batch_size_ = input_node_dims[i];
        }
        else if (i == 1)
        {
            channels_ = input_node_dims[i];
        }
        else if (i == 2)
        {
            height_ = input_node_dims[i];
        }
        else if (i == 3)
        {
            width_ = input_node_dims[i];
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
