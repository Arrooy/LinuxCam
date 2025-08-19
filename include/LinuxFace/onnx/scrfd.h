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

    explicit SCRFDetector(const std::string& onnx_model_path);
    ~SCRFDetector() = default;
    Ort::Value transform(const std::unique_ptr<Image>& image) override;
    std::vector<Face> detect(const std::unique_ptr<Image>& image);

  private:
    void generate_points();
    std::unordered_map<int, std::vector<math_utils::StridePoint>> center_points_;

    void generate_bboxes_kps_single_stride(Ort::Value& score_pred, Ort::Value& bbox_pred, Ort::Value& kps_pred,
                                           unsigned int stride, float score_threshold, float img_width,
                                           float img_height, std::vector<Face>& faces);

    static constexpr const unsigned int num_anchors_ = 2;
    const std::vector<int> feat_stride_fpn_;

    // True if the model is using keypoints.
    bool using_kps_{false};

    // Configuration constants
    static constexpr const float nms_threshold_ = 0.45f;
    static constexpr const float score_threshold = 0.1f;  // Lowered from 0.3f to 0.1f for better detection
    static constexpr const unsigned int max_faces_per_stride = 1000;
    static constexpr const unsigned int max_number_of_faces_ = 3000;

    // NMS implementation - works in-place
    void applyNMS(std::vector<Face>& faces) const;
};
} // namespace linuxface
#endif
