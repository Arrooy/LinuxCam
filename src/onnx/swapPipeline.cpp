#include "LinuxFace/onnx/swapPipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/profiler.h"

namespace linuxface
{

SwapPipeline::SwapPipeline(std::shared_ptr<InSwapper> inswapper, std::shared_ptr<ArcfaceRecognizer> arcface,
                           std::shared_ptr<SCRFDetector> scrfd, std::shared_ptr<FaceSegmentationDetector> faceSeg)
    : inswapper_(std::move(inswapper))
    , arcface_(std::move(arcface))
    , scrfd_(std::move(scrfd))
    , faceSeg_(std::move(faceSeg))
{
    // Enable inswapper compatibility for better face swapping quality
    if (arcface_ && !arcface_->enableInswapperCompatibility(inswapper_->getModelPath()))
    {
        linuxface::common::logWarn("Failed to enable inswapper compatibility - face swapping quality may be reduced");
    }
}

bool SwapPipeline::run(std::unique_ptr<Image>& image, std::unique_ptr<Image>& targetImg, std::vector<Face> srcFaces)
{
    if (!inswapper_ || !inswapper_->isReady() || !targetImg || !arcface_ || !arcface_->isReady() || !scrfd_ || !image)
    {
        return false;
    }
    Profiler::getInstance().start("SwapPipeline", "run");

    if (!target_img_embedding_ready_ && !prepareTargetEmbedding(targetImg))
    {
        return false;
    }

    if (srcFaces.empty())
    {
        // Detect faces in the webcam image if none were provided
        srcFaces = scrfd_->detect(image);
        if (srcFaces.empty())
        {
            common::logWarn("SwapPipeline: No faces detected in the webcam image.");
            Profiler::getInstance().stop("SwapPipeline", "run");
            return false;
        }

        if (faceSeg_ && faceSeg_->isReady())
        {
            for (auto& face : srcFaces)
            {
                // Perform face segmentation on the first detected face for better mask creation
                faceSeg_->detect(image, face);
            }
        }
    }

    Image swappedFace; // Reuse buffer for all faces

    for (const auto& face : srcFaces)
    {
        if (!processFace(face, image, swappedFace))
        {
            // processFace logs errors itself
            Profiler::getInstance().stop("SwapPipeline", "run");
            return false;
        }
    }
    // Stop the overall run profiler after processing all faces
    Profiler::getInstance().stop("SwapPipeline", "run");
    return true;
}

bool SwapPipeline::prepareTargetEmbedding(const std::unique_ptr<Image>& targetImg)
{
    Profiler::getInstance().start("SwapPipeline", "get target embedding");
    const std::vector<Face> targetFaces = scrfd_->detect(targetImg);
    if (targetFaces.empty())
    {
        common::logError("SwapPipeline: No faces found in target image.");
        Profiler::getInstance().stop("SwapPipeline", "get target embedding");
        return false;
    }
    else if (targetFaces.size() > 1)
    {
        common::logWarn("SwapPipeline: Multiple faces detected in target image. Using the first detected face.");
    }

    target_img_landmarks_ = targetFaces[0].getFivePointLandmarksArcFaceOrder2D();
    if (target_img_landmarks_.size() != 5)
    {
        common::logError("SwapPipeline: Target face does not contain 5 landmarks.");
        Profiler::getInstance().stop("SwapPipeline", "get target embedding");
        return false;
    }

    // Generate inswapper-compatible embedding
    target_img_embedding_ready_ = arcface_->recognize(*targetImg, target_img_landmarks_, target_img_embedding_, true);
    target_img_embedding_ready_ &= (target_img_embedding_.size() == 512);

    if (!target_img_embedding_ready_)
    {
        common::logError("Target image embedding is not ready or landmarks are not valid.");
        Profiler::getInstance().stop("SwapPipeline", "get target embedding");
        return false;
    }

    if (debug_)
    {
        debug_target_image_ = std::move(targetImg->deepCopy());
        targetFaces[0].paintBoundingBox(debug_target_image_, Pixel(255, 0, 0));
        targetFaces[0].paintAllFaceLandmarks(debug_target_image_, false, Pixel(255, 0, 0), 5.0f);
        debug_target_image_aligned_ = arcface_->preprocess(*targetImg, target_img_landmarks_);
        debug_target_image_->scaleInPlace(0.3, ScalingAlgorithm::AREA_AVERAGING);
    }

    Profiler::getInstance().stop("SwapPipeline", "get target embedding");
    return true;
}


bool SwapPipeline::processFace(const Face& face, std::unique_ptr<Image>& image, Image& swappedFace)
{
    constexpr std::size_t kLandmarkCount = 5;
    const std::vector<math_utils::Point<>>& webcamLandmarks = face.getFivePointLandmarksArcFaceOrder2D();
    if (webcamLandmarks.size() != kLandmarkCount)
    {
        common::logError("SwapPipeline: Detected face does not have 5 landmarks. It has %d landmarks.",
                         static_cast<int>(webcamLandmarks.size()));
        return false;
    }

    const auto [swapOk, affineFromSwap] = inswapper_->swap(target_img_embedding_, webcamLandmarks, *image, swappedFace);
    if (!swapOk)
    {
        common::logError("SwapPipeline: Face swap failed.");
        return false;
    }

    Face swpface = face;
    if (faceSeg_ && faceSeg_->isReady())
    {
        Profiler::getInstance().start("SwapPipeline", "Face segmentation");
        // Perform face segmentation to create a better mask for blending
        if (!faceSeg_->detect(image, swpface))
        {
            common::logWarn("SwapPipeline: Face segmentation failed, using default mask.");
        }
        Profiler::getInstance().stop("SwapPipeline", "Face segmentation");
    }

    Profiler::getInstance().start("SwapPipeline", "Affine Warp and Crop face");

    std::unique_ptr<Image> finalFace =
        swappedFace.affineWarpBilinear(affineFromSwap.data(), image->info.width, image->info.height, ImageFormat::RGBA);

    if (!finalFace)
    {
        common::logError("SwapPipeline: Affine warp failed.");
        Profiler::getInstance().stop("SwapPipeline", "Affine Warp and Crop face");
        return false;
    }
    Profiler::getInstance().stop("SwapPipeline", "Affine Warp and Crop face");

    Profiler::getInstance().start("SwapPipeline", "Crop mask creation");

    crop_mask_prototype_ = face.createFaceMask(image);

    Profiler::getInstance().stop("SwapPipeline", "Crop mask creation");

    Profiler::getInstance().start("SwapPipeline", "Smart feather mask");
    if (crop_mask_prototype_)
    {
        const auto boundingBox = face.getBoundingBox().rect;
        const float maxDimension = std::max(boundingBox.width() * 1.2f, boundingBox.height() * 1.2f);
        const float innerFeatherRadius = maxDimension * 0.01f;
        const float outerFeatherRadius = maxDimension * 0.03f;

        if (auto smartMask = image_utils::buildSmartFeatherMask(*crop_mask_prototype_, innerFeatherRadius, outerFeatherRadius))
        {
            crop_mask_prototype_ = std::move(smartMask);
        }
    }
    Profiler::getInstance().stop("SwapPipeline", "Smart feather mask");

    if (debug_)
    {
        auto output = image->deepCopy();
        output->alphaBlend(*finalFace, *crop_mask_prototype_);
        crop_mask_prototype_->convertToRGBInplace();
        crop_mask_prototype_->drawBorder(Pixel(0, 255, 0), 2);

        auto y = image->info.height;
        image->pasteAt(*crop_mask_prototype_, 0, y, true);

        image->pasteAt(*output, image->info.width, image->info.y, true);
        image->pasteAt(*debug_target_image_, image->info.width, 0, true);
        image->pasteAt(*debug_target_image_aligned_, image->info.width, 0, true);
        image->pasteAt(*finalFace, 0, image->info.height, true);
        image->pasteAt(*crop_mask_prototype_, image->info.width, image->info.height, true);
    }
    else
    {
        Profiler::getInstance().start("SwapPipeline", "Smart feather blend");
        image->alphaBlend(*finalFace, *crop_mask_prototype_);
        Profiler::getInstance().stop("SwapPipeline", "Smart feather blend");

        
        image->pasteAt(*crop_mask_prototype_, 0, image->info.height, true);
    }

    return true;
}

} // namespace linuxface


