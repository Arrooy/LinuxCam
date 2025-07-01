#include "LinuxFace/onnx/onnxDetector.h"

#include "LinuxFace/math_utils.h"
#include "config.hpp"

using namespace linuxface;

OnnxDetector::OnnxDetector(const std::string& onnx_model_path)
    : env_(ORT_LOGGING_LEVEL_FATAL, "OnnxDetector"),
      session_options_{},
      detector_session_{nullptr},
      memory_info_{nullptr},
      io_binding_(nullptr)
{
    // session_options_.SetInterOpNumThreads(2);  // e.g., parallel execution of independent ops
    // session_options_.SetIntraOpNumThreads(4);  // e.g., threads used inside each op
    session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    session_options_.SetLogSeverityLevel(ORT_LOGGING_LEVEL_FATAL);

    // Try to add CUDA provider with better error handling
    has_cuda_ = Config::getInstance().isGPUEnabled() && checkCudaAvailability();
    if (has_cuda_)
    {
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
            has_cuda_ = true;
            common::log_info("OnnxDetector: CUDA provider added successfully with 2GB memory limit");
        }
        catch (const Ort::Exception& e)
        {
            common::log_warn("OnnxDetector: ONNX Runtime CUDA error: %s. Falling back to CPU.", e.what());
            has_cuda_ = false;
        }
        catch (const std::exception& e)
        {
            common::log_warn("OnnxDetector: CUDA initialization failed: %s. Falling back to CPU.", e.what());
            has_cuda_ = false;
        }
        catch (...)
        {
            common::log_warn("OnnxDetector: Unknown CUDA initialization error. Falling back to CPU.");

            has_cuda_ = false;
        }
    }

    try
    {
        // Create ONNX Runtime detector_session_ for the detector
        detector_session_ = std::make_unique<Ort::Session>(env_, onnx_model_path.c_str(), session_options_);

        // Disable CUDA memory acceleration.
        if (false)
        {
            common::log_info("OnnxDetector: Model loaded successfully with CUDA acceleration");
            memory_info_ =
                Ort::MemoryInfo("Cuda", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemType::OrtMemTypeDefault);
        }
        else
        {
            memory_info_ =
                Ort::MemoryInfo::CreateCpu(OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
            common::log_info("OnnxDetector: Model loaded successfully with CPU execution");
        }
    }
    catch (const Ort::Exception& e)
    {
        common::log_error("OnnxDetector: Failed to create ONNX session: %s", e.what());
        ready_ = false;
        return;
    }catch (const std::exception& e)
    {
        common::log_error("OnnxDetector: Failed to create ONNX session: %s", e.what());
        ready_ = false;
        return;
    }
    catch (...)
    {
        common::log_error("OnnxDetector: Unknown error while creating ONNX session");
        ready_ = false;
        return;
    }

    io_binding_ = Ort::IoBinding(*detector_session_);

    ready_ = readModelInputSize();
}

bool OnnxDetector::checkCudaAvailability()
{
    // Check if CUDA execution provider is available through ONNX Runtime
    auto available_providers = Ort::GetAvailableProviders();

    for (const auto& provider : available_providers)
    {
        if (provider == "CUDAExecutionProvider")
        {
            common::log_info("CUDA Execution Provider available");
            return true;
        }
    }

    common::log_info("CUDA Execution Provider not available");
    common::log_info("Available providers: ");
    for (const auto& provider : available_providers)
    {
        common::log_info("\t %s", provider.c_str());
    }

    return false;
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
        // common::log_info("OnnxDetector::readModelInputSize - input name: %s", name.c_str());
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
        // common::log_info("OnnxDetector::readModelInputSize - Output name: %s", names[i].c_str());
        // for (size_t i = 0; i < output_dims.size(); ++i)
        // {
        //     common::log_info("OnnxDetector::readModelInputSize - Detected shape: %d", output_dims[i]);
        // }
    }
    input_node_names_.reserve(input_node_names_str_.size());
    output_node_names_.reserve(output_node_names_str_.size());
    for (const auto& name : input_node_names_str_)
    {
        input_node_names_.push_back(name.c_str());
        // common::log_info("FSANet: input_tensor: %s", name.c_str());
    }
    for (const auto& name : output_node_names_str_)
    {
        output_node_names_.push_back(name.c_str());
        // common::log_info("FSANet: output_tensor: %s", name.c_str());
    }
    return true;
}
