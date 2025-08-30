/**
 * WFLW TEST BASE IMPLEMENTATION
 *
 * Implementation of the base class for WFLW integration tests
 */

#include "wflw_test_base.h"
#include "../../test_utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include "config.hpp"

using namespace linuxface;

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

    // Initialize WFLW loader using centralized dataset utilities
    wflw_loader_ = std::make_unique<TestUtils::Datasets::SimpleWFLWLoader>();
    int max_samples = TestUtils::getEnvVarInt("WFLW_MAX_SAMPLES", 100);
    if (!wflw_loader_->loadDataset(max_samples)) {
        GTEST_SKIP() << "Failed to load WFLW dataset using centralized loader.";
    }
    if (wflw_loader_->getSampleCount() == 0) {
        GTEST_SKIP() << "No WFLW samples loaded.";
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

std::string WFLWTestBase::createTestOutputDirectory(const std::string& test_name) const
{
    std::filesystem::path output_dir = "testing";
    output_dir /= "wflw_integration_suite";
    output_dir /= test_name;
    
    try
    {
        std::filesystem::create_directories(output_dir);
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::cerr << "Failed to create output directory " << output_dir << ": " << e.what() << std::endl;
        return "";
    }
    
    return output_dir.string();
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

void WFLWTestBase::saveDetectionVisualization(const TestUtils::Datasets::WFLWSample& sample,
                                              const std::vector<FaceLandmark>& detected_landmarks, int image_index,
                                              double mne) const
{
    auto image_ptr = sample.loadImage();
    if (!image_ptr) return;
    if (!TestUtils::getEnvVarBool("SAVE_IMAGES")) return;
    auto viz_image = image_ptr->deepCopy();
    Pixel red_color(255, 0, 0);
    std::vector<math_utils::Point<long>> detected_points;
    for (const auto& landmark : detected_landmarks)
        detected_points.emplace_back(static_cast<long>(landmark.p.x), static_cast<long>(landmark.p.y));
    viz_image->paintPoints(detected_points, red_color);
    Pixel green_color(0, 255, 0);
    std::vector<math_utils::Point<long>> gt_points;
    for (const auto& gt_landmark : sample.landmarks)
        gt_points.emplace_back(static_cast<long>(gt_landmark[0]), static_cast<long>(gt_landmark[1]));
    viz_image->paintPoints(gt_points, green_color);
    std::string mne_text = "MNE: " + std::to_string(mne);
    drawText(*viz_image, 10, 10, mne_text, Pixel(255, 255, 255));
    std::string output_dir = createTestOutputDirectory("detection_visualization");
    std::string filename = "detection_viz_img" + std::to_string(image_index) + "_mne" + std::to_string(mne) + ".ppm";
    if (!output_dir.empty()) filename = output_dir + "/" + filename;
    viz_image->saveToDisk(filename);
}

void WFLWTestBase::saveDetectionVisualizationWithFaceInfo(const TestUtils::Datasets::WFLWSample& sample,
                                                          const std::vector<FaceLandmark>& detected_landmarks,
                                                          int image_index, double mne, int face_index, double iou,
                                                          int total_faces) const
{
    auto image_ptr = sample.loadImage();
    if (!image_ptr) return;
    if (!TestUtils::getEnvVarBool("SAVE_IMAGES")) return;
    auto viz_image = image_ptr->deepCopy();
    Pixel red_color(255, 0, 0);
    std::vector<math_utils::Point<long>> detected_points;
    for (const auto& landmark : detected_landmarks)
        detected_points.emplace_back(static_cast<long>(landmark.p.x), static_cast<long>(landmark.p.y));
    viz_image->paintPoints(detected_points, red_color);
    Pixel green_color(0, 255, 0);
    std::vector<math_utils::Point<long>> gt_points;
    for (const auto& gt_landmark : sample.landmarks)
        gt_points.emplace_back(static_cast<long>(gt_landmark[0]), static_cast<long>(gt_landmark[1]));
    viz_image->paintPoints(gt_points, green_color);
    std::vector<std::string> info_lines = {"MNE: " + std::to_string(mne),
                                           "Face: " + std::to_string(face_index) + "/" + std::to_string(total_faces),
                                           "IoU: " + std::to_string(iou)};
    Pixel white_color(255, 255, 255);
    int text_y = 10;
    for (const auto& line : info_lines) {
        drawText(*viz_image, 10, text_y, line, white_color);
        text_y += 15;
    }
    std::string output_dir = createTestOutputDirectory("detection_visualization");
    std::string filename = "detection_viz_detailed_img" + std::to_string(image_index) + "_face"
                           + std::to_string(face_index) + "_mne" + std::to_string(mne) + ".ppm";
    if (!output_dir.empty()) filename = output_dir + "/" + filename;
    viz_image->saveToDisk(filename);
}
