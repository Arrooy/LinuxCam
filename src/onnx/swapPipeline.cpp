#include "LinuxFace/onnx/swapPipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/profiler.h"

namespace linuxface
{

namespace
{

std::vector<float>
computeDistanceField(const std::vector<unsigned char>& binaryMask, int width, int height, bool seedsAreInside)
{
    // PERF: Current implementation uses a Dijkstra-style propagation. If ROI sizes grow, consider
    // replacing with a linear-time, two-pass Euclidean Distance Transform.
    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> dist(static_cast<size_t>(width) * static_cast<size_t>(height), inf);

    using Node = std::pair<float, int>;
    auto cmp = [](const Node& lhs, const Node& rhs) { return lhs.first > rhs.first; };
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> queue(cmp);

    for (int idx = 0; idx < width * height; ++idx)
    {
        const bool isInside = binaryMask[static_cast<size_t>(idx)] != 0;
        if ((seedsAreInside && isInside) || (!seedsAreInside && !isInside))
        {
            dist[static_cast<size_t>(idx)] = 0.0f;
            queue.emplace(0.0f, idx);
        }
    }

    if (queue.empty())
    {
        return dist;
    }

    constexpr int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr int dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    constexpr float orthCost = 1.0f;
    constexpr float diagCost = 1.41421356f;
    constexpr float costs[8] = {diagCost, orthCost, diagCost, orthCost, orthCost, diagCost, orthCost, diagCost};

    while (!queue.empty())
    {
        const auto [currentDist, index] = queue.top();
        queue.pop();

        if (currentDist > dist[static_cast<size_t>(index)])
        {
            continue;
        }

        const int x = index % width;
        const int y = index / width;

        for (int k = 0; k < 8; ++k)
        {
            const int nx = x + dx[k];
            const int ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height)
            {
                continue;
            }

            const int neighbourIndex = ny * width + nx;
            const float candidate = currentDist + costs[k];
            if (candidate < dist[static_cast<size_t>(neighbourIndex)])
            {
                dist[static_cast<size_t>(neighbourIndex)] = candidate;
                queue.emplace(candidate, neighbourIndex);
            }
        }
    }

    return dist;
}

float smootherstep(float edge0, float edge1, float value)
{
    if (edge0 == edge1)
    {
        return value >= edge0 ? 1.0f : 0.0f;
    }

    float t = (value - edge0) / (edge1 - edge0);
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float smoothstep(float edge0, float edge1, float value)
{
    if (edge0 == edge1)
    {
        return value >= edge0 ? 1.0f : 0.0f;
    }

    float t = (value - edge0) / (edge1 - edge0);
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::unique_ptr<Image> buildSmartFeatherMask(const Image& mask, float innerRadius, float outerRadius)
{
    if (mask.info.format != ImageFormat::GRAYSCALE || mask.info.pixelSizeBytes != 1 || innerRadius <= 0.0f
        || outerRadius <= 0.0f)
    {
        return nullptr;
    }

    if (outerRadius < innerRadius)
    {
        std::swap(outerRadius, innerRadius);
    }

    const int width = static_cast<int>(mask.info.width);
    const int height = static_cast<int>(mask.info.height);
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    const unsigned char* maskData = mask.data();
    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    bool hasInside = false;

    for (int y = 0; y < height; ++y)
    {
        const int rowOffset = y * width;
        for (int x = 0; x < width; ++x)
        {
            if (maskData[static_cast<size_t>(rowOffset + x)] > 0)
            {
                hasInside = true;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (!hasInside || maxX < minX || maxY < minY)
    {
        return mask.deepCopy();
    }

    const float radiusExtent = std::max(innerRadius, outerRadius);
    const int margin = std::max(2, static_cast<int>(std::ceil(radiusExtent)));
    minX = std::max(0, minX - margin);
    minY = std::max(0, minY - margin);
    maxX = std::min(width - 1, maxX + margin);
    maxY = std::min(height - 1, maxY + margin);

    const int roiWidth = maxX - minX + 1;
    const int roiHeight = maxY - minY + 1;
    if (roiWidth <= 0 || roiHeight <= 0)
    {
        return mask.deepCopy();
    }

    std::vector<unsigned char> binary(static_cast<size_t>(roiWidth) * static_cast<size_t>(roiHeight), 0);
    bool hasOutside = false;
    for (int y = 0; y < roiHeight; ++y)
    {
        const int srcY = minY + y;
        const int srcRowOffset = srcY * width;
        for (int x = 0; x < roiWidth; ++x)
        {
            const int srcX = minX + x;
            const unsigned char value = maskData[static_cast<size_t>(srcRowOffset + srcX)];
            if (value > 0)
            {
                binary[static_cast<size_t>(y) * roiWidth + static_cast<size_t>(x)] = 1;
            }
            else
            {
                hasOutside = true;
            }
        }
    }

    if (!hasOutside)
    {
        return mask.deepCopy();
    }

    auto insideDistances = computeDistanceField(binary, roiWidth, roiHeight, false);
    auto outsideDistances = computeDistanceField(binary, roiWidth, roiHeight, true);

    auto smartMask = mask.deepCopy();
    if (!smartMask)
    {
        return nullptr;
    }

    unsigned char* output = smartMask->data();
    if (!output)
    {
        return nullptr;
    }

    std::fill(output, output + smartMask->size(), static_cast<unsigned char>(0));

    const float fallbackDistance = innerRadius + outerRadius;
    constexpr float kMinInnerAlpha = 0.2f;

    for (int y = 0; y < roiHeight; ++y)
    {
        const int srcY = minY + y;
        const size_t srcRowOffset = static_cast<size_t>(srcY) * static_cast<size_t>(width);
        for (int x = 0; x < roiWidth; ++x)
        {
            const size_t idx = static_cast<size_t>(y) * roiWidth + static_cast<size_t>(x);
            float inside = insideDistances[idx];
            float outside = outsideDistances[idx];

            if (!std::isfinite(inside))
            {
                inside = fallbackDistance;
            }
            if (!std::isfinite(outside))
            {
                outside = fallbackDistance;
            }

            // Positive signed distance means inside the mask, negative outside.
            const float signedDistance = inside - outside;
            const size_t dstIndex = srcRowOffset + static_cast<size_t>(minX + x);
            const bool originalInside = binary[idx] == 1;

            if (signedDistance >= innerRadius)
            {
                output[dstIndex] = 255;
                continue;
            }

            if (signedDistance <= -outerRadius)
            {
                output[dstIndex] = 0;
                continue;
            }

            float alpha = smootherstep(-outerRadius, innerRadius, signedDistance);
            if (originalInside)
            {
                alpha = std::max(alpha, kMinInnerAlpha);
            }
            alpha = std::clamp(alpha, 0.0f, 1.0f);

            output[dstIndex] = static_cast<unsigned char>(std::round(alpha * 255.0f));
        }
    }

    return smartMask;
}

} // namespace

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
        // Perform face segmentation to create a better mask for blending
        if (faceSeg_->detect(image, swpface))
        {
            // Face segmentation succeeded
            FaceSegmentationDetector::applySegmentationVisualization(swappedFace, swpface);
        }
    }

    // swpface has the target image segmentation mask.

    // lets join mouth masks

    // Use segmentation mask to create face mask
    // Face classes: skin(1), brows(2,3), eyes(4,5), nose(10), mouth(11,12,13), neck(14)
    const std::vector<FaceSegmentationClass> faceClasses = {FaceSegmentationClass::MOUTH};
    auto swappedFaceMouth = FaceSegmentationDetector::createFaceShapeMask(*swpface.getSegmentationMask(), faceClasses);

    // Apply edge blur to soften mask boundaries without affecting center
    // Use much smaller blur for segmentation masks (pixel-accurate) vs bounding box masks
    const int blurRadius = 1;
    if (blurRadius > 0)
    {
        image_utils::softenMaskEdges(*swappedFaceMouth, blurRadius);
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
    auto crops = crop_mask_prototype_->deepCopy();

    Profiler::getInstance().start("SwapPipeline", "Smart feather mask");
    if (crop_mask_prototype_)
    {
        const auto boundingBox = face.getBoundingBox().rect;
        const float maxDimension = std::max(boundingBox.width() * 1.2f, boundingBox.height() * 1.2f);
        const float innerFeatherRadius = maxDimension * 0.01f;
        const float outerFeatherRadius = maxDimension * 0.03f;

        if (auto smartMask = buildSmartFeatherMask(*crop_mask_prototype_, innerFeatherRadius, outerFeatherRadius))
        {
            crop_mask_prototype_ = std::move(smartMask);
        }
    }
    Profiler::getInstance().stop("SwapPipeline", "Smart feather mask");

    // TODO: execute face segmentation also in finalFace image.
    // From the segmentation, use it to create a better crop mask.
    // This should remove hair and improve mouth swapping.

    if (debug_)
    {
        auto output = image->deepCopy();
        output->alphaBlend(*finalFace, *crop_mask_prototype_);
        crop_mask_prototype_->convertToRGBInplace();
        crop_mask_prototype_->drawBorder(Pixel(0, 255, 0), 2);

        image->pasteAt(*output, image->info.width, image->info.y, true);
        image->pasteAt(*debug_target_image_, image->info.width, 0, true);
        image->pasteAt(*debug_target_image_aligned_, image->info.width, 0, true);
        image->pasteAt(*finalFace, 0, image->info.height, true);
        image->pasteAt(*crop_mask_prototype_, image->info.width, image->info.height, true);
    }
    else
    {
        auto is = image->deepCopy();
        Profiler::getInstance().start("SwapPipeline", "Alpha blending");
        is->alphaBlend(*finalFace, *crops);
        Profiler::getInstance().stop("SwapPipeline", "Alpha blending");

        Profiler::getInstance().start("SwapPipeline", "Smart feather blend");
        image->alphaBlend(*finalFace, *crop_mask_prototype_);
        Profiler::getInstance().stop("SwapPipeline", "Smart feather blend");
        image->pasteAt(*is, image->info.width, 0, true);
        crop_mask_prototype_->convertToRGBInplace();
        crop_mask_prototype_->drawBorder(Pixel(0, 255, 0), 2);
        crops->convertToRGBInplace();
        crops->drawBorder(Pixel(255, 0, 0), 2);
        auto y = image->info.height;
        image->pasteAt(*crop_mask_prototype_, 0, y, true);
        image->pasteAt(*crops, crop_mask_prototype_->info.width, y, true);
        


        auto h = image->info.height;
        image->pasteAt(*finalFace, crop_mask_prototype_->info.width*2, h, true);
        // auto aux = std::make_unique<Image>(finalFace->size());
        // aux->black();
        // aux->info = finalFace->info;
        // auto w = image->info.width;
        // aux->alphaBlend(*finalFace, *crop_mask_prototype_);
        // image->pasteAt(*aux, w, h, true);

        // crop_mask_prototype_->convertToRGBInplace();
        // crop_mask_prototype_->drawBorder(Pixel(0, 255, 0), 2);
        // image->pasteAt(*crop_mask_prototype_, w, 0, true);

        // swappedFaceMouth->convertToRGB();
        // image->pasteAt(*swappedFaceMouth, 2 * w, h, true);
    }

    return true;
}

} // namespace linuxface


