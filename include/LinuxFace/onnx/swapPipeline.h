#pragma once
#include <algorithm>
#include <memory>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/onnx/arcfaceRecognizer.h"
#include "LinuxFace/onnx/inswapper.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/onnx/faceSegmentation.h"

namespace linuxface
{

class SwapPipeline
{
  public:
    SwapPipeline(std::shared_ptr<InSwapper> inswapper, std::shared_ptr<ArcfaceRecognizer> arcface,
                 std::shared_ptr<SCRFDetector> scrfd, std::shared_ptr<FaceSegmentationDetector> faceSeg = nullptr);

    // Call once per frame. Returns true if swap was performed.
    bool run(std::unique_ptr<Image>& image, std::unique_ptr<Image>& targetImg, std::vector<Face> srcFaces = {});

    // Update target face embedding (can be called to change target dynamically)
    bool prepareTargetEmbedding(const std::unique_ptr<Image>& targetImg);

  private:
    bool processFace(Face& face, std::unique_ptr<Image>& image, Image& swappedFace);

    std::shared_ptr<InSwapper> inswapper_;
    std::shared_ptr<ArcfaceRecognizer> arcface_;
    std::shared_ptr<SCRFDetector> scrfd_;
    std::shared_ptr<FaceSegmentationDetector> faceSeg_;

    std::vector<float> target_img_embedding_;
    std::vector<math_utils::Point<>> target_img_landmarks_;
    bool target_img_embedding_ready_ = false;
    std::unique_ptr<Image> crop_mask_prototype_;

    // Debugging
    bool debug_{false};
    std::unique_ptr<Image> debug_target_image_;
    std::unique_ptr<Image> debug_target_image_aligned_;
};

} // namespace linuxface
