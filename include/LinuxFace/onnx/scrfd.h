#ifndef SCRFD_H
#define SCRFD_H

#include "LinuxFace/detectors.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/onnx/onnxDetector.h"

/**
 * Using CPU -> 40ms
 * Using Cuda + CPUMemory -> 10ms
 */

namespace linuxface
{
class SCRFDetector : public OnnxDetector
{
  public:
    enum LandmarkIndex
    {
        LEYE = 36,
        REYE = 45,
        NOSE = 33,
        LMOUTH = 48, // left mouth corner
        RMOUTH = 54, // right mouth corner
    };

    explicit SCRFDetector(const std::string& onnxModelPath);
    ~SCRFDetector() = default;
    Ort::Value transform(const std::unique_ptr<Image>& image) override;
    std::vector<Face> detect(const std::unique_ptr<Image>& image);

  private:
    void generatePoints();
    std::unordered_map<int, std::vector<math_utils::StridePoint>> center_points_;

    void
    generateBboxesKpsSingleStride(Ort::Value& scorePred, Ort::Value& bboxPred, Ort::Value& kpsPred, unsigned int stride,
                                  float scoreThreshold, float imgWidth, float imgHeight, std::vector<Face>& faces);

    static constexpr const unsigned int NumAnchors = 2;
    const std::vector<int> feat_stride_fpn_;

    // True if the model is using keypoints.
    bool using_kps_{false};

    // Configuration constants
    static constexpr const float NmsThreshold = 0.45f;
    static constexpr const float ScoreThreshold = 0.1f;
    static constexpr const unsigned int MaxFacesPerStride = 1000;
    static constexpr const unsigned int MaxNumberOfFaces = 3000;

    // NMS implementation - works in-place
    void applyNMS(std::vector<Face>& faces) const;
};
} // namespace linuxface
#endif
