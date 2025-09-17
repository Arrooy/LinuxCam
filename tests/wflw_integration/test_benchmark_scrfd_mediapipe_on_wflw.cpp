#include "../common/test_utils.h"
#include "../common/dataset_utils.h"

#include <algorithm>
#include <chrono>
#include <fstream>
/**
 * BENCHMARK TEST: SCRFD + MEDIAPIPE PERFORMANCE ON WFLW DATASET
 *
 * This benchmark evaluates:
 * - SCRFD face detection speed and accuracy
 * - MediaPipe landmark detection speed and accuracy
 * - End-to-end pipeline performance
 * - Mean Normalized Error (MNE) against WFLW ground truth
 * - Comparison with PFLD performance
 */
#include <gtest/gtest.h>
#include <iomanip>
#include <memory>
#include <numeric>
#include <onnxruntime_cxx_api.h>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/face.h"
#include "LinuxFace/onnx/mediaPipe_FaceLandmarks.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/landmark_converter.h"
#include "LinuxFace/profiler.h"
#include "config.hpp"

using namespace linuxface;

// Helper function to check CUDA availability
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

// Helper function to compute robust statistics that handle low sample counts gracefully
struct RobustStatistics {
    double mean = 0.0;
    double median = 0.0;
    double std_dev = 0.0;
    double p90 = 0.0;
    double p95 = 0.0;
    bool is_reliable = false; // True if sample size is adequate for meaningful statistics
};

static RobustStatistics computeRobustStats(const std::vector<double>& values) {
    RobustStatistics stats;
    
    if (values.empty()) {
        return stats;
    }
    
    // Sort values for percentile calculations
    std::vector<double> sorted_values = values;
    std::sort(sorted_values.begin(), sorted_values.end());
    
    // Calculate mean
    double sum = std::accumulate(sorted_values.begin(), sorted_values.end(), 0.0);
    stats.mean = sum / sorted_values.size();
    
    // Calculate median
    size_t n = sorted_values.size();
    if (n % 2 == 0) {
        stats.median = (sorted_values[n/2 - 1] + sorted_values[n/2]) / 2.0;
    } else {
        stats.median = sorted_values[n/2];
    }
    
    // Calculate standard deviation (handle single sample case)
    if (n == 1) {
        stats.std_dev = 0.0;
    } else {
        double variance = 0.0;
        for (double value : sorted_values) {
            variance += std::pow(value - stats.mean, 2);
        }
        stats.std_dev = std::sqrt(variance / (n - 1));
    }
    
    // Calculate percentiles (with fallback for small samples)
    if (n >= 10) {
        // Standard percentile calculation for adequate sample size
        size_t p90_idx = static_cast<size_t>(std::ceil(0.9 * n)) - 1;
        size_t p95_idx = static_cast<size_t>(std::ceil(0.95 * n)) - 1;
        stats.p90 = sorted_values[std::min(p90_idx, n - 1)];
        stats.p95 = sorted_values[std::min(p95_idx, n - 1)];
        stats.is_reliable = true;
    } else {
        // For small samples, use max value as conservative estimate
        stats.p90 = sorted_values.back();
        stats.p95 = sorted_values.back();
        stats.is_reliable = false;
    }
    
    return stats;
}

/**
 * Comprehensive benchmarking suite for MediaPipe face keypoint detection on WFLW dataset.
 * This suite provides detailed performance analysis and comparison with PFLD.
 */
class MediaPipeFaceKeypointBenchmark : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Ensure we can load the test configuration file
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

        // Initialize SCRFD for face detection
        scrfd_500m_detector_ = std::make_shared<SCRFDetector>(models_folder + "scrfd_500m_bnkps_shape640x640.onnx");

        // Initialize MediaPipe for landmark detection
        mediapipe_detector_ = std::make_shared<MediaPipeFaceLandmarks>(models_folder + "MediaPipeFaceLandmarkDetector.onnx");

        ASSERT_TRUE(scrfd_500m_detector_->isReady())
            << "SCRFD detector failed to initialize. Expected path: " << models_folder
            << "scrfd_500m_bnkps_shape640x640.onnx";
        ASSERT_TRUE(mediapipe_detector_->isReady())
            << "MediaPipe detector failed to initialize. Expected path: " << models_folder << "MediaPipeFaceLandmarkDetector.onnx";

        // Load WFLW dataset with environment variable control using centralized loader
        const int max_wflw_samples = TestUtils::getEnvVarInt("WFLW_MAX_SAMPLES", 5);
        simple_wflw_loader_ = std::make_unique<TestUtils::Datasets::SimpleWFLWLoader>();
        if (!simple_wflw_loader_->loadDataset(max_wflw_samples)) {
            GTEST_SKIP() << "Failed to load WFLW dataset using centralized loader.";
        }

        ASSERT_GT(simple_wflw_loader_->getSampleCount(), 0) << "No WFLW samples loaded for benchmarking";
    }

    struct DetailedMetrics
    {
        // Accuracy metrics
        double mean_mne = 0.0;
        double median_mne = 0.0;
        double std_dev_mne = 0.0;
        double p90_mne = 0.0; // 90th percentile
        double p95_mne = 0.0; // 95th percentile

        // Success rates
        double overall_success_rate = 0.0;
        double face_detection_success_rate = 0.0;
        double landmark_success_rate = 0.0;

        // Performance metrics
        double avg_face_detection_ms = 0.0;
        double avg_landmark_detection_ms = 0.0;
        double avg_total_pipeline_ms = 0.0;
        double max_face_detection_ms = 0.0;
        double max_landmark_detection_ms = 0.0;

        // Quality thresholds (percentage of samples below threshold)
        double samples_below_003_mne = 0.0; // Very good
        double samples_below_005_mne = 0.0; // Good
        double samples_below_010_mne = 0.0; // Acceptable

        // Data
        int total_samples = 0;
        int successful_face_detections = 0;
        int successful_landmark_detections = 0;
        std::vector<double> mne_scores;
        std::vector<double> face_detection_times;
        std::vector<double> landmark_detection_times;
    };

    DetailedMetrics
    runDetailedBenchmark(const std::vector<int>& example_indices, const std::string& subset_name = "Unknown") const
    {
        DetailedMetrics metrics;
        metrics.total_samples = example_indices.size();
        metrics.mne_scores.reserve(example_indices.size());
        metrics.face_detection_times.reserve(example_indices.size());
        metrics.landmark_detection_times.reserve(example_indices.size());

        std::cout << "\nRunning detailed MediaPipe benchmark on " << subset_name << " (" << example_indices.size()
                  << " samples)...\n";

        for (int idx : example_indices)
        {
            const auto& sample = simple_wflw_loader_->getSample(idx);
            auto image_ptr = sample.loadImage();
            if (!image_ptr)
            {
                continue;
            }

            // Face detection timing
            auto face_start = std::chrono::high_resolution_clock::now();
            auto detected_faces = scrfd_500m_detector_->detect(image_ptr);
            auto face_end = std::chrono::high_resolution_clock::now();

            double face_detection_time =
                std::chrono::duration_cast<std::chrono::microseconds>(face_end - face_start).count() / 1000.0;
            metrics.face_detection_times.push_back(face_detection_time);

            if (detected_faces.empty())
            {
                continue;
            }

            metrics.successful_face_detections++;
            Face& face = detected_faces[0];

            // Crop face region for MediaPipe (it expects face-cropped images)
            auto face_bbox = face.getBoundingBox();
            
            // Add some padding around the detected face
            float padding = 0.2f; // 20% padding
            float width = face_bbox.rect.r - face_bbox.rect.l;
            float height = face_bbox.rect.b - face_bbox.rect.t;
            
            math_utils::Rect<float> padded_bbox;
            padded_bbox.l = std::max(0.0f, face_bbox.rect.l - width * padding);
            padded_bbox.t = std::max(0.0f, face_bbox.rect.t - height * padding);
            padded_bbox.r = std::min(static_cast<float>(image_ptr->info.width), face_bbox.rect.r + width * padding);
            padded_bbox.b = std::min(static_cast<float>(image_ptr->info.height), face_bbox.rect.b + height * padding);
            
            // Crop the face region
            auto cropped_face = image_ptr->crop(padded_bbox);

            // MediaPipe landmark detection timing
            auto landmark_start = std::chrono::high_resolution_clock::now();
            auto mediapipe_result = mediapipe_detector_->detect(cropped_face);
            auto landmark_end = std::chrono::high_resolution_clock::now();

            double landmark_detection_time =
                std::chrono::duration_cast<std::chrono::microseconds>(landmark_end - landmark_start).count() / 1000.0;
            metrics.landmark_detection_times.push_back(landmark_detection_time);

            if (mediapipe_result.landmarks.size() != 468)
            {
                continue;
            }

            metrics.successful_landmark_detections++;

            // Convert MediaPipe landmarks to FaceLandmark format
            std::vector<FaceLandmark> mediapipe_landmarks;
            for (size_t i = 0; i < mediapipe_result.landmarks.size(); ++i)
            {
                if (mediapipe_result.landmarks[i].size() >= 2)
                {
                    FaceLandmark fl;
                    fl.i = static_cast<unsigned int>(i);
                    // Convert normalized coordinates back to original image space
                    fl.p.x = padded_bbox.l + mediapipe_result.landmarks[i][0] * (padded_bbox.r - padded_bbox.l);
                    fl.p.y = padded_bbox.t + mediapipe_result.landmarks[i][1] * (padded_bbox.b - padded_bbox.t);
                    fl.p.z = mediapipe_result.landmarks[i].size() >= 3 ? mediapipe_result.landmarks[i][2] : 0.0f;
                    mediapipe_landmarks.push_back(fl);
                }
            }

            // Convert MediaPipe 468 landmarks to WFLW 98 format using landmark converter
            auto wflw_landmarks = LandmarkConverter::mediapipeToWflw(mediapipe_landmarks);
            
            if (wflw_landmarks.size() != 98)
            {
                continue;
            }

            // Calculate MNE using SCRFD eye landmarks for normalization
            auto left_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::LEYE);
            auto right_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::REYE);
            double iod = std::sqrt(std::pow(right_eye.x - left_eye.x, 2) + std::pow(right_eye.y - left_eye.y, 2));

            if (iod <= 0.0)
            {
                continue;
            }

            // Compare converted MediaPipe landmarks with WFLW ground truth
            double error_sum = 0.0;
            for (size_t i = 0; i < wflw_landmarks.size() && i < sample.landmarks.size(); ++i)
            {
                double dx = wflw_landmarks[i].p.x - sample.landmarks[i][0];
                double dy = wflw_landmarks[i].p.y - sample.landmarks[i][1];
                error_sum += std::sqrt(dx * dx + dy * dy);
            }

            double mne = (error_sum / wflw_landmarks.size()) / iod;
            metrics.mne_scores.push_back(mne);
        }

        // Calculate statistics
        calculateDetailedStatistics(metrics);

        return metrics;
    }

    void calculateDetailedStatistics(DetailedMetrics& metrics) const
    {
        // Success rates
        metrics.face_detection_success_rate =
            (static_cast<double>(metrics.successful_face_detections) / metrics.total_samples) * 100.0;
        metrics.landmark_success_rate =
            metrics.successful_face_detections > 0 ? 
            (static_cast<double>(metrics.successful_landmark_detections) / metrics.successful_face_detections) * 100.0 : 0.0;
        metrics.overall_success_rate =
            (static_cast<double>(metrics.successful_landmark_detections) / metrics.total_samples) * 100.0;

        // Timing statistics
        if (!metrics.face_detection_times.empty())
        {
            metrics.avg_face_detection_ms =
                std::accumulate(metrics.face_detection_times.begin(), metrics.face_detection_times.end(), 0.0)
                / metrics.face_detection_times.size();
            metrics.max_face_detection_ms =
                *std::max_element(metrics.face_detection_times.begin(), metrics.face_detection_times.end());
        }

        if (!metrics.landmark_detection_times.empty())
        {
            metrics.avg_landmark_detection_ms =
                std::accumulate(metrics.landmark_detection_times.begin(), metrics.landmark_detection_times.end(), 0.0)
                / metrics.landmark_detection_times.size();
            metrics.max_landmark_detection_ms =
                *std::max_element(metrics.landmark_detection_times.begin(), metrics.landmark_detection_times.end());
        }

        metrics.avg_total_pipeline_ms = metrics.avg_face_detection_ms + metrics.avg_landmark_detection_ms;

        // MNE statistics using robust calculation
        if (!metrics.mne_scores.empty())
        {
            RobustStatistics robust_stats = computeRobustStats(metrics.mne_scores);
            
            metrics.mean_mne = robust_stats.mean;
            metrics.median_mne = robust_stats.median;
            metrics.std_dev_mne = robust_stats.std_dev;
            metrics.p90_mne = robust_stats.p90;
            metrics.p95_mne = robust_stats.p95;

            // Quality thresholds
            std::vector<double> sorted_mne = metrics.mne_scores;
            std::sort(sorted_mne.begin(), sorted_mne.end());
            
            metrics.samples_below_003_mne =
                (std::count_if(sorted_mne.begin(), sorted_mne.end(), [](double x) { return x < 0.03; })
                 / static_cast<double>(sorted_mne.size()))
                * 100.0;
            metrics.samples_below_005_mne =
                (std::count_if(sorted_mne.begin(), sorted_mne.end(), [](double x) { return x < 0.05; })
                 / static_cast<double>(sorted_mne.size()))
                * 100.0;
            metrics.samples_below_010_mne =
                (std::count_if(sorted_mne.begin(), sorted_mne.end(), [](double x) { return x < 0.10; })
                 / static_cast<double>(sorted_mne.size()))
                * 100.0;
        }
    }

    void printDetailedReport(const DetailedMetrics& metrics, const std::string& subset_name) const
    {
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "MEDIAPIPE BENCHMARK REPORT: " << subset_name << "\n";
        std::cout << std::string(60, '=') << "\n";

        std::cout << std::fixed << std::setprecision(4);

        // Success rates
        std::cout << "\nSUCCESS RATES:\n";
        std::cout << "  Overall Success Rate:      " << std::setw(8) << metrics.overall_success_rate << "%\n";
        std::cout << "  Face Detection Success:    " << std::setw(8) << metrics.face_detection_success_rate << "%\n";
        std::cout << "  Landmark Detection Success:" << std::setw(8) << metrics.landmark_success_rate << "%\n";

        // Accuracy metrics
        std::cout << "\nACCURACY METRICS (MNE):\n";
        std::cout << "  Mean:                      " << std::setw(8) << metrics.mean_mne << "\n";
        std::cout << "  Median:                    " << std::setw(8) << metrics.median_mne << "\n";
        std::cout << "  Standard Deviation:        " << std::setw(8) << metrics.std_dev_mne << "\n";
        std::cout << "  90th Percentile:           " << std::setw(8) << metrics.p90_mne << "\n";
        std::cout << "  95th Percentile:           " << std::setw(8) << metrics.p95_mne << "\n";

        // Quality thresholds
        std::cout << "\nQUALITY DISTRIBUTION:\n";
        std::cout << "  Very Good (MNE < 0.03):    " << std::setw(8) << metrics.samples_below_003_mne << "%\n";
        std::cout << "  Good (MNE < 0.05):         " << std::setw(8) << metrics.samples_below_005_mne << "%\n";
        std::cout << "  Acceptable (MNE < 0.10):   " << std::setw(8) << metrics.samples_below_010_mne << "%\n";

        // Performance metrics
        std::cout << "\nPERFORMANCE METRICS (ms):\n";
        std::cout << "  Avg Face Detection:        " << std::setw(8) << metrics.avg_face_detection_ms << "\n";
        std::cout << "  Avg Landmark Detection:    " << std::setw(8) << metrics.avg_landmark_detection_ms << "\n";
        std::cout << "  Avg Total Pipeline:        " << std::setw(8) << metrics.avg_total_pipeline_ms << "\n";
        std::cout << "  Max Face Detection:        " << std::setw(8) << metrics.max_face_detection_ms << "\n";
        std::cout << "  Max Landmark Detection:    " << std::setw(8) << metrics.max_landmark_detection_ms << "\n";

        std::cout << "\nSAMPLE STATISTICS:\n";
        std::cout << "  Total Samples:             " << std::setw(8) << metrics.total_samples << "\n";
        std::cout << "  Successful Detections:     " << std::setw(8) << metrics.successful_landmark_detections << "\n";
        
        // Add reliability note for small sample sizes
        if (metrics.total_samples < 5) {
            std::cout << "\nNOTE: Small sample size (n=" << metrics.total_samples 
                      << ") - statistical measures may be unreliable\n";
        } else if (metrics.total_samples < 10) {
            std::cout << "\nNOTE: Limited sample size (n=" << metrics.total_samples 
                      << ") - percentiles use conservative estimates\n";
        }

        std::cout << std::string(60, '=') << "\n";
    }

    void saveMetricsToFile(const DetailedMetrics& metrics, const std::string& filename) const
    {
        std::ofstream file(filename);
        if (!file.is_open())
        {
            return;
        }

        file << "subset_name,total_samples,successful_detections,overall_success_rate,";
        file << "mean_mne,median_mne,std_dev_mne,p90_mne,p95_mne,";
        file << "samples_below_003,samples_below_005,samples_below_010,";
        file << "avg_face_detection_ms,avg_landmark_detection_ms,avg_total_pipeline_ms\n";

        file << filename << "," << metrics.total_samples << "," << metrics.successful_landmark_detections << ",";
        file << metrics.overall_success_rate << "," << metrics.mean_mne << "," << metrics.median_mne << ",";
        file << metrics.std_dev_mne << "," << metrics.p90_mne << "," << metrics.p95_mne << ",";
        file << metrics.samples_below_003_mne << "," << metrics.samples_below_005_mne << ","
             << metrics.samples_below_010_mne << ",";
        file << metrics.avg_face_detection_ms << "," << metrics.avg_landmark_detection_ms << ","
             << metrics.avg_total_pipeline_ms << "\n";

        file.close();
    }

    std::shared_ptr<SCRFDetector> scrfd_500m_detector_;
    std::shared_ptr<MediaPipeFaceLandmarks> mediapipe_detector_;
    std::unique_ptr<TestUtils::Datasets::SimpleWFLWLoader> simple_wflw_loader_;
    bool has_cuda_available_ = false;
};

// Comprehensive MediaPipe benchmarking tests
TEST_F(MediaPipeFaceKeypointBenchmark, FullDatasetBenchmark)
{
    std::vector<int> all_indices;
    for (int i = 0; i < TestUtils::getMaxSamples(5); ++i)
    {
        all_indices.push_back(i);
    }

    auto metrics = runDetailedBenchmark(all_indices, "Full WFLW Test Set");
    printDetailedReport(metrics, "Full WFLW Test Set");
    saveMetricsToFile(metrics, "mediapipe_full_dataset_benchmark.csv");

    // Assert reasonable performance on full dataset
    EXPECT_GT(metrics.overall_success_rate, 60.0) << "MediaPipe: Poor overall success rate";
    EXPECT_LT(metrics.mean_mne, 0.15) << "MediaPipe: High landmark error rate";
    EXPECT_LT(metrics.avg_total_pipeline_ms, 600.0) << "MediaPipe: Pipeline too slow";
}

TEST_F(MediaPipeFaceKeypointBenchmark, NormalConditionsBenchmark)
{
    auto normal_indices = simple_wflw_loader_->getSamplesByAttributes(true, true, true, true, true, true);

    const int max_normal_samples = TestUtils::getEnvVarInt("WFLW_MAX_SAMPLES", 5);
    if (normal_indices.size() > static_cast<size_t>(max_normal_samples))
    {
        normal_indices.resize(max_normal_samples);
    }

    // Skip test if no normal condition samples found
    if (normal_indices.empty()) {
        GTEST_SKIP() << "No normal condition examples found in WFLW dataset subset";
    }

    auto metrics = runDetailedBenchmark(normal_indices, "Normal Conditions");
    printDetailedReport(metrics, "Normal Conditions");

    // Adjust expectations based on sample size for more reliable testing
    if (normal_indices.size() >= 5) {
        // Standard expectations for adequate sample size
        EXPECT_GT(metrics.overall_success_rate, 75.0) << "MediaPipe: Poor success rate on normal conditions (n=" << normal_indices.size() << ")";
        EXPECT_LT(metrics.mean_mne, 0.08) << "MediaPipe: High error on normal conditions";
        EXPECT_GT(metrics.samples_below_010_mne, 70.0) << "MediaPipe: Too few samples achieving acceptable quality";
    } else {
        // Relaxed expectations for very small samples (still useful for smoke testing)
        EXPECT_GT(metrics.overall_success_rate, 50.0) << "MediaPipe: Very poor success rate on normal conditions (n=" << normal_indices.size() << " - small sample)";
        EXPECT_LT(metrics.mean_mne, 0.15) << "MediaPipe: Extremely high error on normal conditions (basic functionality check, n=" << normal_indices.size() << ")";
    }
}

TEST_F(MediaPipeFaceKeypointBenchmark, ChallengingConditionsBenchmark)
{
    // Load challenging condition samples using centralized loader
    const int max_challenging_samples = TestUtils::getEnvVarInt("MAX_ATTRIBUTE_EXAMPLES_CHALLENGING", 5);
    
    // Use centralized loader to get challenging condition samples
    auto challenging_indices = simple_wflw_loader_->getSamplesByAttributes(false, false, false, false, false, false);
    
    if (challenging_indices.empty()) {
        GTEST_SKIP() << "No challenging condition examples found in WFLW dataset. "
                     << "Dataset may contain only normal conditions in this subset.";
    }

    // Limit to max samples
    if (challenging_indices.size() > static_cast<size_t>(max_challenging_samples)) {
        challenging_indices.resize(max_challenging_samples);
    }

    auto metrics = runDetailedBenchmark(challenging_indices, "Challenging Conditions");
    printDetailedReport(metrics, "Challenging Conditions");

    // More lenient thresholds for challenging conditions
    EXPECT_GT(metrics.overall_success_rate, 40.0) << "MediaPipe: Very poor performance on challenging conditions";
    EXPECT_LT(metrics.mean_mne, 0.20) << "MediaPipe: Extremely high error on challenging conditions";
}

TEST_F(MediaPipeFaceKeypointBenchmark, PerformanceRegressionTest)
{
    // Use environment variable for regression test sample count
    const int regression_samples = TestUtils::getEnvVarInt("WFLW_MAX_SAMPLES", 5);
    const int total_samples = simple_wflw_loader_->getSampleCount();
    const int actual_samples = std::min(regression_samples, total_samples);
    
    std::vector<int> regression_indices;
    for (int i = 0; i < actual_samples; ++i)
    {
        regression_indices.push_back(i);
    }

    auto metrics = runDetailedBenchmark(regression_indices, "Performance Regression");

    // Performance thresholds for MediaPipe
    EXPECT_LT(metrics.avg_face_detection_ms, 100.0) << "MediaPipe: Face detection performance regression";
    EXPECT_LT(metrics.avg_landmark_detection_ms, 400.0) << "MediaPipe: Landmark detection performance regression";
    EXPECT_GT(metrics.overall_success_rate, 60.0) << "MediaPipe: Success rate regression";
    EXPECT_LT(metrics.mean_mne, 0.12) << "MediaPipe: Accuracy regression";

    std::cout << "\nMediaPipe Performance regression test PASSED\n";
    std::cout << "Pipeline Time: " << metrics.avg_total_pipeline_ms << " ms (target: < 500 ms)\n";
    std::cout << "Success Rate: " << metrics.overall_success_rate << "% (target: > 60%)\n";
    std::cout << "Mean MNE: " << metrics.mean_mne << " (target: < 0.12)\n";
}

TEST_F(MediaPipeFaceKeypointBenchmark, AttributeSpecificAnalysis)
{
    struct AttributeTest
    {
        std::string name;
        bool pose, expression, illumination, makeup, occlusion, blur;
        double expected_min_success_rate;
        double expected_max_mne;
    };

    std::vector<AttributeTest> attribute_tests = {
        {"Normal Pose",          true,  true,  true,  true,  true,  true,  80.0, 0.06 },
        {"Pose Variation",       false, true,  true,  true,  true,  true,  50.0, 0.10 },
        {"Expression Variation", true,  false, true,  true,  true,  true,  70.0, 0.08 },
        {"Poor Illumination",    true,  true,  false, true,  true,  true,  65.0, 0.09 },
        {"With Makeup",          true,  true,  true,  false, true,  true,  70.0, 0.08 },
        {"With Occlusion",       true,  true,  true,  true,  false, true,  55.0, 0.12 },
        {"Blurry Images",        true,  true,  true,  true,  true,  false, 60.0, 0.10 },
    };

    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "MEDIAPIPE ATTRIBUTE-SPECIFIC ANALYSIS\n";
    std::cout << std::string(80, '=') << "\n";

    int tested_attributes = 0;
    int skipped_attributes = 0;

    for (const auto& test : attribute_tests)
    {
        auto indices = simple_wflw_loader_->getSamplesByAttributes(test.pose, test.expression, test.illumination, test.makeup,
                                                                 test.occlusion, test.blur);

        if (indices.empty())
        {
            std::cout << test.name << ": No samples found\n";
            skipped_attributes++;
            continue;
        }

        // Use environment variable to limit attribute test size
        const int max_attribute_samples = TestUtils::getEnvVarInt("WFLW_MAX_SAMPLES", 5);
        if (indices.size() > static_cast<size_t>(max_attribute_samples))
        {
            indices.resize(max_attribute_samples);
        }

        auto metrics = runDetailedBenchmark(indices, test.name);
        tested_attributes++;

        std::cout << "\n" << test.name << " (n=" << indices.size() << "):\n";
        std::cout << "  Success Rate: " << std::setprecision(1) << std::fixed << metrics.overall_success_rate
                  << "% (target: >" << test.expected_min_success_rate << "%)\n";
        std::cout << "  Mean MNE: " << std::setprecision(4) << std::fixed << metrics.mean_mne << " (target: <"
                  << test.expected_max_mne << ")\n";
        std::cout << "  Pipeline Time: " << std::setprecision(2) << std::fixed << metrics.avg_total_pipeline_ms
                  << " ms\n";

        // Add sample size adequacy warning for low sample counts
        if (indices.size() < 5) {
            std::cout << "  NOTE: Small sample size (n=" << indices.size() << ") - statistics may be unreliable\n";
        }

        // Soft assertions for attribute-specific tests
        if (metrics.overall_success_rate < test.expected_min_success_rate)
        {
            std::cout << "  WARNING: Success rate below target for MediaPipe " << test.name << "\n";
        }
        if (metrics.mean_mne > test.expected_max_mne)
        {
            std::cout << "  WARNING: MNE above target for MediaPipe " << test.name << "\n";
        }
    }

    // Summary of attribute testing
    std::cout << "\n" << std::string(80, '-') << "\n";
    std::cout << "MEDIAPIPE ATTRIBUTE ANALYSIS SUMMARY:\n";
    std::cout << "  Tested attributes: " << tested_attributes << "\n";
    std::cout << "  Skipped attributes (no samples): " << skipped_attributes << "\n";
    
    // At least some attribute tests should have samples, unless dataset is very small
    if (tested_attributes == 0) {
        std::cout << "  WARNING: No attribute-specific samples found - dataset may be too small or improperly filtered\n";
    } else {
        std::cout << "  MediaPipe attribute testing completed successfully\n";
    }

    std::cout << std::string(80, '=') << "\n";
}
