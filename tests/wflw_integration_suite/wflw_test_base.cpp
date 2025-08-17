/**
 * WFLW TEST BASE IMPLEMENTATION
 *
 * Implementation of the base class for WFLW integration tests
 */

#include "wflw_test_base.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include "config.hpp"

using namespace linuxface;
using namespace linuxface::test;

void WFLWTestBase::SetUp()
{
    // Load configuration first
    std::vector<std::string> config_paths = {"../config.yaml", "config.yaml",
                                             "../tests/wflw_integration/test_config.yaml"};
    bool config_loaded = false;

    for (const auto& config_path : config_paths)
    {
        if (std::ifstream(config_path).good())
        {
            // Force reload config from specific path
            bool reloaded = Config::getInstance().reloadFromFile(config_path.c_str());
            if (reloaded)
            {
                // Parse the loaded configuration
                config_loaded = Config::getInstance().loadConfiguration();
            }
            if (config_loaded)
            {
                std::cout << "Loaded test configuration from: " << config_path << std::endl;
                break;
            }
        }
    }

    if (!config_loaded)
    {
        GTEST_SKIP() << "Failed to load any configuration file";
    }

    // Check for CUDA availability
    has_cuda_available_ = checkCudaAvailability();

    // Initialize SCRFD detector
    std::string scrfd_model_path = "../models/scrfd_10g_bnkps_shape640x640.onnx";
    if (!std::filesystem::exists(scrfd_model_path))
    {
        GTEST_SKIP() << "SCRFD model not found at: " << scrfd_model_path;
    }

    try
    {
        scrfd_detector_ = std::make_shared<SCRFDetector>(scrfd_model_path);
    }
    catch (const std::exception& e)
    {
        GTEST_SKIP() << "Failed to initialize SCRFD detector: " << e.what();
    }

    // Initialize PFLD detector
    std::string pfld_model_path = "../models/pfld-106-v3.onnx";
    if (!std::filesystem::exists(pfld_model_path))
    {
        GTEST_SKIP() << "PFLD model not found at: " << pfld_model_path;
    }

    try
    {
        pfld_detector_ = std::make_shared<PFLDDetector>(pfld_model_path);
    }
    catch (const std::exception& e)
    {
        GTEST_SKIP() << "Failed to initialize PFLD detector: " << e.what();
    }

    // Initialize WFLW loader
    std::string wflw_annotations_path =
        "../WFLW/WFLW_annotations/list_98pt_rect_attr_train_test/list_98pt_rect_attr_test.txt";
    if (!std::filesystem::exists(wflw_annotations_path))
    {
        GTEST_SKIP() << "WFLW annotations not found at: " << wflw_annotations_path;
    }

    try
    {
        wflw_loader_ = std::make_unique<WFLWLoader>(wflw_annotations_path, 100); // Limit to 100 samples for tests
    }
    catch (const std::exception& e)
    {
        GTEST_SKIP() << "Failed to initialize WFLW loader: " << e.what();
    }

    if (wflw_loader_->get_num_examples() == 0)
    {
        GTEST_SKIP() << "No WFLW examples loaded";
    }
}

void WFLWTestBase::TearDown()
{
    // Clean up resources
    scrfd_detector_.reset();
    pfld_detector_.reset();
    wflw_loader_.reset();
}

bool WFLWTestBase::checkCudaAvailability()
{
    // Simple check - in a real implementation you might want to check CUDA runtime
    // For now, assume CPU-only execution
    return false;
}

double WFLWTestBase::calculateMNE(const std::vector<FaceLandmark>& predicted_landmarks,
                                  const std::vector<math_utils::Point<double>>& ground_truth,
                                  double interocular_distance) const
{
    if (predicted_landmarks.size() != ground_truth.size() || interocular_distance <= 0.0)
    {
        return -1.0; // Invalid input
    }

    double total_error = 0.0;
    for (size_t i = 0; i < predicted_landmarks.size(); ++i)
    {
        double dx = predicted_landmarks[i].p.x - ground_truth[i].x;
        double dy = predicted_landmarks[i].p.y - ground_truth[i].y;
        double distance = std::sqrt(dx * dx + dy * dy);
        total_error += distance;
    }

    double mean_error = total_error / predicted_landmarks.size();
    return mean_error / interocular_distance;
}

double WFLWTestBase::calculateInterocularDistance(const Face& face) const
{
    auto landmarks = face.getLandmarks();
    if (landmarks.empty())
    {
        return -1.0;
    }

    // Try to find eye landmarks - this is a simplified approach
    // In PFLD, eye landmarks are typically around indices 60-95 range
    // For a more robust implementation, you'd want to use specific eye landmark indices

    math_utils::Point3D left_eye_center(0, 0, 0);
    math_utils::Point3D right_eye_center(0, 0, 0);

    // Simple heuristic: assume first quarter are left eye, second quarter are right eye
    if (landmarks.size() >= 20)
    {
        // Average some landmarks that are likely to be eye centers
        for (size_t i = 60; i < 70 && i < landmarks.size(); ++i)
        {
            left_eye_center.x += landmarks[i].p.x;
            left_eye_center.y += landmarks[i].p.y;
        }
        left_eye_center.x /= 10;
        left_eye_center.y /= 10;

        for (size_t i = 70; i < 80 && i < landmarks.size(); ++i)
        {
            right_eye_center.x += landmarks[i].p.x;
            right_eye_center.y += landmarks[i].p.y;
        }
        right_eye_center.x /= 10;
        right_eye_center.y /= 10;
    }
    else
    {
        return -1.0; // Not enough landmarks
    }

    double dx = right_eye_center.x - left_eye_center.x;
    double dy = right_eye_center.y - left_eye_center.y;
    return std::sqrt(dx * dx + dy * dy);
}

WFLWTestBase::FaceMatchResult
WFLWTestBase::findBestMatchingFace(const std::vector<Face>& detected_faces, const math_utils::Rect<double>& gt_bbox,
                                   double min_iou_threshold) const
{
    FaceMatchResult result;

    if (detected_faces.empty())
    {
        return result;
    }

    double best_iou = 0.0;
    int best_index = -1;

    for (size_t i = 0; i < detected_faces.size(); ++i)
    {
        auto face_bbox = detected_faces[i].getBoundingBox().rect;

        // Convert float rect to double rect for IoU calculation
        math_utils::Rect<double> face_rect_double(static_cast<double>(face_bbox.l), static_cast<double>(face_bbox.t),
                                                  static_cast<double>(face_bbox.r), static_cast<double>(face_bbox.b));

        double iou = math_utils::calculateIoU(gt_bbox, face_rect_double);

        if (iou > best_iou)
        {
            best_iou = iou;
            best_index = static_cast<int>(i);
        }
    }

    result.iou_score = best_iou;
    result.face_index = best_index;

    if (best_iou >= min_iou_threshold && best_index >= 0)
    {
        result.found_match = true;
        result.best_face = const_cast<Face*>(&detected_faces[best_index]);
    }

    return result;
}

void WFLWTestBase::saveDetectionVisualization(const WFLWExample& example,
                                              const std::vector<FaceLandmark>& detected_landmarks, int image_index,
                                              double mne) const
{
    if (!example.image)
    {
        return;
    }

    auto viz_image = example.image->deepCopy();

    // Draw detected landmarks in red
    Pixel red_color(255, 0, 0);
    std::vector<math_utils::Point<long>> detected_points;
    for (const auto& landmark : detected_landmarks)
    {
        detected_points.emplace_back(static_cast<long>(landmark.p.x), static_cast<long>(landmark.p.y));
    }
    viz_image->paintPoints(detected_points, red_color);

    // Draw ground truth landmarks in green
    Pixel green_color(0, 255, 0);
    std::vector<math_utils::Point<long>> gt_points;
    for (const auto& gt_landmark : example.landmarks)
    {
        gt_points.emplace_back(static_cast<long>(gt_landmark.x), static_cast<long>(gt_landmark.y));
    }
    viz_image->paintPoints(gt_points, green_color);

    // Add MNE text
    std::string mne_text = "MNE: " + std::to_string(mne);
    drawText(*viz_image, 10, 10, mne_text, Pixel(255, 255, 255));

    // Save visualization
    std::string filename = "detection_viz_img" + std::to_string(image_index) + "_mne" + std::to_string(mne) + ".ppm";
    viz_image->saveToDisk(filename);
}

void WFLWTestBase::saveDetectionVisualizationWithFaceInfo(const WFLWExample& example,
                                                          const std::vector<FaceLandmark>& detected_landmarks,
                                                          int image_index, double mne, int face_index, double iou,
                                                          int total_faces) const
{
    if (!example.image)
    {
        return;
    }

    auto viz_image = example.image->deepCopy();

    // Draw detected landmarks in red
    Pixel red_color(255, 0, 0);
    std::vector<math_utils::Point<long>> detected_points;
    for (const auto& landmark : detected_landmarks)
    {
        detected_points.emplace_back(static_cast<long>(landmark.p.x), static_cast<long>(landmark.p.y));
    }
    viz_image->paintPoints(detected_points, red_color);

    // Draw ground truth landmarks in green
    Pixel green_color(0, 255, 0);
    std::vector<math_utils::Point<long>> gt_points;
    for (const auto& gt_landmark : example.landmarks)
    {
        gt_points.emplace_back(static_cast<long>(gt_landmark.x), static_cast<long>(gt_landmark.y));
    }
    viz_image->paintPoints(gt_points, green_color);

    // Add detailed info text
    std::vector<std::string> info_lines = {"MNE: " + std::to_string(mne),
                                           "Face: " + std::to_string(face_index) + "/" + std::to_string(total_faces),
                                           "IoU: " + std::to_string(iou)};

    Pixel white_color(255, 255, 255);
    int text_y = 10;
    for (const auto& line : info_lines)
    {
        drawText(*viz_image, 10, text_y, line, white_color);
        text_y += 15;
    }

    // Save visualization
    std::string filename = "detection_viz_detailed_img" + std::to_string(image_index) + "_face"
                           + std::to_string(face_index) + "_mne" + std::to_string(mne) + ".ppm";
    viz_image->saveToDisk(filename);
}
