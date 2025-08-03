#ifndef ARCFACE_RECOGNIZER_H
#define ARCFACE_RECOGNIZER_H

#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/onnx/onnxDetector.h"

namespace linuxface
{

class ArcfaceRecognizer : public OnnxDetector
{
    friend class SwapPipeline; // Allow SwapPipeline access to preprocess for debug visualization
  public:
    explicit ArcfaceRecognizer(const std::string& onnx_model_path);
    ~ArcfaceRecognizer() = default;

    // Extracts a normalized embedding from an image and 5-point landmarks
    bool recognize(const Image& input_img, const std::vector<math_utils::Point<>>& face_landmark_5,
                   std::vector<float>& embedding);
  private:
    // Preprocess input image using 5-point landmarks
    std::unique_ptr<Image> preprocess(const Image& input_img, const std::vector<math_utils::Point<>>& face_landmark_5);
    Ort::Value transform(const std::unique_ptr<Image>& img_rs) override;
};

} // namespace linuxface

#endif // ARCFACE_RECOGNIZER_H
