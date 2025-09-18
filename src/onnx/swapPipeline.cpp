#include "LinuxFace/onnx/swapPipeline.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/profiler.h"

namespace linuxface
{

SwapPipeline::SwapPipeline(std::shared_ptr<InSwapper> inswapper, std::shared_ptr<ArcfaceRecognizer> arcface,
                           std::shared_ptr<SCRFDetector> scrfd)
    : inswapper_(std::move(inswapper)), arcface_(std::move(arcface)), scrfd_(std::move(scrfd))
{
    // Enable inswapper compatibility for better face swapping quality
    if (arcface_ && !arcface_->enableInswapperCompatibility(inswapper_->getModelPath()))
    {
        linuxface::common::logWarn("Failed to enable inswapper compatibility - face swapping quality may be reduced");
    }
}

bool SwapPipeline::run(std::unique_ptr<Image>& image, std::unique_ptr<Image>& targetImg, std::vector<Face> srcFaces)
{
    Profiler::getInstance().start("SwapPipeline", "run");
    if (!inswapper_ || !inswapper_->isReady() || !targetImg || !arcface_ || !arcface_->isReady() || !scrfd_ || !image)
    {
        return false;
    }

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
    Profiler::getInstance().start("SwapPipeline", "swap face");
    constexpr std::size_t kLandmarkCount = 5;
    const std::vector<math_utils::Point<>>& webcamLandmarks = face.getFivePointLandmarksArcFaceOrder2D();
    if (webcamLandmarks.size() != kLandmarkCount)
    {
        common::logError("SwapPipeline: Detected face does not have 5 landmarks. It has %d landmarks.",
                         static_cast<int>(webcamLandmarks.size()));
        Profiler::getInstance().stop("SwapPipeline", "swap face");
        return false;
    }

    const bool swapOk = inswapper_->swap(target_img_embedding_, webcamLandmarks, *image, swappedFace);
    Profiler::getInstance().stop("SwapPipeline", "swap face");
    if (!swapOk)
    {
        common::logError("SwapPipeline: Face swap failed.");
        return false;
    }

    Profiler::getInstance().start("SwapPipeline", "Affine Warp and Crop face");

    std::array<double, kLandmarkCount * 2> srcPoints{};
    std::array<double, kLandmarkCount * 2> dstPoints{};
    for (std::size_t i = 0; i < kLandmarkCount; ++i)
    {
        srcPoints[2 * i] = image_utils::TEMPLATE_128[i][0] * swappedFace.info.width;
        srcPoints[2 * i + 1] = image_utils::TEMPLATE_128[i][1] * swappedFace.info.height;
        dstPoints[2 * i] = webcamLandmarks[i].x;
        dstPoints[2 * i + 1] = webcamLandmarks[i].y;
    }

    std::array<double, 6> affineM;
    math_utils::estimateAffine2d(srcPoints.data(), dstPoints.data(), static_cast<int>(kLandmarkCount), affineM.data());
    std::unique_ptr<Image> warpedSwappedFace =
        swappedFace.affineWarpBilinear(affineM.data(), image->info.width, image->info.height);
    if (!warpedSwappedFace)
    {
        common::logError("SwapPipeline: Affine warp failed.");
        Profiler::getInstance().stop("SwapPipeline", "Affine Warp and Crop face");
        return false;
    }
    Profiler::getInstance().stop("SwapPipeline", "Affine Warp and Crop face");

    Profiler::getInstance().start("SwapPipeline", "Crop mask creation");
    const auto& bbox = face.getBoundingBox();
    const double outWidth = bbox.rect.width();
    const double outHeight = bbox.rect.height();
    const double minX = bbox.rect.x();
    const double minY = bbox.rect.y();
    // Use integer sizes to avoid tiny floating point differences
    const int cropW = static_cast<int>(std::round(outWidth));
    const int cropH = static_cast<int>(std::round(outHeight));

    // Prepare cropSize for mask generation when prototype needs creation
    const std::vector<double> cropSize = {static_cast<double>(cropW), static_cast<double>(cropH)};

    // Rebuild prototype only when size changes or prototype is missing
    if (!crop_mask_prototype_ || crop_mask_prototype_->info.width != cropW || crop_mask_prototype_->info.width != cropH)
    {
        crop_mask_prototype_ = image_utils::createStaticBoxMask(cropSize);
        if (!crop_mask_prototype_)
        {
            common::logError("SwapPipeline: Failed to create crop mask.");
            Profiler::getInstance().stop("SwapPipeline", "Crop mask creation");
            return false;
        }
    }

    std::unique_ptr<Image> warpedMask = std::make_unique<Image>(image->size());
    warpedMask->info = image->info;
    warpedMask->info.pixelSizeBytes = 1;
    warpedMask->info.format = ImageFormat::GRAYSCALE;
    warpedMask->black();
    warpedMask->pasteAt(*crop_mask_prototype_, minX, minY, true);
    if (!warpedMask)
    {
        common::logError("SwapPipeline: Failed to warp crop mask.");
        Profiler::getInstance().stop("SwapPipeline", "Crop mask creation");
        return false;
    }
    Profiler::getInstance().stop("SwapPipeline", "Crop mask creation");

    if (debug_)
    {
        auto output = image->deepCopy();
        output->alphaBlend(*warpedSwappedFace, *warpedMask);
        // paint all faces in debug view
        // ... existing debug painting uses scrfd result; keep previous behavior minimal here
        warpedMask->convertToRGBInplace();

        image->pasteAt(*output, image->info.width, image->info.y, true);
        image->pasteAt(*debug_target_image_, image->info.width, 0, true);
        image->pasteAt(*debug_target_image_aligned_, image->info.width, 0, true);
        image->pasteAt(*warpedSwappedFace, 0, image->info.height, true);
        image->pasteAt(*warpedMask, image->info.width, image->info.height, true);
    }
    else
    {
        Profiler::getInstance().start("SwapPipeline", "Alpha blending");
        image->alphaBlend(*warpedSwappedFace, *warpedMask);
        Profiler::getInstance().stop("SwapPipeline", "Alpha blending");
    }

    return true;
}

} // namespace linuxface


