#include "LinuxFace/onnx/swapPipeline.h"

#include <iostream>
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
    : inswapper_(std::move(std::move(inswapper)))
    , arcface_(std::move(std::move(arcface)))
    , scrfd_(std::move(std::move(scrfd)))
{
}

bool SwapPipeline::run(std::unique_ptr<Image>& image, std::unique_ptr<Image>& targetImg)
{
    Profiler::getInstance().start("SwapPipeline", "run");
    if (!inswapper_ || !inswapper_->isReady() || !targetImg || !arcface_ || !arcface_->isReady() || !scrfd_ || !image)
    {
        return false;
    }

    // 1. Get landmarks for target_img (source) only once
    if (!target_img_embedding_ready_)
    {
        Profiler::getInstance().start("SwapPipeline", "get target embedding");
        std::vector<Face> targetFaces = scrfd_->detect(targetImg);
        if (!targetFaces.empty())
        {
            common::logInfo("SwapPipeline: Detected face %d with bounding box: (%f, %f, "
                            "%f, %f)",
                            99, targetFaces[0].getBoundingBox().rect.x(), targetFaces[0].getBoundingBox().rect.y(),
                            targetFaces[0].getBoundingBox().rect.width(),
                            targetFaces[0].getBoundingBox().rect.height());
            target_img_landmarks_ = targetFaces[0].getFivePointLandmarksArcFaceOrder2D();
            if (target_img_landmarks_.size() == 5)
            {
                // print keypoints
                common::logInfo("Target image landmarks: ");
                for (const auto& landmark : target_img_landmarks_)
                {
                    common::logInfo("  - (%ld, %ld)", landmark.x, landmark.y);
                }

                arcface_->recognize(*targetImg, target_img_landmarks_, target_img_embedding_);
                target_img_embedding_ready_ = (target_img_embedding_.size() == 512);
            }
            if (debug_)
            {
                debug_target_image_ = std::move(targetImg->deepCopy());
                targetFaces[0].paintBoundingBox(debug_target_image_, Pixel(255, 0, 0));
                targetFaces[0].paintAllFaceLandmarks(debug_target_image_, false, Pixel(255, 0, 0), 5.0f);
                debug_target_image_aligned_ = arcface_->preprocess(*targetImg, target_img_landmarks_);
                debug_target_image_->scaleInPlace(0.3, ScalingAlgorithm::AREA_AVERAGING);
                // debug_target_image_aligned_->scaleInPlace(0.3, ScalingAlgorithm::AREA_AVERAGING);
            }
        }
        if (!target_img_embedding_ready_)
        {
            common::logError("Target image embedding is not ready or landmarks are not "
                             "valid.");
            return false;
        }
        Profiler::getInstance().stop("SwapPipeline", "get target embedding");
    }
    Profiler::getInstance().start("SwapPipeline", "detect image faces");
    // 2. Get landmarks for webcam (target)
    const std::vector<Face> scrfdFaces = scrfd_->detect(image);
    if (scrfdFaces.empty())
    {
        common::logWarn("SwapPipeline: No faces detected in the webcam image.");
        return false;
    }
    bool worked = false;
    int i = 0;
    Image swappedFace; // Reuse buffer for all faces
    for (const auto& face : scrfdFaces)
    {
        // print bounding box coords
        common::logInfo("SwapPipeline: Detected face %d with bounding box: (%f, %f, %f, "
                        "%f)",
                        i, face.getBoundingBox().rect.x(), face.getBoundingBox().rect.y(),
                        face.getBoundingBox().rect.width(), face.getBoundingBox().rect.height());
        std::vector<math_utils::Point<>> webcamLandmarks = face.getFivePointLandmarksArcFaceOrder2D();
        if (webcamLandmarks.size() != 5)
        {
            common::logError("SwapPipeline: Detected face does not have 5 landmarks. It has "
                             "%d landmarks.",
                             static_cast<int>(webcamLandmarks.size()));
            return false;
        }
        common::logInfo("Source image landmarks %d", i++);
        for (const auto& landmark : webcamLandmarks)
        {
            common::logInfo("  - (%ld, %ld)", landmark.x, landmark.y);
        }
        Profiler::getInstance().stop("SwapPipeline", "detect image faces");
        Profiler::getInstance().start("SwapPipeline", "swap face");
        // Reuse swapped_face buffer for all faces
        const bool swapOk = inswapper_->swap(target_img_embedding_, webcamLandmarks, *image, swappedFace);
        if (!swapOk)
        {
            common::logError("SwapPipeline: Face swap failed.");
            return false;
        }
        Profiler::getInstance().stop("SwapPipeline", "swap face");
        Profiler::getInstance().start("SwapPipeline", "Affine Warp and Crop face"); // 10ms in 1080px!
        // 3. Estimate affine transformation from template to webcam landmarks
        double srcPoints[10];
        double dstPoints[10];
        for (int i = 0; i < 5; ++i)
        {
            srcPoints[2 * i] = image_utils::template[i][0] * swappedFace.info.width;
            srcPoints[2 * i + 1] = image_utils::template[i][1] * swappedFace.info.height;
            dstPoints[2 * i] = webcamLandmarks[i].x;
            dstPoints[2 * i + 1] = webcamLandmarks[i].y;
        }

        double affineM[6];
        math_utils::estimateAffine2d(srcPoints, dstPoints, 5, affineM);
        std::unique_ptr<Image> warpedSwappedFace =
            swappedFace.affineWarpBilinear(affineM, image->info.width, image->info.height);
        if (!warpedSwappedFace)
        {
            common::logError("SwapPipeline: Affine warp failed.");
            return false;
        }

        Profiler::getInstance().stop("SwapPipeline", "Affine Warp and Crop face"); // 3ms.
        Profiler::getInstance().start("SwapPipeline", "Crop mask creation");
        // 4. Create a crop_mask for the swapped face
        // Use the bounding box of the detected face to create a crop_mask
        const auto& bbox = face.getBoundingBox();
        const double outWidth = bbox.rect.width();
        const double outHeight = bbox.rect.height();
        const double minX = bbox.rect.x();
        const double minY = bbox.rect.y();
        const std::vector<double> cropSize = {outWidth, outHeight};
        std::unique_ptr<Image> cropMask = image_utils::createStaticBoxMask(cropSize);
        if (!cropMask)
        {
            common::logError("SwapPipeline: Failed to create crop mask.");
            return false;
        }

        std::unique_ptr<Image> warpedMask = std::make_unique<Image>(image->size());
        warpedMask->info = image->info;                   // Copy metadata from the original image
        warpedMask->info.pixelSizeBytes = 1;              // Single channel for mask
        warpedMask->info.format = ImageFormat::GRAYSCALE; // Mask is grayscale
        warpedMask->black();                              // Initialize the mask to black
        warpedMask->pasteAt(*cropMask, minX, minY, true);
        if (!warpedMask)
        {
            common::logError("SwapPipeline: Failed to warp crop mask.");
            return false;
        }
        Profiler::getInstance().stop("SwapPipeline", "Crop mask creation");


        worked = true;

        if (debug_)
        {
            auto output = image->deepCopy();
            output->alphaBlend(*warpedSwappedFace, *warpedMask);
            for (const auto& face : scrfdFaces)
            {
                face.paintBoundingBox(image, Pixel(0, 255, 0));
                face.paintAllFaceLandmarks(image, false, Pixel(0, 255, 0), 2.0f);
            }
            auto scale = 1;
            if (scale != 1)
            {
                image->scaleInPlace(scale);
                output->scaleInPlace(scale);
                warpedSwappedFace->scaleInPlace(scale);
                warpedMask->scaleInPlace(scale);
            }
            warpedMask->convertToRgbInplace();
            cropMask->convertToRgbInplace();

            image->drawBorder(Pixel(255, 0, 0), 2);
            output->drawBorder(Pixel(0, 255, 0), 2);
            warpedSwappedFace->drawBorder(Pixel(0, 0, 255), 2);
            warpedMask->drawBorder(Pixel(255, 255, 0), 2);
            cropMask->drawBorder(Pixel(255, 0, 255), 2);

            auto width = image->info.width;
            auto height = image->info.height;
            image->pasteAt(*output, image->info.width, image->info.y, true);
            image->pasteAt(*debug_target_image_, image->info.width, 0, true);
            image->pasteAt(*debug_target_image_aligned_, image->info.width, 0, true);
            image->pasteAt(*warpedSwappedFace, 0, height, true);
            image->pasteAt(*warped_mask, width, height, true);
            image->pasteAt(*crop_mask, width * 2, height, true);
        }
        else
        {
            Profiler::getInstance().start("SwapPipeline", "Alpha blending");
            // Alpha blend crop_mask onto temp_img using warped_mask

            image->alphaBlend(*warpedSwappedFace, *warped_mask);
            Profiler::getInstance().stop("SwapPipeline", "Alpha blending");
        }
        Profiler::getInstance().stop("SwapPipeline", "run");
    }
    return worked;
}

} // namespace linuxface
