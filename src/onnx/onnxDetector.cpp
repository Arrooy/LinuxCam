#include "LinuxFace/onnx/onnxDetector.h"

#include "LinuxFace/math_utils.h"
#include "config.hpp"

using namespace linuxface;

OnnxDetector::OnnxDetector(const std::string& onnxModelPath)
    : env_(ORT_LOGGING_LEVEL_FATAL, "OnnxDetector")
    , detector_session_{nullptr}
    , memory_info_{nullptr}
    , io_binding_(nullptr)
    , onnx_model_path_(onnxModelPath)
    , has_cuda_(Config::getInstance().isGPUEnabled() && checkCudaAvailability())
{
    // session_options_.SetInterOpNumThreads(2);  // e.g., parallel execution of independent ops
    // session_options_.SetIntraOpNumThreads(4);  // e.g., threads used inside each op
    session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    session_options_.SetLogSeverityLevel(ORT_LOGGING_LEVEL_FATAL);

    // Try to add CUDA provider with better error handling

    if (has_cuda_)
    {
        try
        {
            // Check if CUDA libraries are available
            common::logInfo("OnnxDetector: Attempting to initialize CUDA provider...");
            common::logInfo("OnnxDetector: Using CUDA device ID: 0");
            common::logInfo("OnnxDetector: GPU memory: unlimited (will use all available)");

            OrtCUDAProviderOptions cudaOptions{};
            cudaOptions.device_id = 0;
            cudaOptions.arena_extend_strategy = 0;
            cudaOptions.gpu_mem_limit = SIZE_MAX; // Unlimited GPU memory
            cudaOptions.do_copy_in_default_stream = 1;
            cudaOptions.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
            session_options_.AppendExecutionProvider_CUDA(cudaOptions);
            has_cuda_ = true;
            common::logInfo("OnnxDetector: CUDA provider added successfully with unlimited GPU memory");
        }
        catch (const Ort::Exception& e)
        {
            common::logError("OnnxDetector: ONNX Runtime CUDA error: %s. Falling back to CPU.", e.what());
            common::logError("OnnxDetector: Error code: %d", e.GetOrtErrorCode());
            has_cuda_ = false;
        }
        catch (const std::exception& e)
        {
            common::logError("OnnxDetector: CUDA initialization failed: %s. Falling back to CPU.", e.what());
            has_cuda_ = false;
        }
        catch (...)
        {
            common::logError("OnnxDetector: Unknown CUDA initialization error. Falling back to CPU.");
            has_cuda_ = false;
        }
    }
    else
    {
        common::logInfo("OnnxDetector: GPU disabled or CUDA not available, using CPU");
    }

    try
    {
        common::logInfo("OnnxDetector: Loading ONNX model from %s", onnxModelPath.c_str());
        // Create ONNX Runtime detector_session_ for the detector
        detector_session_ = std::make_unique<Ort::Session>(env_, onnxModelPath.c_str(), session_options_);

        // Disable CUDA memory acceleration.
        {
            memory_info_ =
                Ort::MemoryInfo::CreateCpu(OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
            common::logInfo("OnnxDetector: Model loaded successfully with CPU Memory execution");
        }
    }
    catch (const Ort::Exception& e)
    {
        common::logError("OnnxDetector: Failed to create ONNX session: %s", e.what());
        ready_ = false;
        return;
    }
    catch (const std::exception& e)
    {
        common::logError("OnnxDetector: Failed to create ONNX session: %s", e.what());
        ready_ = false;
        return;
    }
    catch (...)
    {
        common::logError("OnnxDetector: Unknown error while creating ONNX session");
        ready_ = false;
        return;
    }

    io_binding_ = Ort::IoBinding(*detector_session_);

    ready_ = readModelInputSize();
}

bool OnnxDetector::checkCudaAvailability()
{
    auto availableProviders = Ort::GetAvailableProviders();

    for (const auto& provider : availableProviders)
    {
        if (provider == "CUDAExecutionProvider")
        {
            common::logInfo("OnnxDetector: CUDA Execution Provider is available in ONNX Runtime");
            return true;
        }
    }

    common::logWarn("OnnxDetector: CUDA Execution Provider NOT available in ONNX Runtime");
    common::logWarn("OnnxDetector: Your ONNX Runtime was built without CUDA support");
    common::logInfo("Available execution providers:");
    for (const auto& provider : availableProviders)
    {
        common::logInfo("  - %s", provider.c_str());
    }

    return false;
}


bool OnnxDetector::readModelInputSize()
{
    const Ort::TypeInfo inputTypeInfo = detector_session_->GetInputTypeInfo(0);
    auto tensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
    input_node_dims = tensorInfo.GetShape();
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
        common::logInfo("OnnxDetector::readModelInputSize - input name: %s", name.c_str());
    }

    common::logInfo("OnnxDetector::readModelInputSize - batch_size_ = %d, channels_ = %d, width_ = %d, height_ = %d",
                    batch_size_, channels_, width_, height_);
    if (width_ == 0 || height_ == 0)
    {
        common::logError("OnnxDetector::readModelInputSize - width_ or height_ is 0");
        return false;
    }
    const int outputCount = detector_session_->GetOutputCount();
    names = detector_session_->GetOutputNames();
    for (int i = 0; i < outputCount; i++)
    {
        const Ort::TypeInfo ouputTypeInfo = detector_session_->GetOutputTypeInfo(i);
        auto outputTensorInfo = ouputTypeInfo.GetTensorTypeAndShapeInfo();
        const std::vector<int64_t> outputDims = outputTensorInfo.GetShape();
        output_node_names_str_.push_back(names[i]);
        common::logInfo("OnnxDetector::readModelInputSize - Output name: %s", names[i].c_str());
        for (const long& outputDim : outputDims)
        {
            common::logInfo("OnnxDetector::readModelInputSize - Detected shape: %d", outputDim);
        }
    }
    input_node_names_.reserve(input_node_names_str_.size());
    output_node_names_.reserve(output_node_names_str_.size());
    for (const auto& name : input_node_names_str_)
    {
        input_node_names_.push_back(name.c_str());
    }
    for (const auto& name : output_node_names_str_)
    {
        output_node_names_.push_back(name.c_str());
    }
    return true;
}
