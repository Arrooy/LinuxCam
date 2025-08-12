#include "wflw_loader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <numeric>
#include <onnxruntime_cxx_api.h>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/face.h"
#include "LinuxFace/onnx/pfld.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/profiler.h"
#include "config.hpp"

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

class WFLWIntegrationTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Ensure we can load the test configuration file
        // Try test-specific config first, then fallback to main config
        std::string config_paths[] = {"tests/wflw_integration/test_config.yaml",
                                      "../tests/wflw_integration/test_config.yaml", "config.yaml", "../config.yaml"};
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

        ASSERT_TRUE(config_loaded) << "Could not find test_config.yaml or config.yaml in expected paths";

        // Check CUDA availability for conditional testing
        has_cuda_available_ = checkCudaAvailability();

        // Initialize models
        std::string models_folder = Config::getInstance().getModelFolderPath();

        // SCRFD for face detection (fast model for testing)
        std::string scrfd_path = models_folder + "scrfd_500m_bnkps_shape640x640.onnx";
        scrfd_detector_ = std::make_shared<SCRFDetector>(scrfd_path);
        ASSERT_TRUE(scrfd_detector_->isReady()) << "SCRFD detector failed to initialize. Expected path: " << scrfd_path;

        // PFLD for landmarks
        std::string pfld_path = models_folder + "pfld-106-v3.onnx";
        pfld_detector_ = std::make_shared<PFLDDetector>(pfld_path);
        ASSERT_TRUE(pfld_detector_->isReady()) << "PFLD detector failed to initialize. Expected path: " << pfld_path;

        // Load WFLW dataset for testing
        const std::string wflw_base = Config::getInstance().getWFLWFolderPath();
        const std::string test_annotations =
            wflw_base + "/WFLW_annotations/list_98pt_rect_attr_train_test/list_98pt_rect_attr_test.txt";

        // Check if WFLW dataset is available
        if (!std::ifstream(test_annotations).good())
        {
            GTEST_SKIP() << "WFLW dataset not found at: " << test_annotations << "\n"
                         << "Please run: ./scripts/download_wflw_dataset.sh\n"
                         << "Or set WFLW_folder_path in config to correct location";
        }

        // Load small subset for testing (adjust size as needed)
        wflw_loader_ = std::make_unique<WFLWLoader>(test_annotations, 50);
        ASSERT_GT(wflw_loader_->get_num_examples(), 0) << "No WFLW examples loaded from: " << test_annotations;
    }

    void TearDown() override
    {
        // Clean up if needed
    }

    // Calculate Mean Normalized Error for 98-point landmarks
    double calculateMNE(const std::vector<FaceLandmark>& predicted_landmarks,
                        const std::vector<math_utils::Point<double>>& ground_truth, double interocular_distance) const
    {
        if (predicted_landmarks.size() != ground_truth.size() || interocular_distance <= 0.0)
        {
            return -1.0; // Invalid input
        }

        double error_sum = 0.0;
        for (size_t i = 0; i < predicted_landmarks.size(); ++i)
        {
            double dx = predicted_landmarks[i].p.x - ground_truth[i].x;
            double dy = predicted_landmarks[i].p.y - ground_truth[i].y;
            error_sum += std::sqrt(dx * dx + dy * dy);
        }

        return (error_sum / predicted_landmarks.size()) / interocular_distance;
    }

    // Calculate interocular distance from SCRFD 5-point landmarks
    double calculateInterocularDistance(const Face& face) const
    {
        auto left_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::LEYE);
        auto right_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::REYE);

        double dx = right_eye.x - left_eye.x;
        double dy = right_eye.y - left_eye.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Benchmark metrics calculation
    struct BenchmarkResults
    {
        double mean_mne = 0.0;
        double median_mne = 0.0;
        double std_dev_mne = 0.0;
        double success_rate = 0.0; // Percentage of successful detections
        int total_samples = 0;
        int successful_detections = 0;
        std::vector<double> individual_mne_scores;

        // Performance metrics
        double avg_scrfd_time_ms = 0.0;
        double avg_pfld_time_ms = 0.0;
        double total_pipeline_time_ms = 0.0;
    };

    BenchmarkResults runBenchmarkOnSubset(const std::vector<int>& example_indices) const
    {
        BenchmarkResults results;
        results.total_samples = example_indices.size();
        results.individual_mne_scores.reserve(example_indices.size());

        double total_scrfd_time = 0.0;
        double total_pfld_time = 0.0;

        for (int idx : example_indices)
        {
            WFLWExample example;
            if (!wflw_loader_->load_example(idx, example))
            {
                continue;
            }

            // Face detection with SCRFD
            auto start_time = std::chrono::high_resolution_clock::now();
            auto detected_faces = scrfd_detector_->detect(example.image);
            auto scrfd_end = std::chrono::high_resolution_clock::now();

            auto scrfd_duration = std::chrono::duration_cast<std::chrono::microseconds>(scrfd_end - start_time);
            total_scrfd_time += scrfd_duration.count() / 1000.0; // Convert to ms

            if (detected_faces.empty())
            {
                continue; // No face detected
            }

            // Use the first (highest confidence) detected face
            Face& face = detected_faces[0];

            // Landmark detection with PFLD
            auto pfld_start = std::chrono::high_resolution_clock::now();
            pfld_detector_->detect(example.image, face);
            auto pfld_end = std::chrono::high_resolution_clock::now();

            auto pfld_duration = std::chrono::duration_cast<std::chrono::microseconds>(pfld_end - pfld_start);
            total_pfld_time += pfld_duration.count() / 1000.0; // Convert to ms

            auto predicted_landmarks = face.getLandmarks();

            // Skip if we don't have 106 landmarks (PFLD should detect 106)
            if (predicted_landmarks.size() != 106)
            {
                continue;
            }

            // Calculate interocular distance for normalization
            double iod = calculateInterocularDistance(face);
            if (iod <= 0.0)
            {
                continue;
            }

            // Note: WFLW has 98 landmarks, PFLD detects 106
            // We need to map or subset the landmarks appropriately
            // For now, let's use the first 98 landmarks from PFLD
            std::vector<FaceLandmark> pfld_98_landmarks(predicted_landmarks.begin(), predicted_landmarks.begin() + 98);

            double mne = calculateMNE(pfld_98_landmarks, example.landmarks, iod);
            if (mne >= 0.0)
            {
                results.individual_mne_scores.push_back(mne);
                results.successful_detections++;
            }
        }

        // Calculate statistics
        if (!results.individual_mne_scores.empty())
        {
            // Mean
            results.mean_mne =
                std::accumulate(results.individual_mne_scores.begin(), results.individual_mne_scores.end(), 0.0)
                / results.individual_mne_scores.size();

            // Median
            std::vector<double> sorted_scores = results.individual_mne_scores;
            std::sort(sorted_scores.begin(), sorted_scores.end());
            size_t mid = sorted_scores.size() / 2;
            if (sorted_scores.size() % 2 == 0)
            {
                results.median_mne = (sorted_scores[mid - 1] + sorted_scores[mid]) / 2.0;
            }
            else
            {
                results.median_mne = sorted_scores[mid];
            }

            // Standard deviation
            double variance = 0.0;
            for (double score : results.individual_mne_scores)
            {
                variance += std::pow(score - results.mean_mne, 2);
            }
            results.std_dev_mne = std::sqrt(variance / results.individual_mne_scores.size());
        }

        results.success_rate = (static_cast<double>(results.successful_detections) / results.total_samples) * 100.0;
        results.avg_scrfd_time_ms = total_scrfd_time / results.total_samples;
        results.avg_pfld_time_ms = total_pfld_time / results.total_samples;
        results.total_pipeline_time_ms = total_scrfd_time + total_pfld_time;

        return results;
    }

    std::shared_ptr<SCRFDetector> scrfd_detector_;
    std::shared_ptr<PFLDDetector> pfld_detector_;
    std::unique_ptr<WFLWLoader> wflw_loader_;
    bool has_cuda_available_ = false;
};

// Basic functionality tests
TEST_F(WFLWIntegrationTest, BasicDetectorInitialization)
{
    EXPECT_TRUE(scrfd_detector_->isReady());
    EXPECT_TRUE(pfld_detector_->isReady());
    EXPECT_GT(wflw_loader_->get_num_examples(), 0);
}

TEST_F(WFLWIntegrationTest, ConfigurationValidation)
{
    // Verify config was loaded properly
    EXPECT_FALSE(Config::getInstance().getModelFolderPath().empty());
    EXPECT_FALSE(Config::getInstance().getWFLWFolderPath().empty());

    // Check GPU configuration - should be disabled for tests
    EXPECT_FALSE(Config::getInstance().isGPUEnabled()) << "GPU should be disabled in test configuration";

    // Report CUDA availability for diagnostic purposes
    if (has_cuda_available_)
    {
        std::cout << "INFO: CUDA Execution Provider is available but disabled by config" << std::endl;
    }
    else
    {
        std::cout << "INFO: CUDA Execution Provider is not available" << std::endl;
    }
}

TEST_F(WFLWIntegrationTest, SingleImagePipeline)
{
    WFLWExample example;
    ASSERT_TRUE(wflw_loader_->load_example(0, example));
    ASSERT_TRUE(example.image != nullptr);

    // Face detection
    auto detected_faces = scrfd_detector_->detect(example.image);
    EXPECT_GT(detected_faces.size(), 0) << "No faces detected in test image";

    if (!detected_faces.empty())
    {
        Face& face = detected_faces[0];

        // Landmark detection
        pfld_detector_->detect(example.image, face);
        auto landmarks = face.getLandmarks();

        EXPECT_GT(landmarks.size(), 0) << "No landmarks detected";
        EXPECT_EQ(landmarks.size(), 106) << "PFLD should detect 106 landmarks";

        // Calculate basic metrics
        double iod = calculateInterocularDistance(face);
        EXPECT_GT(iod, 0.0) << "Invalid interocular distance";

        // Basic sanity check: landmarks should be within image bounds
        for (const auto& landmark : landmarks)
        {
            EXPECT_GE(landmark.p.x, 0.0);
            EXPECT_LT(landmark.p.x, example.image->info.width);
            EXPECT_GE(landmark.p.y, 0.0);
            EXPECT_LT(landmark.p.y, example.image->info.height);
        }
    }
}

// Performance benchmarking tests
TEST_F(WFLWIntegrationTest, BenchmarkNormalConditions)
{
    // Test on images with normal conditions (pose, expression, etc.)
    auto normal_indices = wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true);

    if (normal_indices.empty())
    {
        GTEST_SKIP() << "No normal condition images found in dataset";
    }

    // Limit to reasonable number for testing
    if (normal_indices.size() > 20)
    {
        normal_indices.resize(20);
    }

    auto results = runBenchmarkOnSubset(normal_indices);

    EXPECT_GT(results.success_rate, 80.0) << "Low success rate on normal conditions: " << results.success_rate << "%";
    EXPECT_LT(results.mean_mne, 0.05) << "High mean error on normal conditions: " << results.mean_mne;

    std::cout << "\n=== Normal Conditions Benchmark ===\n";
    std::cout << "Success Rate: " << results.success_rate << "%\n";
    std::cout << "Mean MNE: " << results.mean_mne << "\n";
    std::cout << "Median MNE: " << results.median_mne << "\n";
    std::cout << "Std Dev MNE: " << results.std_dev_mne << "\n";
    std::cout << "Avg SCRFD Time: " << results.avg_scrfd_time_ms << " ms\n";
    std::cout << "Avg PFLD Time: " << results.avg_pfld_time_ms << " ms\n";
    std::cout << "Total Pipeline Time: " << results.total_pipeline_time_ms << " ms\n";
}

TEST_F(WFLWIntegrationTest, BenchmarkChallengingConditions)
{
    // Test on challenging conditions (pose variations, occlusions, etc.)
    auto challenging_indices = wflw_loader_->getExamplesByAttribute(false, false, false, false, false, false);

    if (challenging_indices.empty())
    {
        GTEST_SKIP() << "No challenging condition images found in dataset";
    }

    // Limit to reasonable number for testing
    if (challenging_indices.size() > 15)
    {
        challenging_indices.resize(15);
    }

    auto results = runBenchmarkOnSubset(challenging_indices);

    // More lenient thresholds for challenging conditions
    EXPECT_GT(results.success_rate, 50.0)
        << "Very low success rate on challenging conditions: " << results.success_rate << "%";
    EXPECT_LT(results.mean_mne, 0.10) << "Very high mean error on challenging conditions: " << results.mean_mne;

    std::cout << "\n=== Challenging Conditions Benchmark ===\n";
    std::cout << "Success Rate: " << results.success_rate << "%\n";
    std::cout << "Mean MNE: " << results.mean_mne << "\n";
    std::cout << "Median MNE: " << results.median_mne << "\n";
    std::cout << "Std Dev MNE: " << results.std_dev_mne << "\n";
    std::cout << "Avg SCRFD Time: " << results.avg_scrfd_time_ms << " ms\n";
    std::cout << "Avg PFLD Time: " << results.avg_pfld_time_ms << " ms\n";
    std::cout << "Total Pipeline Time: " << results.total_pipeline_time_ms << " ms\n";
}

// Regression test to ensure performance doesn't degrade
TEST_F(WFLWIntegrationTest, PerformanceRegression)
{
    // Get first 10 examples for consistent regression testing
    std::vector<int> test_indices;
    for (int i = 0; i < std::min(10, wflw_loader_->get_num_examples()); ++i)
    {
        test_indices.push_back(i);
    }

    auto results = runBenchmarkOnSubset(test_indices);

    // These thresholds should be updated based on your performance requirements
    EXPECT_LT(results.avg_scrfd_time_ms, 10.0) << "SCRFD detection too slow: " << results.avg_scrfd_time_ms << " ms";
    EXPECT_LT(results.avg_pfld_time_ms, 50.0) << "PFLD detection too slow: " << results.avg_pfld_time_ms << " ms";
    EXPECT_GT(results.success_rate, 70.0) << "Success rate below regression threshold: " << results.success_rate << "%";

    std::cout << "\n=== Performance Regression Test ===\n";
    std::cout << "Pipeline meets performance requirements\n";
    std::cout << "Success Rate: " << results.success_rate << "%\n";
    std::cout << "Total Pipeline Time: " << results.total_pipeline_time_ms << " ms\n";
}

// Test specific attributes
TEST_F(WFLWIntegrationTest, TestPoseVariations)
{
    auto pose_indices = wflw_loader_->getExamplesByAttribute(false, true, true, true, true, true);

    if (pose_indices.empty())
    {
        GTEST_SKIP() << "No pose variation images found";
    }

    if (pose_indices.size() > 10)
    {
        pose_indices.resize(10);
    }

    auto results = runBenchmarkOnSubset(pose_indices);

    std::cout << "\n=== Pose Variations Test ===\n";
    std::cout << "Success Rate: " << results.success_rate << "%\n";
    std::cout << "Mean MNE: " << results.mean_mne << "\n";

    // Pose variations typically have higher error rates
    EXPECT_GT(results.success_rate, 40.0) << "Very poor performance on pose variations";
}

// Edge case tests
TEST_F(WFLWIntegrationTest, EmptyImageHandling)
{
    auto empty_image = std::make_unique<Image>();

    auto faces = scrfd_detector_->detect(empty_image);
    EXPECT_TRUE(faces.empty()) << "Should not detect faces in empty image";
}

TEST_F(WFLWIntegrationTest, MultipleFactorsDetection)
{
    // Test images with multiple challenging factors
    int tested_count = 0;
    int success_count = 0;

    for (int i = 0; i < std::min(20, wflw_loader_->get_num_examples()); ++i)
    {
        WFLWExample example;
        if (!wflw_loader_->load_example(i, example))
        {
            continue;
        }

        // Count challenging factors
        int challenging_factors = 0;
        if (!example.isNormalPose())
        {
            challenging_factors++;
        }
        if (!example.isNormalExpression())
        {
            challenging_factors++;
        }
        if (!example.isNormalIllumination())
        {
            challenging_factors++;
        }
        if (!example.hasNoMakeup())
        {
            challenging_factors++;
        }
        if (!example.hasNoOcclusion())
        {
            challenging_factors++;
        }
        if (!example.isClear())
        {
            challenging_factors++;
        }

        if (challenging_factors >= 3) // Images with 3+ challenging factors
        {
            tested_count++;

            auto faces = scrfd_detector_->detect(example.image);
            if (!faces.empty())
            {
                Face& face = faces[0];
                pfld_detector_->detect(example.image, face);
                auto landmarks = face.getLandmarks();

                if (landmarks.size() == 106)
                {
                    success_count++;
                }
            }
        }
    }

    if (tested_count > 0)
    {
        double success_rate = (static_cast<double>(success_count) / tested_count) * 100.0;
        std::cout << "\n=== Multiple Challenging Factors Test ===\n";
        std::cout << "Tested " << tested_count << " images with 3+ challenging factors\n";
        std::cout << "Success Rate: " << success_rate << "%\n";

        EXPECT_GT(success_rate, 20.0) << "Very poor performance on multiple challenging factors";
    }
    else
    {
        GTEST_SKIP() << "No images with multiple challenging factors found";
    }
}
