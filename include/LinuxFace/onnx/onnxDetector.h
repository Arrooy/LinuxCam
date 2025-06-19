#ifndef ONNXDETECTOR_H
#define ONNXDETECTOR_H

#include <onnxruntime_cxx_api.h>

#include "LinuxFace/detectors.h"

namespace linuxface
{
class OnnxDetector
{
  public:
    const bool isReady() const { return ready_; };
  protected:
    OnnxDetector(const std::string& onnx_model_path);
    ~OnnxDetector() = default;

    virtual std::vector<Ort::Value> transform(const std::unique_ptr<Image>& image) = 0;

    int batch_size_;
    int channels_;
    int width_;
    int height_;

    bool readModelInputSize();
    std::vector<int64_t> input_node_dims;

    std::vector<std::string> input_node_names_str_;
    std::vector<std::string> output_node_names_str_;
    std::vector<const char*> input_node_names_;
    std::vector<const char*> output_node_names_;

    Ort::Env env_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> detector_session_;
    Ort::MemoryInfo memory_info_;
    Ort::IoBinding io_binding_;
    Ort::AllocatorWithDefaultOptions allocator_;

    bool ready_{false};
    bool has_cuda_{false};

  private:
    bool checkCudaAvailability();
};
} // namespace linuxface

#endif // ONNXDETECTOR_H
