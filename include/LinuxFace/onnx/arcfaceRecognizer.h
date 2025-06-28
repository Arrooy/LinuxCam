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
  public:
    explicit ArcfaceRecognizer(const std::string& onnx_model_path);
    ~ArcfaceRecognizer() = default;

    // Extracts a normalized embedding from an image and 5-point landmarks
    bool recognize(const Image& input_img, const std::vector<math_utils::Point>& face_landmark_5,
                   std::vector<float>& embedding);

  private:
    // Preprocess input image using 5-point landmarks
    std::unique_ptr<Image> preprocess(const Image& input_img, const std::vector<math_utils::Point>& face_landmark_5);


    // TODO: transform shouldnt return a vector.
    // Override to match base: always return a vector (even if only one tensor)
    std::vector<Ort::Value> transform(const std::unique_ptr<Image>& image) override
    {
        std::vector<Ort::Value> result;
        result.push_back(transform(*image)); // call the non-virtual version
        return result;
    }
    // The actual transform implementation for a single image
    Ort::Value transform(const Image& img_rs);
};

} // namespace linuxface

#endif // ARCFACE_RECOGNIZER_H
