#ifndef ONNXDETECTOR_H
#define ONNXDETECTOR_H

#include <onnxruntime_cxx_api.h>

#include "LinuxFace/detectors.h"

namespace linuxface
{
class OnnxDetector
{
  public:
    bool isReady() const { return ready_; };
    std::string getModelPath() const { return onnx_model_path_; }

  protected:
    explicit OnnxDetector(const std::string& onnxModelPath);
    ~OnnxDetector() = default;

    virtual Ort::Value transform(const std::unique_ptr<Image>& image) = 0;

    int batch_size_{};
    int channels_{};
    int width_{};
    int height_{};

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

    std::string onnx_model_path_;
    bool ready_{false};
    bool has_cuda_{false};

  private:
    static bool checkCudaAvailability();
};
} // namespace linuxface

#endif // ONNXDETECTOR_H
