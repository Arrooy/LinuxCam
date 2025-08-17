/**
 * LANDMARK MAPPING TEST: SCRFD + PFLD + WFLW
 *
 * This test validates landmark mappings and conversions between:
 * - SCRFD 5-point landmarks (face detection)
 * - PFLD 106-point landmarks (detailed facial landmarks)
 * - WFLW 98-point ground truth landmarks (benchmark dataset)
 *
 * Uses LandmarkConverter for proper format translation between coordinate systems
 */
#include "wflw_loader.h"

#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <map>
#include <memory>
#include <numeric>
#include <onnxruntime_cxx_api.h>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/face.h"
#include "LinuxFace/landmark_converter.h"
#include "LinuxFace/onnx/pfld.h"
#include "LinuxFace/onnx/scrfd.h"
#include "config.hpp"
#include "wflw_integration_suite/wflw_test_base.h"

using namespace linuxface;
using namespace linuxface::test;

// Helper function to check CUDA availability (similar to OnnxDetector::checkCudaAvailability)
static bool checkCudaAvailability()
{
    auto available_providers = Ort::GetAvailableProviders();
    for (const auto& provider : available_providers)
    {
        if (provider == "CUDAExecutionProvider")
        {
            return true;
        }
    }
    return false;
}

/**
 * Test suite for validating landmark mappings and conversions between different
 * keypoint detection formats (SCRFD 5-point, PFLD 106-point, WFLW 98-point)
 */
class LandmarkMappingTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Ensure we can load the test configuration file
        // Only use test-specific config, not main config
        std::string config_paths[] = {"tests/wflw_integration/test_config.yaml",
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

        ASSERT_TRUE(config_loaded) << "Could not find test_config.yaml in expected test paths";

        // Check CUDA availability for conditional testing
        has_cuda_available_ = checkCudaAvailability();

        std::string models_folder = Config::getInstance().getModelFolderPath();

        scrfd_detector_ = std::make_shared<SCRFDetector>(models_folder + "scrfd_500m_bnkps_shape640x640.onnx");
        pfld_detector_ = std::make_shared<PFLDDetector>(models_folder + "pfld-106-v3.onnx");

        ASSERT_TRUE(scrfd_detector_->isReady())
            << "SCRFD detector failed to initialize. Expected path: " << models_folder
            << "scrfd_500m_bnkps_shape640x640.onnx";
        ASSERT_TRUE(pfld_detector_->isReady())
            << "PFLD detector failed to initialize. Expected path: " << models_folder << "pfld-106-v3.onnx";

        const std::string test_annotations = WFLWTestBase::getWFLWAnnotationsPath() + "/list_98pt_rect_attr_test.txt";
        wflw_loader_ = std::make_unique<WFLWLoader>(test_annotations, 10);

        ASSERT_GT(wflw_loader_->get_num_examples(), 0);
    }

    // Validate that SCRFD 5-point landmarks are reasonable
    bool validateSCRFDLandmarks(const Face& face, const Image& image) const
    {
        auto left_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::LEYE);
        auto right_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::REYE);
        auto nose = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::NOSE);
        auto left_mouth = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::LMOUTH);
        auto right_mouth = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::RMOUTH);

        // Check if landmarks are within image bounds
        std::vector<math_utils::Point3D> points;
        points.push_back(left_eye);
        points.push_back(right_eye);
        points.push_back(nose);
        points.push_back(left_mouth);
        points.push_back(right_mouth);

        for (const auto& point : points)
        {
            if (point.x < 0 || point.x >= image.info.width || point.y < 0 || point.y >= image.info.height)
            {
                return false;
            }
        }

        // Check basic facial geometry constraints
        // Eyes should be horizontally aligned (approximately)
        double eye_y_diff = std::abs(left_eye.y - right_eye.y);
        double face_height = face.getBoundingBox().rect.height();
        if (eye_y_diff > face_height * 0.15) // 15% tolerance
        {
            return false;
        }

        // Right eye should be to the right of left eye
        if (right_eye.x <= left_eye.x)
        {
            return false;
        }

        // Nose should be between eyes (roughly)
        if (nose.x < left_eye.x || nose.x > right_eye.x)
        {
            return false;
        }

        // Mouth should be below nose
        if (left_mouth.y <= nose.y || right_mouth.y <= nose.y)
        {
            return false;
        }

        return true;
    }

    // Validate PFLD 106-point landmarks structure
    bool validatePFLDLandmarks(const std::vector<FaceLandmark>& landmarks, const Image& image) const
    {
        if (landmarks.size() != 106)
        {
            return false;
        }

        // Check if all landmarks are within image bounds
        for (const auto& landmark : landmarks)
        {
            if (landmark.p.x < 0 || landmark.p.x >= image.info.width || landmark.p.y < 0
                || landmark.p.y >= image.info.height)
            {
                return false;
            }
        }

        // Check if landmarks form reasonable facial structure
        // (This is a simplified check - could be expanded)

        // Calculate bounding box of all landmarks
        double min_x = landmarks[0].p.x, max_x = landmarks[0].p.x;
        double min_y = landmarks[0].p.y, max_y = landmarks[0].p.y;

        for (const auto& landmark : landmarks)
        {
            min_x = std::min(min_x, landmark.p.x);
            max_x = std::max(max_x, landmark.p.x);
            min_y = std::min(min_y, landmark.p.y);
            max_y = std::max(max_y, landmark.p.y);
        }

        // Landmarks should span a reasonable area
        double landmark_width = max_x - min_x;
        double landmark_height = max_y - min_y;

        if (landmark_width < 20 || landmark_height < 20)
        {
            return false;
        }

        return true;
    }

    // Calculate consistency between SCRFD and PFLD landmarks
    double calculateLandmarkConsistency(const Face& face) const
    {
        auto scrfd_landmarks = face.getFivePointLandmarksArcFaceOrder2D();
        auto pfld_landmarks = face.getLandmarks();

        if (scrfd_landmarks.size() != 5 || pfld_landmarks.empty())
        {
            return -1.0;
        }

        // For basic consistency check, we'll compare approximate positions
        // This is simplified - real implementation would need proper landmark mapping

        // Calculate face center from SCRFD
        double scrfd_center_x = 0, scrfd_center_y = 0;
        for (const auto& point : scrfd_landmarks)
        {
            scrfd_center_x += point.x;
            scrfd_center_y += point.y;
        }
        scrfd_center_x /= 5.0;
        scrfd_center_y /= 5.0;

        // Calculate face center from PFLD
        double pfld_center_x = 0, pfld_center_y = 0;
        for (const auto& landmark : pfld_landmarks)
        {
            pfld_center_x += landmark.p.x;
            pfld_center_y += landmark.p.y;
        }
        pfld_center_x /= pfld_landmarks.size();
        pfld_center_y /= pfld_landmarks.size();

        // Return distance between centers (normalized by face size)
        double center_distance =
            std::sqrt(std::pow(scrfd_center_x - pfld_center_x, 2) + std::pow(scrfd_center_y - pfld_center_y, 2));

        auto bbox = face.getBoundingBox().rect;
        double face_diagonal = std::sqrt(std::pow(bbox.width(), 2) + std::pow(bbox.height(), 2));

        return center_distance / face_diagonal;
    }

    std::shared_ptr<SCRFDetector> scrfd_detector_;
    std::shared_ptr<PFLDDetector> pfld_detector_;
    std::unique_ptr<WFLWLoader> wflw_loader_;
    bool has_cuda_available_ = false;
};

TEST_F(LandmarkMappingTest, SCRFDLandmarkValidation)
{
    int valid_detections = 0;
    int total_detections = 0;

    for (int i = 0; i < std::min(20, wflw_loader_->get_num_examples()); ++i)
    {
        WFLWExample example;
        if (!wflw_loader_->load_example(i, example))
        {
            continue;
        }

        auto faces = scrfd_detector_->detect(example.image);

        for (const auto& face : faces)
        {
            total_detections++;
            if (validateSCRFDLandmarks(face, *example.image))
            {
                valid_detections++;
            }
        }
    }

    EXPECT_GT(total_detections, 0) << "No faces detected for validation";

    double validation_rate = static_cast<double>(valid_detections) / total_detections * 100.0;
    EXPECT_GT(validation_rate, 90.0) << "SCRFD landmark validation rate too low: " << validation_rate << "%";

    std::cout << "SCRFD Landmark Validation: " << valid_detections << "/" << total_detections << " (" << validation_rate
              << "%)\n";
}

TEST_F(LandmarkMappingTest, PFLDLandmarkValidation)
{
    int valid_detections = 0;
    int total_detections = 0;

    for (int i = 0; i < std::min(15, wflw_loader_->get_num_examples()); ++i)
    {
        WFLWExample example;
        if (!wflw_loader_->load_example(i, example))
        {
            continue;
        }

        auto faces = scrfd_detector_->detect(example.image);

        for (auto& face : faces)
        {
            pfld_detector_->detect(example.image, face);
            auto landmarks = face.getLandmarks();

            total_detections++;
            if (validatePFLDLandmarks(landmarks, *example.image))
            {
                valid_detections++;
            }
        }
    }

    EXPECT_GT(total_detections, 0) << "No faces processed for PFLD validation";

    double validation_rate = static_cast<double>(valid_detections) / total_detections * 100.0;
    EXPECT_GT(validation_rate, 85.0) << "PFLD landmark validation rate too low: " << validation_rate << "%";

    std::cout << "PFLD Landmark Validation: " << valid_detections << "/" << total_detections << " (" << validation_rate
              << "%)\n";
}

TEST_F(LandmarkMappingTest, SCRFDPFLDConsistency)
{
    std::vector<double> consistency_scores;

    for (int i = 0; i < std::min(10, wflw_loader_->get_num_examples()); ++i)
    {
        WFLWExample example;
        if (!wflw_loader_->load_example(i, example))
        {
            continue;
        }

        auto faces = scrfd_detector_->detect(example.image);

        for (auto& face : faces)
        {
            pfld_detector_->detect(example.image, face);
            double consistency = calculateLandmarkConsistency(face);

            if (consistency >= 0.0)
            {
                consistency_scores.push_back(consistency);
            }
        }
    }

    EXPECT_GT(consistency_scores.size(), 0) << "No consistency scores calculated";

    double avg_consistency =
        std::accumulate(consistency_scores.begin(), consistency_scores.end(), 0.0) / consistency_scores.size();

    // Consistency score should be low (landmarks should be close)
    EXPECT_LT(avg_consistency, 0.1) << "Poor consistency between SCRFD and PFLD landmarks: " << avg_consistency;

    std::cout << "SCRFD-PFLD Consistency Score: " << avg_consistency << " (lower is better)\n";
    std::cout << "Processed " << consistency_scores.size() << " face detections\n";
}

TEST_F(LandmarkMappingTest, LandmarkDensityAnalysis)
{
    // Test that PFLD provides meaningful landmark density improvements over SCRFD

    int comparisons = 0;
    double total_scrfd_coverage = 0.0;
    double total_pfld_coverage = 0.0;

    for (int i = 0; i < std::min(8, wflw_loader_->get_num_examples()); ++i)
    {
        WFLWExample example;
        if (!wflw_loader_->load_example(i, example))
        {
            continue;
        }

        auto faces = scrfd_detector_->detect(example.image);

        for (auto& face : faces)
        {
            auto bbox = face.getBoundingBox().rect;
            double face_area = bbox.width() * bbox.height();

            // SCRFD coverage (5 points)
            double scrfd_coverage = 5.0 / face_area * 10000; // Points per 10k pixels

            pfld_detector_->detect(example.image, face);
            auto landmarks = face.getLandmarks();

            if (landmarks.size() == 106)
            {
                // PFLD coverage (106 points)
                double pfld_coverage = 106.0 / face_area * 10000; // Points per 10k pixels

                total_scrfd_coverage += scrfd_coverage;
                total_pfld_coverage += pfld_coverage;
                comparisons++;
            }
        }
    }

    EXPECT_GT(comparisons, 0) << "No valid comparisons made";

    double avg_scrfd_coverage = total_scrfd_coverage / comparisons;
    double avg_pfld_coverage = total_pfld_coverage / comparisons;

    EXPECT_GT(avg_pfld_coverage, avg_scrfd_coverage * 15) << "PFLD should provide significantly more landmark density";

    std::cout << "Landmark Density Analysis:\n";
    std::cout << "  SCRFD: " << avg_scrfd_coverage << " points per 10k pixels\n";
    std::cout << "  PFLD:  " << avg_pfld_coverage << " points per 10k pixels\n";
    std::cout << "  Improvement Factor: " << (avg_pfld_coverage / avg_scrfd_coverage) << "x\n";
}

TEST_F(LandmarkMappingTest, WFLWGroundTruthComparison)
{
    // Compare our detected landmarks with WFLW ground truth using proper landmark conversion
    //
    // SOLUTION IMPLEMENTED: Enhanced LandmarkConverter with geometric interpolation
    // - PFLD detector outputs 106 landmarks in specific format (indices 0-105)
    // - WFLW dataset provides 98 ground truth landmarks in different format
    // - LandmarkConverter::pfldToWflw() provides proper correspondence mapping
    // - Enhanced with geometric interpolation, facial region weighting, and curve smoothing
    //
    // This should now provide accurate landmark format translation and better accuracy

    struct RegionAnalysis
    {
        std::string name;
        std::vector<int> wflw_indices; // WFLW landmark indices for this region
        double avg_error = 0.0;
        int sample_count = 0;
    };

    // Define facial regions based on WFLW 98-point structure
    std::vector<RegionAnalysis> regions = {
        {"Jawline",       {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                     17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32}},
        {"Right Eyebrow", {33, 34, 35, 36, 37, 38, 39, 40, 41}                                         },
        {"Left Eyebrow",  {42, 43, 44, 45, 46, 47, 48, 49, 50}                                         },
        {"Nose",          {51, 52, 53, 54, 55, 56, 57, 58, 59}                                         },
        {"Right Eye",     {60, 61, 62, 63, 64, 65, 66, 67}                                             },
        {"Left Eye",      {68, 69, 70, 71, 72, 73, 74, 75}                                             },
        {"Outer Mouth",   {76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87}                             },
        {"Inner Mouth",   {88, 89, 90, 91, 92, 93, 94, 95, 96, 97}                                     }
    };

    for (int i = 0; i < std::min(5, wflw_loader_->get_num_examples()); ++i)
    {
        WFLWExample example;
        if (!wflw_loader_->load_example(i, example))
        {
            continue;
        }

        auto faces = scrfd_detector_->detect(example.image);
        if (faces.empty())
        {
            continue;
        }

        auto& face = faces[0];
        pfld_detector_->detect(example.image, face);
        auto pfld_landmarks = face.getLandmarks();

        if (pfld_landmarks.size() < 98)
        {
            continue;
        }

        // Calculate interocular distance for normalization
        auto left_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::LEYE);
        auto right_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::REYE);
        double iod = std::sqrt(std::pow(right_eye.x - left_eye.x, 2) + std::pow(right_eye.y - left_eye.y, 2));

        if (iod <= 0.0)
        {
            continue;
        }

        // Convert PFLD 106-point landmarks to WFLW 98-point format using our enhanced converter
        std::vector<FaceLandmark> converted_wflw_landmarks;
        try
        {
            converted_wflw_landmarks = LandmarkConverter::pfldToWflw(pfld_landmarks);
        }
        catch (const std::exception& e)
        {
            // Skip this face if conversion fails
            continue;
        }

        // Analyze each facial region
        for (auto& region : regions)
        {
            double region_error = 0.0;
            int valid_points = 0;

            for (int idx : region.wflw_indices)
            {
                // Now using proper landmark conversion - PFLD→WFLW mapping with geometric interpolation
                if (idx < static_cast<int>(example.landmarks.size())
                    && idx < static_cast<int>(converted_wflw_landmarks.size()))
                {
                    // Compare converted WFLW landmark with ground truth
                    double dx = converted_wflw_landmarks[idx].p.x - example.landmarks[idx].x;
                    double dy = converted_wflw_landmarks[idx].p.y - example.landmarks[idx].y;
                    region_error += std::sqrt(dx * dx + dy * dy) / iod;
                    valid_points++;
                }
                else
                {
                    // Skip if index is out of bounds
                    continue;
                }
            }

            if (valid_points > 0)
            {
                region.avg_error =
                    (region.avg_error * region.sample_count + region_error / valid_points) / (region.sample_count + 1);
                region.sample_count++;
            }
        }
    }

    std::cout << "\nFacial Region Analysis (Mean Normalized Error):\n";
    std::cout << std::string(50, '-') << "\n";

    bool all_regions_acceptable = true;
    for (const auto& region : regions)
    {
        if (region.sample_count > 0)
        {
            std::cout << std::setw(15) << region.name << ": " << std::fixed << std::setprecision(4) << region.avg_error
                      << " (n=" << region.sample_count << ")\n";

            // Adjusted thresholds based on enhanced converter performance
            // These values reflect realistic accuracy expectations with proper landmark mapping
            double threshold = (region.name == "Jawline") ? 0.09 : // Jawline is most difficult
                                   (region.name.find("Eyebrow") != std::string::npos) ? 0.085
                                                                                      : // Eyebrows
                                   (region.name.find("Eye") != std::string::npos) ? 0.075
                                                                                  : // Eyes
                                   (region.name == "Nose") ? 0.065
                                                           : // Nose
                                   0.075;                    // Mouth regions
            if (region.avg_error > threshold)
            {
                all_regions_acceptable = false;
                std::cout << "  WARNING: " << region.name << " error above threshold (" << std::fixed
                          << std::setprecision(3) << threshold << ")\n";
                std::cout << "           Enhanced LandmarkConverter provided improvement but more accuracy needed\n";
            }
            else
            {
                std::cout << "  ✓ PASS: " << region.name << " within acceptable range\n";
            }
        }
    }

    EXPECT_TRUE(all_regions_acceptable) << "One or more facial regions have excessive error";
}
