/**
 * WFLW TEST BASE IMPLEMENTATION
 *
 * Implementation of the base class for WFLW integration tests
 */

#include "wflw_test_base.h"

#include <cstdlib>
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
    std::string wflw_annotations_path = getWFLWAnnotationsPath() + "/list_98pt_rect_attr_test.txt";
    if (!std::filesystem::exists(wflw_annotations_path))
    {
        GTEST_SKIP() << "WFLW annotations not found at: " << wflw_annotations_path;
    }

    // Get max samples from environment variable, default to 100 for tests
    const char* max_samples_env = std::getenv("WFLW_MAX_SAMPLES");
    int max_samples = max_samples_env ? std::atoi(max_samples_env) : 100;
    
    // For local development, allow unlimited samples
    if (max_samples == -1)
    {
        std::cout << "Loading ALL available WFLW samples (this may take a while)" << std::endl;
    }
    else
    {
        std::cout << "Loading up to " << max_samples << " WFLW samples for testing" << std::endl;
    }

    try
    {
        wflw_loader_ = std::make_unique<WFLWLoader>(wflw_annotations_path, max_samples);
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

std::string WFLWTestBase::normalizePath(const std::string& path)
{
    std::string normalized = path;

    // Remove any trailing slashes first
    while (!normalized.empty() && normalized.back() == '/')
    {
        normalized.pop_back();
    }

    // Replace double slashes with single slashes
    size_t pos = 0;
    while ((pos = normalized.find("//", pos)) != std::string::npos)
    {
        normalized.replace(pos, 2, "/");
        pos += 1;
    }

    return normalized;
}

std::string WFLWTestBase::getWFLWBasePath()
{
    return normalizePath(Config::getInstance().getWFLWFolderPath());
}

std::string WFLWTestBase::getWFLWImagesPath()
{
    return normalizePath(getWFLWBasePath() + "/WFLW_images");
}

std::string WFLWTestBase::getWFLWAnnotationsPath()
{
    return normalizePath(getWFLWBasePath() + "/WFLW_annotations/list_98pt_rect_attr_train_test");
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
    std::string output_dir = createTestOutputDirectory("detection_visualization");
    std::string filename = "detection_viz_img" + std::to_string(image_index) + "_mne" + std::to_string(mne) + ".ppm";
    
    // Combine with output directory
    if (!output_dir.empty())
    {
        filename = output_dir + "/" + filename;
    }
    
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
    std::string output_dir = createTestOutputDirectory("detection_visualization");
    std::string filename = "detection_viz_detailed_img" + std::to_string(image_index) + "_face"
                           + std::to_string(face_index) + "_mne" + std::to_string(mne) + ".ppm";
    
    // Combine with output directory
    if (!output_dir.empty())
    {
        filename = output_dir + "/" + filename;
    }
    
    viz_image->saveToDisk(filename);
}
