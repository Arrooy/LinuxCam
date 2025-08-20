#ifndef ONNXDETECTOR_H
#define ONNXDETECTOR_H

#include <onnxruntime_cxx_api.h>

#include "LinuxFace/detectors.h"

namespace linuxface
{
class OnnxDetector
{
  public:
    bool isReady() const { return ready; };

  protected:
    explicit OnnxDetector(const std::string& onnxModelPath);
    ~OnnxDetector() = default;

    virtual Ort::Value transform(const std::unique_ptr<Image>& image) = 0;

    int batch_size{};
    int channels{};
    int width{};
    int height{};

    bool readModelInputSize();
    std::vector<int64_t> input_node_dims;

    std::vector<std::string> input_node_names_str;
    std::vector<std::string> output_node_names_str;
    std::vector<const char*> input_node_names;
    std::vector<const char*> output_node_names;

    Ort::Env env;
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> detector_session;
    Ort::MemoryInfo memory_info;
    Ort::IoBinding io_binding;
    Ort::AllocatorWithDefaultOptions allocator;

    bool ready{false};
    bool has_cuda{false};

  private:
    static bool checkCudaAvailability();
};
} // namespace linuxface

#endif // ONNXDETECTOR_H
