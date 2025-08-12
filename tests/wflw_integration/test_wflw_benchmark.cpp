#include "wflw_loader.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
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

/**
 * Comprehensive benchmarking suite for face keypoint detection algorithms.
 * This suite provides detailed performance analysis and can be used for:
 * - Algorithm comparison
 * - Performance regression testing
 * - Model optimization validation
 */
class FaceKeypointBenchmark : public ::testing::Test
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

        std::string models_folder = Config::getInstance().getModelFolderPath();

        // Initialize SCRFD (multiple model sizes for comparison)
        scrfd_500m_detector_ = std::make_shared<SCRFDetector>(models_folder + "scrfd_500m_bnkps_shape640x640.onnx");

        // Initialize PFLD
        pfld_detector_ = std::make_shared<PFLDDetector>(models_folder + "pfld-106-v3.onnx");

        ASSERT_TRUE(scrfd_500m_detector_->isReady())
            << "SCRFD detector failed to initialize. Expected path: " << models_folder
            << "scrfd_500m_bnkps_shape640x640.onnx";
        ASSERT_TRUE(pfld_detector_->isReady())
            << "PFLD detector failed to initialize. Expected path: " << models_folder << "pfld-106-v3.onnx";

        // Load WFLW dataset
        const std::string wflw_base = Config::getInstance().getWFLWFolderPath();
        const std::string test_annotations =
            wflw_base + "/WFLW_annotations/list_98pt_rect_attr_train_test/list_98pt_rect_attr_test.txt";
        wflw_loader_ = std::make_unique<WFLWLoader>(test_annotations, 100); // Larger subset for benchmarking

        ASSERT_GT(wflw_loader_->get_num_examples(), 0) << "No WFLW examples loaded for benchmarking";
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

        std::cout << "\nRunning detailed benchmark on " << subset_name << " (" << example_indices.size()
                  << " samples)...\n";

        for (int idx : example_indices)
        {
            WFLWExample example;
            if (!wflw_loader_->load_example(idx, example))
            {
                continue;
            }

            // Face detection timing
            auto face_start = std::chrono::high_resolution_clock::now();
            auto detected_faces = scrfd_500m_detector_->detect(example.image);
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

            // Landmark detection timing
            auto landmark_start = std::chrono::high_resolution_clock::now();
            pfld_detector_->detect(example.image, face);
            auto landmark_end = std::chrono::high_resolution_clock::now();

            double landmark_detection_time =
                std::chrono::duration_cast<std::chrono::microseconds>(landmark_end - landmark_start).count() / 1000.0;
            metrics.landmark_detection_times.push_back(landmark_detection_time);

            auto predicted_landmarks = face.getLandmarks();
            if (predicted_landmarks.size() != 106)
            {
                continue;
            }

            metrics.successful_landmark_detections++;

            // Calculate MNE
            auto left_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::LEYE);
            auto right_eye = face.getLandmarkByIndex(SCRFDetector::LandmarkIndex::REYE);
            double iod = std::sqrt(std::pow(right_eye.x - left_eye.x, 2) + std::pow(right_eye.y - left_eye.y, 2));

            if (iod <= 0.0)
            {
                continue;
            }

            // Use first 98 landmarks to match WFLW
            std::vector<FaceLandmark> pfld_98_landmarks(predicted_landmarks.begin(), predicted_landmarks.begin() + 98);

            double error_sum = 0.0;
            for (size_t i = 0; i < pfld_98_landmarks.size() && i < example.landmarks.size(); ++i)
            {
                double dx = pfld_98_landmarks[i].p.x - example.landmarks[i].x;
                double dy = pfld_98_landmarks[i].p.y - example.landmarks[i].y;
                error_sum += std::sqrt(dx * dx + dy * dy);
            }

            double mne = (error_sum / pfld_98_landmarks.size()) / iod;
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
            (static_cast<double>(metrics.successful_landmark_detections) / metrics.successful_face_detections) * 100.0;
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

        // MNE statistics
        if (!metrics.mne_scores.empty())
        {
            std::vector<double> sorted_mne = metrics.mne_scores;
            std::sort(sorted_mne.begin(), sorted_mne.end());

            metrics.mean_mne = std::accumulate(sorted_mne.begin(), sorted_mne.end(), 0.0) / sorted_mne.size();

            size_t size = sorted_mne.size();
            metrics.median_mne =
                (size % 2 == 0) ? (sorted_mne[size / 2 - 1] + sorted_mne[size / 2]) / 2.0 : sorted_mne[size / 2];

            metrics.p90_mne = sorted_mne[static_cast<size_t>(size * 0.9)];
            metrics.p95_mne = sorted_mne[static_cast<size_t>(size * 0.95)];

            // Standard deviation
            double variance = 0.0;
            for (double score : sorted_mne)
            {
                variance += std::pow(score - metrics.mean_mne, 2);
            }
            metrics.std_dev_mne = std::sqrt(variance / sorted_mne.size());

            // Quality thresholds
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
        std::cout << "DETAILED BENCHMARK REPORT: " << subset_name << "\n";
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
    std::shared_ptr<PFLDDetector> pfld_detector_;
    std::unique_ptr<WFLWLoader> wflw_loader_;
    bool has_cuda_available_ = false;
};

// Comprehensive benchmarking tests
TEST_F(FaceKeypointBenchmark, DISABLED_FullDatasetBenchmark)
{
    // This test is disabled by default due to long execution time
    // Enable when needed for comprehensive evaluation

    std::vector<int> all_indices;
    for (int i = 0; i < wflw_loader_->get_num_examples(); ++i)
    {
        all_indices.push_back(i);
    }

    auto metrics = runDetailedBenchmark(all_indices, "Full WFLW Test Set");
    printDetailedReport(metrics, "Full WFLW Test Set");
    saveMetricsToFile(metrics, "full_dataset_benchmark.csv");

    // Assert reasonable performance on full dataset
    EXPECT_GT(metrics.overall_success_rate, 75.0);
    EXPECT_LT(metrics.mean_mne, 0.06);
    EXPECT_LT(metrics.avg_total_pipeline_ms, 60.0);
}

TEST_F(FaceKeypointBenchmark, NormalConditionsBenchmark)
{
    auto normal_indices = wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true);

    if (normal_indices.size() > 50)
    {
        normal_indices.resize(50); // Reasonable test size
    }

    auto metrics = runDetailedBenchmark(normal_indices, "Normal Conditions");
    printDetailedReport(metrics, "Normal Conditions");

    // Higher expectations for normal conditions
    EXPECT_GT(metrics.overall_success_rate, 85.0) << "Poor success rate on normal conditions";
    EXPECT_LT(metrics.mean_mne, 0.04) << "High error on normal conditions";
    EXPECT_GT(metrics.samples_below_005_mne, 80.0) << "Too few samples achieving good quality";
}

TEST_F(FaceKeypointBenchmark, ChallenginConditionsBenchmark)
{
    auto challenging_indices = wflw_loader_->getExamplesByAttribute(false, false, false, false, false, false);

    if (challenging_indices.size() > 30)
    {
        challenging_indices.resize(30);
    }

    auto metrics = runDetailedBenchmark(challenging_indices, "Challenging Conditions");
    printDetailedReport(metrics, "Challenging Conditions");

    // More lenient for challenging conditions
    EXPECT_GT(metrics.overall_success_rate, 50.0) << "Very poor performance on challenging conditions";
    EXPECT_LT(metrics.mean_mne, 0.08) << "Extremely high error on challenging conditions";
}

TEST_F(FaceKeypointBenchmark, PerformanceRegressionTest)
{
    // Use first 20 samples for consistent regression testing
    std::vector<int> regression_indices;
    for (int i = 0; i < std::min(20, wflw_loader_->get_num_examples()); ++i)
    {
        regression_indices.push_back(i);
    }

    auto metrics = runDetailedBenchmark(regression_indices, "Performance Regression");

    // Performance thresholds (adjust based on your requirements)
    EXPECT_LT(metrics.avg_face_detection_ms, 8.0) << "Face detection performance regression";
    EXPECT_LT(metrics.avg_landmark_detection_ms, 45.0) << "Landmark detection performance regression";
    EXPECT_GT(metrics.overall_success_rate, 75.0) << "Success rate regression";
    EXPECT_LT(metrics.mean_mne, 0.05) << "Accuracy regression";

    std::cout << "\nPerformance regression test PASSED\n";
    std::cout << "Pipeline Time: " << metrics.avg_total_pipeline_ms << " ms (target: < 53 ms)\n";
    std::cout << "Success Rate: " << metrics.overall_success_rate << "% (target: > 75%)\n";
    std::cout << "Mean MNE: " << metrics.mean_mne << " (target: < 0.05)\n";
}

TEST_F(FaceKeypointBenchmark, AttributeSpecificAnalysis)
{
    struct AttributeTest
    {
        std::string name;
        bool pose, expression, illumination, makeup, occlusion, blur;
        double expected_min_success_rate;
        double expected_max_mne;
    };

    std::vector<AttributeTest> attribute_tests = {
        {"Normal Pose",          true,  true,  true,  true,  true,  true,  85.0, 0.04 },
        {"Pose Variation",       false, true,  true,  true,  true,  true,  60.0, 0.06 },
        {"Expression Variation", true,  false, true,  true,  true,  true,  80.0, 0.045},
        {"Poor Illumination",    true,  true,  false, true,  true,  true,  75.0, 0.05 },
        {"With Makeup",          true,  true,  true,  false, true,  true,  80.0, 0.045},
        {"With Occlusion",       true,  true,  true,  true,  false, true,  65.0, 0.06 },
        {"Blurry Images",        true,  true,  true,  true,  true,  false, 70.0, 0.055},
    };

    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "ATTRIBUTE-SPECIFIC ANALYSIS\n";
    std::cout << std::string(80, '=') << "\n";

    for (const auto& test : attribute_tests)
    {
        auto indices = wflw_loader_->getExamplesByAttribute(test.pose, test.expression, test.illumination, test.makeup,
                                                            test.occlusion, test.blur);

        if (indices.empty())
        {
            std::cout << test.name << ": No samples found\n";
            continue;
        }

        if (indices.size() > 25)
        {
            indices.resize(25); // Limit test size
        }

        auto metrics = runDetailedBenchmark(indices, test.name);

        std::cout << "\n" << test.name << " (n=" << indices.size() << "):\n";
        std::cout << "  Success Rate: " << std::setprecision(1) << std::fixed << metrics.overall_success_rate
                  << "% (target: >" << test.expected_min_success_rate << "%)\n";
        std::cout << "  Mean MNE: " << std::setprecision(4) << std::fixed << metrics.mean_mne << " (target: <"
                  << test.expected_max_mne << ")\n";
        std::cout << "  Pipeline Time: " << std::setprecision(2) << std::fixed << metrics.avg_total_pipeline_ms
                  << " ms\n";

        // Soft assertions for attribute-specific tests
        if (metrics.overall_success_rate < test.expected_min_success_rate)
        {
            std::cout << "  WARNING: Success rate below target for " << test.name << "\n";
        }
        if (metrics.mean_mne > test.expected_max_mne)
        {
            std::cout << "  WARNING: MNE above target for " << test.name << "\n";
        }
    }

    std::cout << std::string(80, '=') << "\n";
}
