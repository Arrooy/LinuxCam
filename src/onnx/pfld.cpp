#include "LinuxFace/onnx/pfld.h"

#include <opencv2/opencv.hpp>

#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/common.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/profiler.h"

using namespace linuxface;

PFLDDetector::PFLDDetector(const std::string& onnx_model_path) : OnnxDetector(onnx_model_path)
{
    // Model should have 1 output: (1, 212) for 106 landmarks (x, y)
    if (output_node_names_str_.empty() || input_node_dims.size() < 4)
    {
        ready_ = false;
        common::log_error("PFLDDetector: Invalid model or input dims");
        common::log_error("Problem is %s, input dims: %s",
                          output_node_names_str_.empty() ? "no outputs" : "invalid outputs",
                          input_node_dims.empty() ? "empty" : "not empty");
    }
}

Ort::Value PFLDDetector::transform(const std::unique_ptr<Image>& image)
{
    int target_height = static_cast<int>(input_node_dims.at(2));
    int target_width = static_cast<int>(input_node_dims.at(3));
    Ort::Value input_tensor =
        Ort::Value::CreateTensor<float>(allocator_, input_node_dims.data(), input_node_dims.size());
    float* tensor_data = input_tensor.GetTensorMutableData<float>();

    // // Use a member TensorPadding to store the transform for landmark mapping
    // pfld_padding_ = TensorPadding::scrfd();
    // image->toTensor(tensor_data, pfld_padding_, target_width, target_height, NormalizationType::MINMAX);

    // Convert image to tensor
    image_utils::ImageView<unsigned char> srcView{image->data(), image->info.width, image->info.height,
                                                  image->info.pixelSizeBytes};
    image_utils::ImageView<float> dstView{tensor_data, static_cast<size_t>(target_width),
                                          static_cast<size_t>(target_height), 3};
    image_utils::bicubicScaling<unsigned char, float, NormalizationType::MINMAX, ImageLayout::CHW>(srcView, dstView);

    return input_tensor;
}

void PFLDDetector::detect(const std::unique_ptr<Image>& image, Face& face)
{
    Profiler::getInstance().start("PFLDDetector", "detect landmarks");

    auto left_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::LEYE);
    auto right_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::REYE);

    math_utils::Point<double> eye_center = {(left_eye.x + right_eye.x) / 2.0, (left_eye.y + right_eye.y) / 2.0};

    double dx = right_eye.x - left_eye.x;
    double dy = right_eye.y - left_eye.y;
    double angleRad = -std::atan2(dy, dx); // rotate to horizontal
    double eye_dist = std::sqrt(dx * dx + dy * dy);

    // Use bounding box from SCRFD
    auto bbox = face.getBoundingBox().rect;
    double bbox_scale_factor = 1.2; // 20% expansion
    double base_box_size = eye_dist * 3.5;
    double bbox_box_size = std::max(bbox.width(), bbox.height()) * bbox_scale_factor;
    double box_size = std::max(bbox_box_size, base_box_size);


    // Store original image dimensions
    unsigned long orig_width = image->info.width;
    unsigned long orig_height = image->info.height;
    double max_box_size = std::min(orig_width, orig_height) * 0.9;
    box_size = std::min(box_size, max_box_size);

    auto aligned_face = image->deepCopy();

    // Rotate the whole image
    aligned_face->rotate(angleRad, eye_center);

    // Calculate where the eye center should be in the rotated image
    // We need to simulate the same transformation that the rotation function does
    double cosA = std::cos(angleRad);
    double sinA = std::sin(angleRad);

    // Calculate the corners of the original image relative to the eye center
    double corners[4][2] = {
        {-eye_center.x,                 -eye_center.y                 }, // top-left
        {orig_width - 1 - eye_center.x, -eye_center.y                 }, // top-right
        {-eye_center.x,                 orig_height - 1 - eye_center.y}, // bottom-left
        {orig_width - 1 - eye_center.x, orig_height - 1 - eye_center.y}  // bottom-right
    };

    // Find the bounding box of rotated corners
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (int i = 0; i < 4; ++i)
    {
        double x = corners[i][0] * cosA - corners[i][1] * sinA;
        double y = corners[i][0] * sinA + corners[i][1] * cosA;
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }


    // 4. Compute crop center
    math_utils::Point<double> face_center_original = {bbox.l + bbox.width() / 2.0, bbox.t + bbox.height() / 2.0};
    double face_dx = face_center_original.x - eye_center.x;
    double face_dy = face_center_original.y - eye_center.y;
    math_utils::Point<double> final_face_center = {cosA * face_dx - sinA * face_dy - minX,
                                                   sinA * face_dx + cosA * face_dy - minY};

    // 5. Crop (reverted to original logic)
    double half_box = box_size / 2.0;
    double crop_left = final_face_center.x - half_box;
    double crop_top = final_face_center.y - half_box;
    math_utils::Rect<float> crop_rect = {
        {static_cast<float>(crop_left), static_cast<float>(crop_top)},
        static_cast<float>(box_size),
        static_cast<float>(box_size)
    };
    aligned_face = aligned_face->crop(crop_rect);
    if (!aligned_face)
    {
        common::log_error("Failed to crop aligned face image for MediaPipe landmarks detection");
        return;
    }
    // 4. Run PFLD on aligned face
    Ort::Value input_tensor = this->transform(aligned_face);

    auto output_tensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &input_tensor, 1,
                                                 output_node_names_.data(), output_node_names_str_.size());
    if (output_tensors.empty())
    {
        common::log_error("PFLDDetector: No output tensors received");
        return;
    }
    // Find the correct output tensor (should be named "output" and have shape (1, 212))
    int output_index = 0;
    for (size_t i = 0; i < output_node_names_str_.size(); ++i)
    {
        if (output_node_names_str_[i] == "output")
        {
            output_index = static_cast<int>(i);
            break;
        }
    }
    Ort::Value& landmarks_tensor = output_tensors.at(output_index); // (1, 212)
    const float* data = landmarks_tensor.GetTensorData<float>();
    unsigned int num_landmarks = 106;
    face.loadNewFaceLandmarks({});

    double scale = 112.0 / box_size;
    // Preserve floating-point precision for landmark coordinates
    std::vector<math_utils::Point<double>> aligned_pts(num_landmarks);
    for (unsigned int i = 0; i < num_landmarks; ++i)
    {
        double x = data[2 * i] * 112.0;
        double y = data[2 * i + 1] * 112.0;
        auto pt = alignedToOriginalCoords(x, y, crop_left, crop_top, minX, minY, angleRad, eye_center, scale);
        aligned_pts[i] = {pt.x, pt.y};
    }



    std::vector<FaceLandmark> pfld_landmarks;
    pfld_landmarks.reserve(num_landmarks);
    for (unsigned int i = 0; i < num_landmarks; ++i)
    {
        // Convert Point to Point3D (z=0)
        pfld_landmarks.push_back(FaceLandmark{i, math_utils::Point3D(aligned_pts[i].x, aligned_pts[i].y, 0.0)});
    }
    face.loadNewFaceLandmarks(std::move(pfld_landmarks));
    Profiler::getInstance().stop("PFLDDetector", "detect landmarks");
}


void PFLDDetector::detectSimilar(const std::unique_ptr<Image>& image, Face& face)
{
    Profiler::getInstance().start("PFLDDetector", "detect landmarks");
    // 1. Get 5-point landmarks in ArcFace order (left eye, right eye, nose, left mouth, right mouth)
    std::vector<math_utils::Point<>> five_pts_2d = face.getFivePointLandmarksArcFaceOrder2D();
    if (five_pts_2d.size() != 5)
    {
        common::log_error("PFLDDetector: Need 5-point landmarks for alignment");
        return;
    }
    auto [aligned_face, affine] =
        image_utils::similarity_face_transform(*image, five_pts_2d, image_utils::template_192_alt, 192, true);

    if (!aligned_face)
    {
        common::log_error("PFLDDetector: Failed to warp face for alignment");
        return;
    }
    aligned_face->saveToDisk("aligned_face_similar.ppm");
    // 4. Run PFLD on aligned face
    Ort::Value input_tensor = this->transform(aligned_face);

    auto output_tensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &input_tensor, 1,
                                                 output_node_names_.data(), output_node_names_str_.size());
    if (output_tensors.empty())
    {
        common::log_error("PFLDDetector: No output tensors received");
        return;
    }
    // Find the correct output tensor (should be named "output" and have shape (1, 212))
    int output_index = 0;
    for (size_t i = 0; i < output_node_names_str_.size(); ++i)
    {
        if (output_node_names_str_[i] == "output")
        {
            output_index = static_cast<int>(i);
            break;
        }
    }
    Ort::Value& landmarks_tensor = output_tensors.at(output_index); // (1, 212)
    const float* data = landmarks_tensor.GetTensorData<float>();
    unsigned int num_landmarks = 106;
    face.loadNewFaceLandmarks({});

    // // 5. Map landmarks from aligned face back to original image using inverse affine
    double inv_affineM[6];
    if (!math_utils::invert_affine(affine.data(), inv_affineM))
    {
        common::log_error("PFLDDetector: Failed to invert affine for landmark mapping");
        return;
    }

    double w = aligned_face->info.width;
    double h = aligned_face->info.height;

    common::log_info("PFLDDetector: Aligned face size: %fx%f", w, h);
    std::vector<std::pair<double, double>> aligned_pts(num_landmarks);
    for (unsigned int i = 0; i < num_landmarks; ++i)
    {
        double x = std::min(std::max(0.f, data[2 * i]), 1.0f);
        double y = std::min(std::max(0.f, data[2 * i + 1]), 1.0f);
        aligned_pts[i] = {x * w, y * h};
    }

    auto unaligned_pts = image_utils::transform_points_affine(aligned_pts, inv_affineM);
    std::vector<FaceLandmark> pfld_landmarks(num_landmarks);
    pfld_landmarks.reserve(num_landmarks);
    for (unsigned int i = 0; i < num_landmarks; ++i)
    {
        // Convert Point to Point3D (z=0)
        pfld_landmarks[i] = FaceLandmark{i, math_utils::Point3D(unaligned_pts[i].first, unaligned_pts[i].second, 0.0)};
    }
    face.loadNewFaceLandmarks(std::move(pfld_landmarks));
    Profiler::getInstance().stop("PFLDDetector", "detect landmarks");
}


void PFLDDetector::detectOpenCv(const std::unique_ptr<Image>& image, Face& face)
{
    Profiler::getInstance().start("PFLDDetector", "Opencv landmarks");
    // 1. Get 5-point landmarks in ArcFace order (left eye, right eye, nose, left mouth, right mouth)
    std::vector<math_utils::Point<>> five_pts_2d = face.getFivePointLandmarksArcFaceOrder2D();
    if (five_pts_2d.size() != 5)
    {
        common::log_error("PFLDDetector: Need 5-point landmarks for alignment");
        return;
    }

    std::vector<cv::Point2f> five_pts_cv;
    for (const auto& pt : five_pts_2d)
    {
        five_pts_cv.emplace_back(static_cast<float>(pt.x), static_cast<float>(pt.y));
    }
    std::vector<cv::Point2f> template_cv;
    for (const auto& pt : image_utils::template_112)
    {
        template_cv.emplace_back(static_cast<float>(pt[0] * 112.0), static_cast<float>(pt[1] * 112.0));
    }

    // 4. Estimate affine transform using eyes + nose (most stable)
    std::vector<cv::Point2f> src_stable = {five_pts_cv[0], five_pts_cv[1], five_pts_cv[2]};
    std::vector<cv::Point2f> dst_stable = {template_cv[0], template_cv[1], template_cv[2]};

    cv::Mat affine = cv::getAffineTransform(src_stable, dst_stable);

    // 1. Create a new, empty OpenCV Mat with the desired dimensions and type.
    cv::Mat input_image(static_cast<int>(image->info.height), static_cast<int>(image->info.width), CV_8UC3);

    // 2. Calculate the total size of the image data in bytes.
    //    (Assuming pixelSizeBytes is 3 for a 3-channel image).

    size_t data_size = image->info.width * image->info.height * image->info.pixelSizeBytes;
    common::log_info("PFLDDetector: Input image size: %zu bytes", data_size);
    // 3. Copy your raw image data into the new cv::Mat.
    std::memcpy(input_image.data, image->data(), data_size);

    cv::Mat bgr_image;
    cv::cvtColor(input_image, bgr_image, cv::COLOR_RGB2BGR);

    // 6. Warp the image to aligned 112x112
    cv::Mat aligned_face_cv;
    cv::warpAffine(bgr_image, aligned_face_cv, affine, cv::Size(112, 112), cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                   cv::Scalar(0, 0, 0));

    // Deep copy the buffer
    data_size = aligned_face_cv.total() * aligned_face_cv.elemSize();

    cv::Mat aligned_face_rgb;
    cv::cvtColor(aligned_face_cv, aligned_face_rgb, cv::COLOR_BGR2RGB);

    // Create Image with owned buffer
    std::unique_ptr<Image> aligned_face = std::make_unique<Image>(data_size);
    std::memcpy(aligned_face->data(), aligned_face_rgb.data, data_size);
    aligned_face->info.width = aligned_face_rgb.cols;
    aligned_face->info.height = aligned_face_rgb.rows;
    aligned_face->info.pixelSizeBytes = 3;
    aligned_face->info.format = ImageFormat::RGB;

    // 4. Run PFLD on aligned face
    Ort::Value input_tensor = this->transform(aligned_face);

    auto output_tensors = detector_session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), &input_tensor, 1,
                                                 output_node_names_.data(), output_node_names_str_.size());
    if (output_tensors.empty())
    {
        common::log_error("PFLDDetector: No output tensors received");
        return;
    }
    // Find the correct output tensor (should be named "output" and have shape (1, 212))
    int output_index = 0;
    for (size_t i = 0; i < output_node_names_str_.size(); ++i)
    {
        if (output_node_names_str_[i] == "output")
        {
            output_index = static_cast<int>(i);
            break;
        }
    }
    Ort::Value& landmarks_tensor = output_tensors.at(output_index); // (1, 212)
    const float* data = landmarks_tensor.GetTensorData<float>();
    unsigned int num_landmarks = 106;
    face.loadNewFaceLandmarks({});

    cv::Mat affine_64f;
    affine.convertTo(affine_64f, CV_64F);

    cv::Mat inverse_affine_2x3;
    cv::invertAffineTransform(affine_64f, inverse_affine_2x3);

    std::vector<cv::Point2d> aligned_pts_cv(num_landmarks);
    for (unsigned int i = 0; i < num_landmarks; ++i)
    {
        // data[] is from the model, already normalized [0,1]
        aligned_pts_cv[i] = cv::Point2d(data[2 * i] * 112.0, data[2 * i + 1] * 112.0);
    }

    // Use cv::transform for robust inverse mapping
    std::vector<cv::Point2d> original_landmarks_cv;
    cv::transform(aligned_pts_cv, original_landmarks_cv, inverse_affine_2x3);

    // Now populate your FaceLandmark struct from original_landmarks_cv
    face.loadNewFaceLandmarks({});
    std::vector<FaceLandmark> pfld_landmarks;
    pfld_landmarks.reserve(num_landmarks);
    for (unsigned int i = 0; i < num_landmarks; ++i)
    {
        pfld_landmarks.emplace_back(
            FaceLandmark{i, math_utils::Point3D(original_landmarks_cv[i].x, original_landmarks_cv[i].y, 0.0)});
    }
    face.loadNewFaceLandmarks(std::move(pfld_landmarks));

    Profiler::getInstance().stop("PFLDDetector", "Opencv landmarks");
}

math_utils::Point<double>
PFLDDetector::alignedToOriginalCoords(double x_aligned, double y_aligned, double crop_left, double crop_top,
                                      double minX, double minY, double angleRad,
                                      const math_utils::Point<double>& eye_center, double scale)
{
    // Step 0: undo scaling (map from 112x112 back to cropped box size)
    double x_unscaled = x_aligned / scale;
    double y_unscaled = y_aligned / scale;

    // Step 1: undo crop
    double x_rotated = x_unscaled + crop_left;
    double y_rotated = y_unscaled + crop_top;

    // Step 2: get absolute rotated coordinates
    double x_rel = x_rotated + minX;
    double y_rel = y_rotated + minY;

    // Step 3: un-rotate around eye center
    double cosA = std::cos(-angleRad);
    double sinA = std::sin(-angleRad);

    double x_orig = cosA * x_rel - sinA * y_rel + eye_center.x;
    double y_orig = sinA * x_rel + cosA * y_rel + eye_center.y;

    return {x_orig, y_orig};
}
