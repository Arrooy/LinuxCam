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
    explicit ArcfaceRecognizer(const std::string& onnxModelPath);
    ~ArcfaceRecognizer() = default;

    // Extracts a normalized embedding from an image and 5-point landmarks
    // Set inswapperCompatible=true to get embeddings transformed for inswapper model
    bool recognize(const Image& inputImg, const std::vector<math_utils::Point<>>& faceLandmark5,
                   std::vector<float>& embedding, bool inswapperCompatible = false);

    // Enable inswapper compatibility mode by loading emap matrix from inswapper model
    bool enableInswapperCompatibility(const std::string& inswapperModelPath);

    // Check if inswapper compatibility is enabled
    bool isInswapperCompatibilityEnabled() const { return inswapper_compatible_mode_; }

  private:
    // Preprocess input image using 5-point landmarks
    std::unique_ptr<Image> preprocess(const Image& inputImg, const std::vector<math_utils::Point<>>& faceLandmark5);
    Ort::Value transform(const std::unique_ptr<Image>& imgRs) override;

    // Transform ArcFace embedding to inswapper-compatible space
    std::vector<float> transformEmbeddingForInswapper(const std::vector<float>& arcfaceEmbedding);

    // Load emap matrix from inswapper ONNX model
    bool loadEmapMatrixFromOnnx(const std::string& inswapperModelPath);

    // Inswapper compatibility members
    std::vector<float> emap_matrix_;
    bool inswapper_compatible_mode_;
    static constexpr int EmbeddingSize = 512;
};

} // namespace linuxface

#endif // ARCFACE_RECOGNIZER_H
