#include "LinuxFace/onnx/swapPipeline.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/profiler.h"

namespace linuxface
{

SwapPipeline::SwapPipeline(std::shared_ptr<InSwapper> inswapper, std::shared_ptr<ArcfaceRecognizer> arcface,
                           std::shared_ptr<SCRFDetector> scrfd)
    : inswapper_(inswapper), arcface_(arcface), scrfd_(scrfd)
{
}

bool SwapPipeline::run(std::unique_ptr<Image>& image, std::unique_ptr<Image>& target_img)
{
    Profiler::getInstance().start("SwapPipeline", "run");
    if (!inswapper_ || !inswapper_->isReady() || !target_img || !arcface_ || !arcface_->isReady() || !scrfd_ || !image)
    {
        return false;
    }
    // 1. Get landmarks for target_img (source) only once
    if (!target_img_embedding_ready_)
    {
        Profiler::getInstance().start("SwapPipeline", "get target embedding");
        std::vector<Face> target_faces = scrfd_->detect(target_img);
        if (!target_faces.empty())
        {
            target_img_landmarks_ = target_faces[0].getFivePointLandmarksArcFaceOrder();
            if (target_img_landmarks_.size() == 5)
            {
                arcface_->recognize(*target_img, target_img_landmarks_, target_img_embedding_);
                target_img_embedding_ready_ = (target_img_embedding_.size() == 512);
            }
        }
        if (!target_img_embedding_ready_)
        {
            common::log_error("Target image embedding is not ready or landmarks are not valid.");
            return false;
        }
        Profiler::getInstance().stop("SwapPipeline", "get target embedding");
    }
    Profiler::getInstance().start("SwapPipeline", "detect image faces");
    // 2. Get landmarks for webcam (target)
    std::vector<Face> scrfd_faces = scrfd_->detect(image);
    if (scrfd_faces.empty())
    {
        common::log_warn("SwapPipeline: No faces detected in the webcam image.");
        return false;
    }

    std::vector<math_utils::Point> webcam_landmarks = scrfd_faces[0].getFivePointLandmarksArcFaceOrder();
    if (webcam_landmarks.size() != 5)
    {
        common::log_error("SwapPipeline: Detected face does not have 5 landmarks. It has %d landmarks.",
                          static_cast<int>(webcam_landmarks.size()));
        return false;
    }
    Profiler::getInstance().stop("SwapPipeline", "detect image faces");
    Profiler::getInstance().start("SwapPipeline", "swap face");
    // Todo: the face always have similar size, maybe we can rehuse the memory allocated for other images
    Image swapped_face;
    bool swap_ok = inswapper_->swap(target_img_embedding_, webcam_landmarks, *image, swapped_face);
    if (!swap_ok)
    {
        common::log_error("SwapPipeline: Face swap failed.");
        return false;
    }
    Profiler::getInstance().stop("SwapPipeline", "swap face");
    Profiler::getInstance().start("SwapPipeline", "Affine Warp and Crop face"); // 12ms!
    // 3. Estimate affine transformation from template to webcam landmarks
    static const double template_128[5][2] = {
        {0.34191607, 0.46157411},
        {0.65653393, 0.45983393},
        {0.50022500, 0.64050536},
        {0.37097589, 0.82469196},
        {0.63151696, 0.82325089}
    };
    double src_points[10], dst_points[10];
    for (int i = 0; i < 5; ++i)
    {
        src_points[2 * i] = template_128[i][0] * swapped_face.info.width;
        src_points[2 * i + 1] = template_128[i][1] * swapped_face.info.height;
        dst_points[2 * i] = webcam_landmarks[i].x;
        dst_points[2 * i + 1] = webcam_landmarks[i].y;
    }

    double affineM[6];
    math_utils::estimate_affine_2d(src_points, dst_points, 5, affineM);
    std::unique_ptr<Image> warped_swapped_face =
        swapped_face.affineWarp(affineM, image->info.width, image->info.height);
    Profiler::getInstance().stop("SwapPipeline", "Affine Warp and Crop face");
    if (!warped_swapped_face)
    {
        common::log_error("SwapPipeline: Failed to warp swapped face.");
        return false;
    }

    Profiler::getInstance().start("SwapPipeline", "Crop mask creation 1"); // 40ms -> peeks to 100 if face is laterally rotated
    // 4. Create a crop_mask for the swapped face
    // Use the bounding box of the detected face to create a crop_mask
    const auto& bbox = scrfd_faces[0].getBoundingBox();
    double out_width = bbox.rect.width();
    double out_height = bbox.rect.height();
    double min_x = bbox.rect.x();
    double min_y = bbox.rect.y();
    std::vector<double> crop_size = {out_width, out_height};
    std::unique_ptr<Image> crop_mask = image_utils::create_static_box_mask(crop_size);
    if (!crop_mask)
    {
        common::log_error("SwapPipeline: Failed to create crop mask.");
        return false;
    }
    Profiler::getInstance().stop("SwapPipeline", "Crop mask creation 1");
    Profiler::getInstance().start("SwapPipeline", "Crop mask creation 2");
    // Compute the affine to paste the crop_mask/crop into the full image
    double paste_src[6] = {0, 0, out_width, 0, 0, out_height};
    double paste_dst[6] = {min_x, min_y, min_x + out_width, min_y, min_x, min_y + out_height};
    double paste_affine[6];
    math_utils::estimate_affine_2d(paste_src, paste_dst, 3, paste_affine);
    bool worked = false;
    Profiler::getInstance().stop("SwapPipeline", "Crop mask creation 2");
    Profiler::getInstance().start("SwapPipeline", "Crop mask creation 3"); // this is 2.5ms
    std::unique_ptr<Image> warped_mask = crop_mask->affineWarpMask(paste_affine, image->info.width, image->info.height);
    if (!warped_mask)
    {
        common::log_error("SwapPipeline: Failed to warp crop mask.");
        return false;
    }
    Profiler::getInstance().stop("SwapPipeline", "Crop mask creation 3");
    Profiler::getInstance().start("SwapPipeline", "Alpha blending");
    // Alpha blend crop_mask onto temp_img using warped_mask
    image->alphaBlend(*warped_swapped_face, *warped_mask);
    Profiler::getInstance().stop("SwapPipeline", "Alpha blending");
    worked = true;

    Profiler::getInstance().stop("SwapPipeline", "run");
    return worked;
}

} // namespace linuxface
