#ifndef SCRFD_H
#define SCRFD_H

#include "FunnyFace/detectors.h"
#include "FunnyFace/math_utils.h"
#include "FunnyFace/onnx/onnxDetector.h"

namespace funnyface
{
class SCRFDetector : public OnnxDetector
{
  public:
    SCRFDetector(const std::string& onnx_model_path);
    ~SCRFDetector() = default;
    Ort::Value transform(const std::unique_ptr<Image>& image) override;
    std::vector<Face> detect(const std::unique_ptr<Image>& image);

  private:
    void generate_points();
    std::unordered_map<int, std::vector<math_utils::StridePoint>> center_points_;

    std::vector<Face>
    generate_bboxes_kps_single_stride(Ort::Value& score_pred, Ort::Value& bbox_pred, Ort::Value& kps_pred,
                                      unsigned int stride, float score_threshold, float img_width, float img_height,
                                      std::vector<Face>& faces);

    static constexpr const unsigned int num_anchors_ = 2;
    const std::vector<int> feat_stride_fpn_;
    ;

    // True if the model is using keypoints.
    bool using_kps_{false};
    // Score threshold for the face detection.
    static constexpr const float score_threshold = 0.3f;
    // Threshold for the intersection over union.
    static constexpr const float iou_threshold = 0.45f;
    // Only process the top K boxes with the highest scores.
    static constexpr const unsigned int topk = 400;
};
} // namespace funnyface
#endif
