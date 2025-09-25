#pragma once
#include <memory>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/onnx/arcfaceRecognizer.h"
#include "LinuxFace/onnx/inswapper.h"
#include "LinuxFace/onnx/scrfd.h"

namespace linuxface
{

class SwapPipeline
{
  public:
    SwapPipeline(std::shared_ptr<InSwapper> inswapper, std::shared_ptr<ArcfaceRecognizer> arcface,
                 std::shared_ptr<SCRFDetector> scrfd);

    // Call once per frame. Returns true if swap was performed.
    bool run(std::unique_ptr<Image>& image, std::unique_ptr<Image>& targetImg, std::vector<Face> srcFaces = {});

  private:
    bool prepareTargetEmbedding(const std::unique_ptr<Image>& targetImg);
    bool processFace(const Face& face, std::unique_ptr<Image>& image, Image& swappedFace);

    std::shared_ptr<InSwapper> inswapper_;
    std::shared_ptr<ArcfaceRecognizer> arcface_;
    std::shared_ptr<SCRFDetector> scrfd_;

    std::vector<float> target_img_embedding_;
    std::vector<math_utils::Point<>> target_img_landmarks_;
    bool target_img_embedding_ready_ = false;
    bool debug_{true};
    std::unique_ptr<Image> debug_target_image_;
    std::unique_ptr<Image> debug_target_image_aligned_;
    std::unique_ptr<Image> crop_mask_prototype_;
};

} // namespace linuxface
