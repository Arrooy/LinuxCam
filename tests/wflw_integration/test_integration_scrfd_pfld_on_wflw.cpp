/**
 * INTEGRATION TEST: SCRFD + PFLD PIPELINE ON WFLW DATASET
 *
 * This integration test covers:
 * - SCRFD face detection initialization and basic functionality
 * - PFLD landmark detection initialization and basic functionality
 * - Combined SCRFD->PFLD pipeline validation
 * - Error handling and edge cases using WFLW test data
 */
#include "wflw_loader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <memory>
#include <numeric>
#include <onnxruntime_cxx_api.h>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/text_draw.h"
#include "LinuxFace/face.h"
#include "LinuxFace/landmark_converter.h" // For proper PFLD->WFLW conversion
#include "LinuxFace/onnx/pfld.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/profiler.h"
#include "config.hpp"
#include "wflw_integration_suite/wflw_test_base.h"

// For line drawing in debug images
#include "LinuxFace/math_utils.h"

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
        const std::string test_annotations = WFLWTestBase::getWFLWAnnotationsPath() + "/list_98pt_rect_attr_test.txt";

        // Check if WFLW dataset is available
        if (!std::ifstream(test_annotations).good())
        {
            GTEST_SKIP() << "WFLW dataset not found at: " << test_annotations << "\n"
                         << "Please run: ./scripts/download_wflw_dataset.sh\n"
                         << "Or set WFLW_folder_path in config to correct location";
        }

        // Load configurable number of samples for analysis
        // Get max samples from environment variable, default to 500 for CI/CD
        const char* max_samples_env = std::getenv("WFLW_MAX_SAMPLES");
        int max_samples = max_samples_env ? std::atoi(max_samples_env) : 500;

        // For local development, allow more samples
        if (max_samples == -1)
        {
            std::cout << "Loading ALL available samples (this may take a while)" << std::endl;
        }
        else
        {
            std::cout << "Loading up to " << max_samples << " samples for analysis" << std::endl;
        }

        wflw_loader_ = std::make_unique<WFLWLoader>(test_annotations, max_samples);
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

    // Save debug image with landmarks for analysis using LinuxFace Image utilities
    // Enhanced debug image function to compare multiple PFLD detection methods
    void saveMultiMethodDebugImageWithMapping(const WFLWExample& example,
                                              const std::vector<FaceLandmark>& original_pfld_landmarks,
                                              const std::vector<FaceLandmark>& method1_landmarks,
                                              const std::vector<FaceLandmark>& method2_landmarks,
                                              const std::vector<FaceLandmark>& method3_landmarks,
                                              const std::vector<math_utils::Point<double>>& ground_truth, double mne1,
                                              double mne2, double mne3, int index, const std::string& reason = "") const
    {
        try
        {
            // First save the regular debug image
            saveMultiMethodDebugImage(example, method1_landmarks, method2_landmarks, method3_landmarks, ground_truth,
                                      mne1, mne2, mne3, index, reason);

            // Create a detailed mapping analysis
            std::cout << "\n=== DETAILED LANDMARK CONVERSION ANALYSIS ===\n";
            std::cout << "Original PFLD landmarks: " << original_pfld_landmarks.size() << std::endl;
            std::cout << "Converted WFLW landmarks: " << method1_landmarks.size() << std::endl;
            std::cout << "Ground truth WFLW landmarks: " << ground_truth.size() << std::endl;

            // Analyze key landmark regions for conversion issues
            std::vector<std::pair<std::string, std::vector<int>>> key_regions = {
                {"Jawline corners", {0, 8, 16}},
                {"Eyebrow centers", {36, 45}  },
                {"Eye centers",     {63, 71}  },
                {"Nose tip",        {54}      },
                {"Mouth corners",   {76, 82}  }
            };

            double total_error = 0.0;
            int valid_comparisons = 0;

            for (const auto& region : key_regions)
            {
                std::cout << "\n" << region.first << ":\n";
                for (int wflw_idx : region.second)
                {
                    if (wflw_idx < static_cast<int>(method1_landmarks.size())
                        && wflw_idx < static_cast<int>(ground_truth.size()))
                    {
                        const auto& converted = method1_landmarks[wflw_idx];
                        const auto& gt = ground_truth[wflw_idx];
                        double distance =
                            std::sqrt(std::pow(converted.p.x - gt.x, 2) + std::pow(converted.p.y - gt.y, 2));
                        total_error += distance;
                        valid_comparisons++;

                        std::cout << "  WFLW[" << wflw_idx << "]: Converted(" << converted.p.x << "," << converted.p.y
                                  << ") vs GT(" << gt.x << "," << gt.y << ") - Error: " << distance;
                        if (distance > 20.0)
                        {
                            std::cout << " **HIGH ERROR**";
                        }
                        std::cout << std::endl;
                    }
                }
            }

            if (valid_comparisons > 0)
            {
                double avg_error = total_error / valid_comparisons;
                std::cout << "\nAverage error for key landmarks: " << avg_error << " pixels\n";

                if (avg_error > 50.0)
                {
                    std::cout << "**WARNING: Very high conversion error detected!**\n";
                    std::cout << "This suggests the PFLD->WFLW mapping is incorrect.\n";
                }
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error in mapping analysis: " << e.what() << std::endl;
        }
    }

    void saveMultiMethodDebugImage(const WFLWExample& example, const std::vector<FaceLandmark>& method1_landmarks,
                                   const std::vector<FaceLandmark>& method2_landmarks,
                                   const std::vector<FaceLandmark>& method3_landmarks,
                                   const std::vector<math_utils::Point<double>>& ground_truth, double mne1, double mne2,
                                   double mne3, int index, const std::string& reason = "") const
    {
        try
        {
            // Create a copy of the original image for drawing
            auto debug_img = example.image->deepCopy();

            // Define colors for different methods
            Pixel red_color(255, 0, 0);       // Red for method 1 (detect)
            Pixel blue_color(0, 0, 255);      // Blue for method 2 (detectSimilar)
            Pixel cyan_color(0, 255, 255);    // Cyan for method 3 (detectOpenCv)
            Pixel green_color(0, 255, 0);     // Green for ground truth landmarks
            Pixel magenta_color(255, 0, 255); // Magenta for error lines (method 1 only)
            Pixel white_color(255, 255, 255); // White for landmark numbers
            Pixel yellow_color(255, 255, 0);  // Yellow for landmark numbers background

            // Draw error lines first (for ALL landmarks to see the full problem)
            if (!method1_landmarks.empty())
            {
                for (size_t i = 0; i < std::min({method1_landmarks.size(), ground_truth.size()}); ++i)
                {
                    const auto& pred = method1_landmarks[i];
                    const auto& gt = ground_truth[i];

                    // Generate line points from predicted to ground truth using DDA algorithm
                    auto line_points = math_utils::DDA(static_cast<long>(pred.p.x), static_cast<long>(pred.p.y),
                                                       static_cast<long>(gt.x), static_cast<long>(gt.y));

                    // Draw the error line in magenta
                    debug_img->paintPoints(line_points, magenta_color);
                }
            }

            // Helper function to draw landmarks with specific color and numbers
            auto drawLandmarksWithNumbers = [&](const std::vector<FaceLandmark>& landmarks, const Pixel& color,
                                                const std::string& prefix, int offset = 0)
            {
                for (size_t i = 0; i < std::min(landmarks.size(), size_t(98)); ++i)
                {
                    const auto& lm = landmarks[i];
                    int x = static_cast<int>(lm.p.x) + offset;
                    int y = static_cast<int>(lm.p.y) + offset;

                    // Draw a 3x3 pixel marker for better visibility
                    for (int dx = -1; dx <= 1; dx++)
                    {
                        for (int dy = -1; dy <= 1; dy++)
                        {
                            int px = x + dx, py = y + dy;
                            if (px >= 0 && px < static_cast<int>(debug_img->info.width) && py >= 0
                                && py < static_cast<int>(debug_img->info.height))
                            {
                                debug_img->pxy(px, py, color.r, color.g, color.b, color.a);
                            }
                        }
                    }

                    // Draw landmark number every 10th landmark or for problematic ones
                    if (i % 10 == 0 || i < 10)
                    {
                        std::string label = prefix + std::to_string(i);
                        // Use text drawing function from text_draw.h
                        linuxface::drawText(*debug_img, x + 5, y - 10, label, white_color, 1, false);
                    }
                }
            };

            // Draw ground truth landmarks first with numbers
            for (size_t i = 0; i < std::min(ground_truth.size(), size_t(98)); ++i)
            {
                const auto& gt = ground_truth[i];
                int x = static_cast<int>(gt.x);
                int y = static_cast<int>(gt.y);

                // Draw a 3x3 pixel marker for ground truth
                for (int dx = -1; dx <= 1; dx++)
                {
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        int px = x + dx, py = y + dy;
                        if (px >= 0 && px < static_cast<int>(debug_img->info.width) && py >= 0
                            && py < static_cast<int>(debug_img->info.height))
                        {
                            debug_img->pxy(px, py, green_color.r, green_color.g, green_color.b, green_color.a);
                        }
                    }
                }

                // Draw ground truth landmark numbers
                if (i % 10 == 0 || i < 10 || i > 90)
                {
                    std::string label = "G" + std::to_string(i);
                    linuxface::drawText(*debug_img, x + 5, y + 5, label, yellow_color, 1, false);
                }
            }

            // Draw all method landmarks with different colors, offsets, and numbers
            if (!method1_landmarks.empty())
            {
                drawLandmarksWithNumbers(method1_landmarks, red_color, "R", 0); // detect() - red, no offset
            }
            if (!method2_landmarks.empty())
            {
                drawLandmarksWithNumbers(method2_landmarks, blue_color, "B", 2); // detectSimilar() - blue, +2 offset
            }
            if (!method3_landmarks.empty())
            {
                drawLandmarksWithNumbers(method3_landmarks, cyan_color, "C", -2); // detectOpenCv() - cyan, -2 offset
            }

            // Create filename with MNE scores for all methods
            std::string filename = "debug_multi_method_idx" + std::to_string(index) + "_mne1-"
                                   + std::to_string(mne1).substr(0, 4) + "_mne2-" + std::to_string(mne2).substr(0, 4)
                                   + "_mne3-" + std::to_string(mne3).substr(0, 4);
            if (!reason.empty())
            {
                std::string clean_reason = reason;
                std::replace(clean_reason.begin(), clean_reason.end(), ' ', '_');
                filename += "_" + clean_reason;
            }
            filename += ".jpg";

            std::cout << "Saving multi-method debug image: " << filename << std::endl;
            std::cout << "Method MNE Scores - detect(): " << mne1 << ", detectSimilar(): " << mne2
                      << ", detectOpenCv(): " << mne3 << std::endl;
            std::cout << "Legend: Red=detect(1px), Blue=detectSimilar(1px+1offset), Cyan=detectOpenCv(1px-1offset), "
                      << "Green=GroundTruth(1px), Magenta=Error_lines" << std::endl;

            // Add landmark correspondence debugging info with detailed mapping verification
            if (!method1_landmarks.empty() && !ground_truth.empty())
            {
                std::cout << "\n=== LANDMARK MAPPING VERIFICATION ===\n";
                std::cout << "PFLD landmarks: " << method1_landmarks.size()
                          << ", WFLW ground truth: " << ground_truth.size() << std::endl;

                // Check the mapping by looking at key facial regions
                std::vector<std::pair<std::string, std::vector<int>>> regions = {
                    {"Jawline",       {0, 8, 16, 24, 32}}, // Key jawline points
                    {"Right Eyebrow", {33, 36, 41}      }, // Key right eyebrow points
                    {"Left Eyebrow",  {42, 45, 50}      }, // Key left eyebrow points
                    {"Nose",          {51, 54, 59}      }, // Key nose points
                    {"Right Eye",     {60, 63, 67}      }, // Key right eye points
                    {"Left Eye",      {68, 71, 75}      }, // Key left eye points
                    {"Outer Mouth",   {76, 82, 87}      }, // Key outer mouth points
                    {"Inner Mouth",   {88, 91, 95}      }  // Key inner mouth points
                };

                for (const auto& region : regions)
                {
                    std::cout << "\n" << region.first << " landmarks:\n";
                    for (int wflw_idx : region.second)
                    {
                        if (wflw_idx < static_cast<int>(ground_truth.size())
                            && wflw_idx < static_cast<int>(method1_landmarks.size()))
                        {
                            const auto& gt = ground_truth[wflw_idx];
                            const auto& pred = method1_landmarks[wflw_idx];
                            double distance = std::sqrt(std::pow(pred.p.x - gt.x, 2) + std::pow(pred.p.y - gt.y, 2));
                            std::cout << "  WFLW[" << wflw_idx << "]: GT(" << gt.x << "," << gt.y << ") vs PFLD("
                                      << pred.p.x << "," << pred.p.y << ") - Distance: " << distance;
                            if (distance > 50.0)
                            {
                                std::cout << " **SUSPICIOUS**";
                            }
                            std::cout << std::endl;
                        }
                    }
                }

                // Show the original 106-landmark PFLD output before conversion
                std::cout << "\n=== ORIGINAL PFLD 106-LANDMARK SAMPLE (before conversion) ===\n";
                // We need to get the original 106 landmarks - this requires modifying the test structure
                std::cout << "Note: Original PFLD landmarks would be shown here for mapping verification\n";
            }

            // Save using LinuxFace Image utilities
            if (!debug_img->saveToDisk(filename))
            {
                std::cout << "Failed to save debug image: " << filename << std::endl;
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error saving multi-method debug image: " << e.what() << std::endl;
        }
    }

    void saveDebugImage(const WFLWExample& example, const std::vector<FaceLandmark>& predicted_landmarks,
                        const std::vector<math_utils::Point<double>>& ground_truth, double mne_score, int index,
                        const std::string& reason = "") const
    {
        try
        {
            // Create a copy of the original image for drawing
            auto debug_img = example.image->deepCopy();

            // Define colors for landmarks
            Pixel red_color(255, 0, 0);       // Red for predicted landmarks
            Pixel green_color(0, 255, 0);     // Green for ground truth landmarks
            Pixel magenta_color(255, 0, 255); // Magenta for error lines
            Pixel white_color(255, 255, 255); // White for text overlay

            // Draw error lines first (so landmarks appear on top) - limit to first 10 for visibility
            for (size_t i = 0; i < std::min({predicted_landmarks.size(), ground_truth.size(), size_t(10)}); ++i)
            {
                const auto& pred = predicted_landmarks[i];
                const auto& gt = ground_truth[i];

                // Generate line points from predicted to ground truth using DDA algorithm
                auto line_points = math_utils::DDA(static_cast<long>(pred.p.x), static_cast<long>(pred.p.y),
                                                   static_cast<long>(gt.x), static_cast<long>(gt.y));

                // Draw the error line in magenta
                debug_img->paintPoints(line_points, magenta_color);
            }

            // Draw predicted landmarks as red circles (3x3 squares for visibility)
            for (size_t i = 0; i < std::min(predicted_landmarks.size(), size_t(98)); ++i)
            {
                const auto& pred = predicted_landmarks[i];
                int x = static_cast<int>(pred.p.x);
                int y = static_cast<int>(pred.p.y);

                // Draw a single pixel centered on the landmark
                if (x >= 0 && x < static_cast<int>(debug_img->info.width) && y >= 0
                    && y < static_cast<int>(debug_img->info.height))
                {
                    debug_img->pxy(x, y, red_color.r, red_color.g, red_color.b, red_color.a);
                }
            }

            // Draw ground truth landmarks as green circles (single pixels for visibility)
            for (size_t i = 0; i < std::min(ground_truth.size(), size_t(98)); ++i)
            {
                const auto& gt = ground_truth[i];
                int x = static_cast<int>(gt.x);
                int y = static_cast<int>(gt.y);

                // Draw a single pixel centered on the landmark
                if (x >= 0 && x < static_cast<int>(debug_img->info.width) && y >= 0
                    && y < static_cast<int>(debug_img->info.height))
                {
                    debug_img->pxy(x, y, green_color.r, green_color.g, green_color.b, green_color.a);
                }
            }

            // Add simple text info by drawing white pixels for the MNE score at top-left
            // Note: This is a basic implementation without font rendering
            // The info is primarily in the filename and console output

            // Create filename with MNE score and reason
            std::string filename =
                "debug_landmarks_idx" + std::to_string(index) + "_mne" + std::to_string(mne_score).substr(0, 4);
            if (!reason.empty())
            {
                // Replace spaces with underscores in reason for filename
                std::string clean_reason = reason;
                std::replace(clean_reason.begin(), clean_reason.end(), ' ', '_');
                filename += "_" + clean_reason;
            }
            filename += ".ppm";

            std::cout << "Saving debug image: " << filename << " (MNE: " << mne_score << ")" << std::endl;
            std::cout << "Legend: Red=Predicted(1px), Green=GroundTruth(1px), Magenta=Error_lines(first_10)"
                      << std::endl;

            // Save using LinuxFace Image utilities
            if (!debug_img->saveToDisk(filename))
            {
                std::cout << "Failed to save debug image: " << filename << std::endl;
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error saving debug image: " << e.what() << std::endl;
        }
    }

    // Benchmark metrics calculation with detailed failure analysis
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

        // Failure analysis
        int scrfd_failures = 0;          // No faces detected by SCRFD
        int pfld_failures = 0;           // PFLD didn't produce 106 landmarks
        int iod_failures = 0;            // Invalid interocular distance
        int landmark_bound_failures = 0; // Landmarks outside image bounds
        int image_load_failures = 0;     // Failed to load image

        // Error distribution by attribute
        struct AttributeStats
        {
            int pose_failures = 0;
            int expression_failures = 0;
            int illumination_failures = 0;
            int makeup_failures = 0;
            int occlusion_failures = 0;
            int blur_failures = 0;
        } attribute_failures;

        // Performance by MNE ranges
        int excellent_count = 0; // MNE < 0.03
        int good_count = 0;      // 0.03 <= MNE < 0.05
        int fair_count = 0;      // 0.05 <= MNE < 0.08
        int poor_count = 0;      // MNE >= 0.08

        // Worst performing examples for debugging
        struct FailureExample
        {
            int index;
            double mne_score;
            std::string failure_reason;
            std::array<int, 6> attributes;
        };
        std::vector<FailureExample> worst_examples;
    };

    BenchmarkResults runBenchmarkOnSubset(const std::vector<int>& example_indices, bool show_progress = false,
                                          bool save_debug_images = false) const
    {
        BenchmarkResults results;
        results.total_samples = example_indices.size();
        results.individual_mne_scores.reserve(example_indices.size());

        double total_scrfd_time = 0.0;
        double total_pfld_time = 0.0;

        // Progress tracking
        int progress_interval = std::max(1, static_cast<int>(example_indices.size() / 20)); // Report every 5%
        auto last_progress_time = std::chrono::steady_clock::now();

        for (size_t i = 0; i < example_indices.size(); ++i)
        {
            int idx = example_indices[i];

            // Show progress - report every N samples or every 2 seconds
            auto now = std::chrono::steady_clock::now();
            bool time_for_update =
                std::chrono::duration_cast<std::chrono::seconds>(now - last_progress_time).count() >= 2;

            if (show_progress && (i % progress_interval == 0 || i == example_indices.size() - 1 || time_for_update))
            {
                double progress = (static_cast<double>(i + 1) / example_indices.size()) * 100.0;
                std::cout << "\rProcessing: " << std::fixed << std::setprecision(1) << progress << "% (" << (i + 1)
                          << "/" << example_indices.size() << ") - "
                          << "Success: " << results.successful_detections << ", "
                          << "Failures: "
                          << (results.scrfd_failures + results.pfld_failures + results.iod_failures
                              + results.landmark_bound_failures)
                          << std::flush;
                last_progress_time = now;
            }

            WFLWExample example;
            if (!wflw_loader_->load_example(idx, example))
            {
                results.image_load_failures++;
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
                results.scrfd_failures++;

                // Track attribute-specific failures
                if (!example.isNormalPose())
                {
                    results.attribute_failures.pose_failures++;
                }
                if (!example.isNormalExpression())
                {
                    results.attribute_failures.expression_failures++;
                }
                if (!example.isNormalIllumination())
                {
                    results.attribute_failures.illumination_failures++;
                }
                if (!example.hasNoMakeup())
                {
                    results.attribute_failures.makeup_failures++;
                }
                if (!example.hasNoOcclusion())
                {
                    results.attribute_failures.occlusion_failures++;
                }
                if (!example.isClear())
                {
                    results.attribute_failures.blur_failures++;
                }

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
                results.pfld_failures++;
                continue;
            }

            // Calculate interocular distance for normalization
            double iod = calculateInterocularDistance(face);
            if (iod <= 0.0)
            {
                results.iod_failures++;
                continue;
            }

            // Validate landmarks are within image bounds
            bool landmarks_valid = true;
            for (const auto& landmark : predicted_landmarks)
            {
                if (landmark.p.x < 0.0 || landmark.p.x >= example.image->info.width || landmark.p.y < 0.0
                    || landmark.p.y >= example.image->info.height)
                {
                    landmarks_valid = false;
                    break;
                }
            }

            if (!landmarks_valid)
            {
                results.landmark_bound_failures++;
                continue;
            }

            // Convert PFLD 106-point landmarks to WFLW 98-point format using proper correspondence
            // Note: WFLW has 98 landmarks, PFLD detects 106
            // We need to use proper landmark correspondence mapping, not just take first 98
            std::vector<FaceLandmark> pfld_98_landmarks;
            try
            {
                pfld_98_landmarks = LandmarkConverter::pfldToWflw(predicted_landmarks);
            }
            catch (const std::exception& e)
            {
                results.pfld_failures++;
                continue; // Conversion failed
            }

            double mne = calculateMNE(pfld_98_landmarks, example.landmarks, iod);
            if (mne >= 0.0)
            {
                results.individual_mne_scores.push_back(mne);
                results.successful_detections++;

                // Categorize performance
                if (mne < 0.03)
                {
                    results.excellent_count++;
                }
                else if (mne < 0.05)
                {
                    results.good_count++;
                }
                else if (mne < 0.08)
                {
                    results.fair_count++;
                }
                else
                {
                    results.poor_count++;

                    // Track worst performing examples
                    BenchmarkResults::FailureExample failure;
                    failure.index = idx;
                    failure.mne_score = mne;
                    failure.attributes = example.attributes;
                    failure.failure_reason = "High MNE score: " + std::to_string(mne);
                    results.worst_examples.push_back(failure);

                    // Save debug image for high MNE cases if requested and SAVE_IMAGES env var is set
                    const char* save_images_env = std::getenv("SAVE_IMAGES");
                    if (save_debug_images && save_images_env && mne > 1.0)
                    { // Only save really bad cases to avoid too many images
                        saveDebugImage(example, pfld_98_landmarks, example.landmarks, mne, idx,
                                       "Poor performance - MNE > 1.0");
                    }
                }
            }
        }

        if (show_progress)
        {
            std::cout << "\nCompleted processing " << example_indices.size() << " samples." << std::endl;
        }

        // Sort worst examples by MNE score (highest first)
        std::sort(results.worst_examples.begin(), results.worst_examples.end(),
                  [](const BenchmarkResults::FailureExample& a, const BenchmarkResults::FailureExample& b)
                  { return a.mne_score > b.mne_score; });

        // Keep only top 10 worst examples
        if (results.worst_examples.size() > 10)
        {
            results.worst_examples.resize(10);
        }

        // Save debug images for worst examples if we have any and SAVE_IMAGES env var is set
        const char* save_images_env = std::getenv("SAVE_IMAGES");
        if (save_debug_images && save_images_env && !results.worst_examples.empty())
        {
            std::cout << "\nSaving debug images for worst " << std::min(size_t(3), results.worst_examples.size())
                      << " examples..." << std::endl;

            for (size_t i = 0; i < std::min(size_t(3), results.worst_examples.size()); ++i)
            {
                const auto& worst = results.worst_examples[i];

                WFLWExample example;
                if (wflw_loader_->load_example(worst.index, example))
                {
                    auto faces = scrfd_detector_->detect(example.image);
                    if (!faces.empty())
                    {
                        Face& face = faces[0];
                        pfld_detector_->detect(example.image, face);
                        auto predicted_landmarks = face.getLandmarks();

                        if (predicted_landmarks.size() >= 98)
                        {
                            std::vector<FaceLandmark> pfld_98_landmarks(predicted_landmarks.begin(),
                                                                        predicted_landmarks.begin() + 98);

                            std::string reason = "Worst #" + std::to_string(i + 1)
                                                 + " - MNE: " + std::to_string(worst.mne_score).substr(0, 6);
                            saveDebugImage(example, pfld_98_landmarks, example.landmarks, worst.mne_score, worst.index,
                                           reason);
                        }
                    }
                }
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

        if (show_progress)
        {
            std::cout << "\nCompleted processing " << example_indices.size() << " samples." << std::endl;
        }

        return results;
    }

    // Generate detailed attribute-based analysis
    void printAttributeAnalysis(const std::string& title, const std::vector<int>& indices,
                                const BenchmarkResults& results) const
    {
        std::cout << "\n=== " << title << " ===\n";
        std::cout << "Sample Size: " << indices.size() << "\n";
        std::cout << "Success Rate: " << results.success_rate << "%\n";
        std::cout << "Mean MNE: " << results.mean_mne << "\n";
        std::cout << "Median MNE: " << results.median_mne << "\n";
        std::cout << "SCRFD Failures: " << results.scrfd_failures << "/" << results.total_samples << " ("
                  << (100.0 * results.scrfd_failures / results.total_samples) << "%)\n";
        std::cout << "PFLD Failures: " << results.pfld_failures << "/" << results.total_samples << " ("
                  << (100.0 * results.pfld_failures / results.total_samples) << "%)\n";

        if (results.successful_detections > 0)
        {
            std::cout << "Performance Distribution:\n";
            std::cout << "  - Excellent: " << results.excellent_count << " ("
                      << (100.0 * results.excellent_count / results.successful_detections) << "%)\n";
            std::cout << "  - Good: " << results.good_count << " ("
                      << (100.0 * results.good_count / results.successful_detections) << "%)\n";
            std::cout << "  - Fair: " << results.fair_count << " ("
                      << (100.0 * results.fair_count / results.successful_detections) << "%)\n";
            std::cout << "  - Poor: " << results.poor_count << " ("
                      << (100.0 * results.poor_count / results.successful_detections) << "%)\n";
        }
    }

    void saveDetectionVisualization(const WFLWExample& example, const std::vector<FaceLandmark>& detected_landmarks,
                                    int image_index, double mne) const
    {
        try
        {
            // Create a copy of the original image for drawing
            auto debug_img = example.image->deepCopy();

            // Define colors
            Pixel red_color(255, 0, 0);       // Red for detected landmarks
            Pixel green_color(0, 255, 0);     // Green for ground truth landmarks
            Pixel magenta_color(255, 0, 255); // Magenta for error lines
            Pixel white_color(255, 255, 255); // White for landmark numbers

            const auto& ground_truth = example.landmarks;

            // Draw error lines first (magenta lines from detected to ground truth)
            if (!detected_landmarks.empty() && !ground_truth.empty())
            {
                for (size_t i = 0; i < std::min(detected_landmarks.size(), ground_truth.size()); ++i)
                {
                    const auto& detected = detected_landmarks[i];
                    const auto& gt = ground_truth[i];

                    // Generate line points using DDA algorithm
                    auto line_points = math_utils::DDA(static_cast<long>(detected.p.x), static_cast<long>(detected.p.y),
                                                       static_cast<long>(gt.x), static_cast<long>(gt.y));

                    // Draw the error line in magenta
                    debug_img->paintPoints(line_points, magenta_color);
                }
            }

            // Draw ground truth landmarks (green dots)
            for (size_t i = 0; i < std::min(ground_truth.size(), size_t(98)); ++i)
            {
                const auto& gt = ground_truth[i];
                int x = static_cast<int>(gt.x);
                int y = static_cast<int>(gt.y);

                // Draw a 3x3 pixel marker for ground truth
                for (int dx = -1; dx <= 1; dx++)
                {
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        int px = x + dx, py = y + dy;
                        if (px >= 0 && px < static_cast<int>(debug_img->info.width) && py >= 0
                            && py < static_cast<int>(debug_img->info.height))
                        {
                            debug_img->pxy(px, py, green_color.r, green_color.g, green_color.b, green_color.a);
                        }
                    }
                }

                // Draw landmark number every 10th landmark for reference
                if (i % 10 == 0 || i < 5) // First 5 and every 10th
                {
                    std::string label = "GT" + std::to_string(i);
                    linuxface::drawText(*debug_img, x + 3, y - 8, label, white_color, 1, false);
                }
            }

            // Draw detected landmarks (red dots) - slightly offset to see both
            for (size_t i = 0; i < std::min(detected_landmarks.size(), size_t(98)); ++i)
            {
                const auto& lm = detected_landmarks[i];
                int x = static_cast<int>(lm.p.x) + 1; // Slight offset
                int y = static_cast<int>(lm.p.y) + 1;

                // Draw a 3x3 pixel marker for detected landmarks
                for (int dx = -1; dx <= 1; dx++)
                {
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        int px = x + dx, py = y + dy;
                        if (px >= 0 && px < static_cast<int>(debug_img->info.width) && py >= 0
                            && py < static_cast<int>(debug_img->info.height))
                        {
                            debug_img->pxy(px, py, red_color.r, red_color.g, red_color.b, red_color.a);
                        }
                    }
                }

                // Draw landmark number every 10th landmark for reference
                if (i % 10 == 0 || i < 5) // First 5 and every 10th
                {
                    std::string label = "D" + std::to_string(i);
                    linuxface::drawText(*debug_img, x + 3, y + 8, label, white_color, 1, false);
                }
            }

            // Save the debug image
            std::stringstream filename;
            filename << "detection_viz_img" << std::setfill('0') << std::setw(3) << image_index << "_mne" << std::fixed
                     << std::setprecision(2) << mne << ".ppm";

            if (!debug_img->saveToDisk(filename.str()))
            {
                std::cerr << "Failed to save detection visualization: " << filename.str() << "\n";
            }
            else
            {
                std::cout << "Saved detection visualization: " << filename.str() << "\n";
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error saving detection visualization for image " << image_index << ": " << e.what() << "\n";
        }
    }

    void saveDetectionVisualizationWithFaceInfo(const WFLWExample& example,
                                                const std::vector<FaceLandmark>& detected_landmarks, int image_index,
                                                double mne, int face_index, double iou, int total_faces) const
    {
        try
        {
            // Create a copy of the original image for drawing
            auto debug_img = example.image->deepCopy();

            // Define colors
            Pixel red_color(255, 0, 0);       // Red for detected landmarks
            Pixel green_color(0, 255, 0);     // Green for ground truth landmarks
            Pixel magenta_color(255, 0, 255); // Magenta for error lines
            Pixel white_color(255, 255, 255); // White for landmark numbers
            Pixel blue_color(0, 0, 255);      // Blue for bounding boxes
            Pixel yellow_color(255, 255, 0);  // Yellow for info text

            const auto& ground_truth = example.landmarks;
            const auto& gt_bbox = example.bounding_box;

            // Draw ground truth bounding box in cyan
            Pixel cyan = {0, 255, 255};

            // Draw rectangle using DDA lines for each edge
            auto top_line = math_utils::DDA(static_cast<long>(gt_bbox.l), static_cast<long>(gt_bbox.t),
                                            static_cast<long>(gt_bbox.r), static_cast<long>(gt_bbox.t));
            auto bottom_line = math_utils::DDA(static_cast<long>(gt_bbox.l), static_cast<long>(gt_bbox.b),
                                               static_cast<long>(gt_bbox.r), static_cast<long>(gt_bbox.b));
            auto left_line = math_utils::DDA(static_cast<long>(gt_bbox.l), static_cast<long>(gt_bbox.t),
                                             static_cast<long>(gt_bbox.l), static_cast<long>(gt_bbox.b));
            auto right_line = math_utils::DDA(static_cast<long>(gt_bbox.r), static_cast<long>(gt_bbox.t),
                                              static_cast<long>(gt_bbox.r), static_cast<long>(gt_bbox.b));

            debug_img->paintPoints(top_line, cyan);
            debug_img->paintPoints(bottom_line, cyan);
            debug_img->paintPoints(left_line, cyan);
            debug_img->paintPoints(right_line, cyan);

            // Draw error lines first (magenta lines from detected to ground truth)
            if (!detected_landmarks.empty() && !ground_truth.empty())
            {
                for (size_t i = 0; i < std::min(detected_landmarks.size(), ground_truth.size()); ++i)
                {
                    const auto& detected = detected_landmarks[i];
                    const auto& gt = ground_truth[i];

                    // Generate line points using DDA algorithm
                    auto line_points = math_utils::DDA(static_cast<long>(detected.p.x), static_cast<long>(detected.p.y),
                                                       static_cast<long>(gt.x), static_cast<long>(gt.y));

                    // Draw the error line in magenta
                    debug_img->paintPoints(line_points, magenta_color);
                }
            }

            // Draw ground truth landmarks (green dots)
            for (size_t i = 0; i < std::min(ground_truth.size(), size_t(98)); ++i)
            {
                const auto& gt = ground_truth[i];
                int x = static_cast<int>(gt.x);
                int y = static_cast<int>(gt.y);

                // Draw a 3x3 pixel marker for ground truth
                for (int dx = -1; dx <= 1; dx++)
                {
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        int px = x + dx, py = y + dy;
                        if (px >= 0 && px < static_cast<int>(debug_img->info.width) && py >= 0
                            && py < static_cast<int>(debug_img->info.height))
                        {
                            debug_img->pxy(px, py, green_color.r, green_color.g, green_color.b, green_color.a);
                        }
                    }
                }

                // Draw landmark number every 10th landmark for reference
                if (i % 10 == 0 || i < 5) // First 5 and every 10th
                {
                    std::string label = "GT" + std::to_string(i);
                    linuxface::drawText(*debug_img, x + 3, y - 8, label, white_color, 1, false);
                }
            }

            // Draw detected landmarks (red dots) - slightly offset to see both
            for (size_t i = 0; i < std::min(detected_landmarks.size(), size_t(98)); ++i)
            {
                const auto& lm = detected_landmarks[i];
                int x = static_cast<int>(lm.p.x) + 1; // Slight offset
                int y = static_cast<int>(lm.p.y) + 1;

                // Draw a 3x3 pixel marker for detected landmarks
                for (int dx = -1; dx <= 1; dx++)
                {
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        int px = x + dx, py = y + dy;
                        if (px >= 0 && px < static_cast<int>(debug_img->info.width) && py >= 0
                            && py < static_cast<int>(debug_img->info.height))
                        {
                            debug_img->pxy(px, py, red_color.r, red_color.g, red_color.b, red_color.a);
                        }
                    }
                }

                // Draw landmark number every 10th landmark for reference
                if (i % 10 == 0 || i < 5) // First 5 and every 10th
                {
                    std::string label = "D" + std::to_string(i);
                    linuxface::drawText(*debug_img, x + 3, y + 8, label, white_color, 1, false);
                }
            }

            // Add info text overlay
            std::stringstream info_text;
            info_text << "Faces:" << total_faces << " Used:" << face_index << " IoU:" << std::fixed
                      << std::setprecision(2) << iou;
            linuxface::drawText(*debug_img, 10, 30, info_text.str(), yellow_color, 1, false);

            // Save the debug image
            std::stringstream filename;
            filename << "detection_viz_img" << std::setfill('0') << std::setw(3) << image_index << "_mne" << std::fixed
                     << std::setprecision(2) << mne << ".ppm";

            if (!debug_img->saveToDisk(filename.str()))
            {
                std::cerr << "Failed to save detection visualization: " << filename.str() << "\n";
            }
            else
            {
                std::cout << "Saved detection visualization: " << filename.str() << "\n";
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error saving detection visualization for image " << image_index << ": " << e.what() << "\n";
        }
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

        std::cout << "\n=== Single Image Pipeline Debug - Multi-Method Comparison ===\n";
        std::cout << "Image size: " << example.image->info.width << "x" << example.image->info.height << "\n";
        std::cout << "WFLW ground truth landmarks: " << example.landmarks.size() << "\n";

        // Method 1: Standard detect() - with original landmark capture
        Face face1 = face; // Make a copy for method 1
        pfld_detector_->detect(example.image, face1);
        auto landmarks1_original = face1.getLandmarks(); // Capture original 106 landmarks
        auto landmarks1 = face1.getLandmarks();

        std::vector<FaceLandmark> pfld_98_landmarks1;
        double mne1 = 0.0;
        double iod1 = calculateInterocularDistance(face1);

        if (landmarks1.size() == 106 && iod1 > 0.0)
        {
            std::cout << "=== PFLD->WFLW CONVERSION DEBUG ===\n";
            std::cout << "Original PFLD landmarks: " << landmarks1_original.size() << std::endl;

            // Show sample of original PFLD landmarks
            std::cout << "Sample original PFLD landmarks (key facial points):\n";
            std::vector<int> sample_indices = {0, 8, 16, 27, 30, 36, 42, 48, 54, 60, 66};
            for (int idx : sample_indices)
            {
                if (idx < static_cast<int>(landmarks1_original.size()))
                {
                    const auto& lm = landmarks1_original[idx];
                    std::cout << "  PFLD[" << idx << "]: (" << lm.p.x << "," << lm.p.y << ")\n";
                }
            }

            pfld_98_landmarks1 = LandmarkConverter::pfldToWflw(landmarks1);
            mne1 = calculateMNE(pfld_98_landmarks1, example.landmarks, iod1);
            std::cout << "Method 1 detect(): " << landmarks1.size() << " landmarks converted to "
                      << pfld_98_landmarks1.size() << ", IOD=" << iod1 << ", MNE=" << mne1 << "\n";

            // Show sample of converted WFLW landmarks
            std::cout << "Sample converted WFLW landmarks:\n";
            std::vector<int> wflw_sample_indices = {0, 8, 16, 30, 54, 60, 68, 76, 82};
            for (int idx : wflw_sample_indices)
            {
                if (idx < static_cast<int>(pfld_98_landmarks1.size()))
                {
                    const auto& lm = pfld_98_landmarks1[idx];
                    std::cout << "  Converted_WFLW[" << idx << "]: (" << lm.p.x << "," << lm.p.y << ")\n";
                }
            }

            // Show corresponding ground truth
            std::cout << "Corresponding WFLW ground truth:\n";
            for (int idx : wflw_sample_indices)
            {
                if (idx < static_cast<int>(example.landmarks.size()))
                {
                    const auto& gt = example.landmarks[idx];
                    std::cout << "  GroundTruth_WFLW[" << idx << "]: (" << gt.x << "," << gt.y << ")\n";
                }
            }
        }
        else
        {
            std::cout << "Method 1 detect(): FAILED - landmarks=" << landmarks1.size() << ", IOD=" << iod1 << "\n";
        }

        // Method 2: detectSimilar() - uses affine alignment
        Face face2 = face; // Make a copy for method 2
        pfld_detector_->detectSimilar(example.image, face2);
        auto landmarks2 = face2.getLandmarks();

        std::vector<FaceLandmark> pfld_98_landmarks2;
        double mne2 = 0.0;
        double iod2 = calculateInterocularDistance(face2);

        if (landmarks2.size() == 106 && iod2 > 0.0)
        {
            pfld_98_landmarks2 = LandmarkConverter::pfldToWflw(landmarks2);
            mne2 = calculateMNE(pfld_98_landmarks2, example.landmarks, iod2);
            std::cout << "Method 2 detectSimilar(): " << landmarks2.size() << " landmarks converted to "
                      << pfld_98_landmarks2.size() << ", IOD=" << iod2 << ", MNE=" << mne2 << "\n";
        }
        else
        {
            std::cout << "Method 2 detectSimilar(): FAILED - landmarks=" << landmarks2.size() << ", IOD=" << iod2
                      << "\n";
        }

        // Method 3: detectOpenCv() - uses OpenCV for affine alignment
        Face face3 = face; // Make a copy for method 3
        pfld_detector_->detectOpenCv(example.image, face3);
        auto landmarks3 = face3.getLandmarks();

        std::vector<FaceLandmark> pfld_98_landmarks3;
        double mne3 = 0.0;
        double iod3 = calculateInterocularDistance(face3);

        if (landmarks3.size() == 106 && iod3 > 0.0)
        {
            pfld_98_landmarks3 = LandmarkConverter::pfldToWflw(landmarks3);
            mne3 = calculateMNE(pfld_98_landmarks3, example.landmarks, iod3);
            std::cout << "Method 3 detectOpenCv(): " << landmarks3.size() << " landmarks converted to "
                      << pfld_98_landmarks3.size() << ", IOD=" << iod3 << ", MNE=" << mne3 << "\n";
        }
        else
        {
            std::cout << "Method 3 detectOpenCv(): FAILED - landmarks=" << landmarks3.size() << ", IOD=" << iod3
                      << "\n";
        }

        // Find the best performing method
        double best_mne = std::numeric_limits<double>::max();
        std::string best_method = "none";

        if (!pfld_98_landmarks1.empty() && mne1 < best_mne)
        {
            best_mne = mne1;
            best_method = "detect()";
        }
        if (!pfld_98_landmarks2.empty() && mne2 < best_mne)
        {
            best_mne = mne2;
            best_method = "detectSimilar()";
        }
        if (!pfld_98_landmarks3.empty() && mne3 < best_mne)
        {
            best_mne = mne3;
            best_method = "detectOpenCv()";
        }

        std::cout << "\nBest performing method: " << best_method << " with MNE=" << best_mne << "\n";

        // Only save debug image if SAVE_IMAGES environment variable is set
        const char* save_images_env = std::getenv("SAVE_IMAGES");
        if (save_images_env)
        {
            saveMultiMethodDebugImageWithMapping(example, landmarks1_original, pfld_98_landmarks1, pfld_98_landmarks2,
                                                 pfld_98_landmarks3, example.landmarks, mne1, mne2, mne3, 0,
                                                 "Multi_method_comparison");
        }
        else
        {
            std::cout << "Debug image not saved (set SAVE_IMAGES=1 to enable)\n";
        }

        // Basic validation - at least one method should work
        EXPECT_TRUE(!pfld_98_landmarks1.empty() || !pfld_98_landmarks2.empty() || !pfld_98_landmarks3.empty())
            << "All PFLD detection methods failed";
    }
}

// Comprehensive analysis of the entire dataset
TEST_F(WFLWIntegrationTest, ComprehensiveDatasetAnalysis)
{
    // Test ALL available images (limited by configuration)
    std::vector<int> all_indices;
    for (int i = 0; i < wflw_loader_->get_num_examples(); ++i)
    {
        all_indices.push_back(i);
    }

    std::cout << "\n========================================\n";
    std::cout << "COMPREHENSIVE DATASET ANALYSIS\n";
    std::cout << "========================================\n";
    std::cout << "Total images to analyze: " << all_indices.size() << std::endl;

    auto results = runBenchmarkOnSubset(all_indices, true, true); // Enable progress reporting AND debug images

    // Print detailed results
    std::cout << "\n=== OVERALL PERFORMANCE METRICS ===\n";
    std::cout << "Success Rate: " << results.success_rate << "%\n";
    std::cout << "Successful Detections: " << results.successful_detections << "/" << results.total_samples << "\n";
    std::cout << "Mean MNE: " << results.mean_mne << "\n";
    std::cout << "Median MNE: " << results.median_mne << "\n";
    std::cout << "Std Dev MNE: " << results.std_dev_mne << "\n";

    std::cout << "\n=== PERFORMANCE DISTRIBUTION ===\n";
    if (results.successful_detections > 0)
    {
        std::cout << "Excellent (MNE < 0.03): " << results.excellent_count << " ("
                  << (100.0 * results.excellent_count / results.successful_detections) << "%)\n";
        std::cout << "Good (0.03 <= MNE < 0.05): " << results.good_count << " ("
                  << (100.0 * results.good_count / results.successful_detections) << "%)\n";
        std::cout << "Fair (0.05 <= MNE < 0.08): " << results.fair_count << " ("
                  << (100.0 * results.fair_count / results.successful_detections) << "%)\n";
        std::cout << "Poor (MNE >= 0.08): " << results.poor_count << " ("
                  << (100.0 * results.poor_count / results.successful_detections) << "%)\n";
    }

    std::cout << "\n=== TIMING ANALYSIS ===\n";
    std::cout << "Average SCRFD Time: " << results.avg_scrfd_time_ms << " ms\n";
    std::cout << "Average PFLD Time: " << results.avg_pfld_time_ms << " ms\n";
    std::cout << "Total Pipeline Time: " << results.total_pipeline_time_ms << " ms\n";

    std::cout << "\n=== FAILURE ANALYSIS ===\n";
    std::cout << "Image Load Failures: " << results.image_load_failures << " ("
              << (100.0 * results.image_load_failures / results.total_samples) << "%)\n";
    std::cout << "SCRFD Detection Failures: " << results.scrfd_failures << " ("
              << (100.0 * results.scrfd_failures / results.total_samples) << "%)\n";
    std::cout << "PFLD Landmark Failures: " << results.pfld_failures << " ("
              << (100.0 * results.pfld_failures / results.total_samples) << "%)\n";
    std::cout << "Invalid Interocular Distance: " << results.iod_failures << " ("
              << (100.0 * results.iod_failures / results.total_samples) << "%)\n";
    std::cout << "Landmarks Out of Bounds: " << results.landmark_bound_failures << " ("
              << (100.0 * results.landmark_bound_failures / results.total_samples) << "%)\n";

    std::cout << "\n=== FAILURE BY ATTRIBUTES ===\n";
    std::cout << "Pose Variation Failures: " << results.attribute_failures.pose_failures << "\n";
    std::cout << "Expression Failures: " << results.attribute_failures.expression_failures << "\n";
    std::cout << "Illumination Failures: " << results.attribute_failures.illumination_failures << "\n";
    std::cout << "Makeup Failures: " << results.attribute_failures.makeup_failures << "\n";
    std::cout << "Occlusion Failures: " << results.attribute_failures.occlusion_failures << "\n";
    std::cout << "Blur Failures: " << results.attribute_failures.blur_failures << "\n";

    if (!results.worst_examples.empty())
    {
        std::cout << "\n=== WORST PERFORMING EXAMPLES ===\n";
        for (size_t i = 0; i < results.worst_examples.size(); ++i)
        {
            const auto& example = results.worst_examples[i];
            std::cout << (i + 1) << ". Index " << example.index << " - MNE: " << example.mne_score;
            std::cout << " [Pose:" << example.attributes[0] << " Expr:" << example.attributes[1]
                      << " Illum:" << example.attributes[2] << " Makeup:" << example.attributes[3]
                      << " Occl:" << example.attributes[4] << " Blur:" << example.attributes[5] << "]\n";
        }
    }

    // Generate summary CSV file for further analysis
    std::ofstream csv_file("comprehensive_analysis_results.csv");
    csv_file << "Metric,Value,Percentage\n";
    csv_file << "Total Samples," << results.total_samples << ",100.0\n";
    csv_file << "Successful Detections," << results.successful_detections << "," << results.success_rate << "\n";
    csv_file << "Image Load Failures," << results.image_load_failures << ","
             << (100.0 * results.image_load_failures / results.total_samples) << "\n";
    csv_file << "SCRFD Failures," << results.scrfd_failures << ","
             << (100.0 * results.scrfd_failures / results.total_samples) << "\n";
    csv_file << "PFLD Failures," << results.pfld_failures << ","
             << (100.0 * results.pfld_failures / results.total_samples) << "\n";
    csv_file << "IOD Failures," << results.iod_failures << "," << (100.0 * results.iod_failures / results.total_samples)
             << "\n";
    csv_file << "Bound Failures," << results.landmark_bound_failures << ","
             << (100.0 * results.landmark_bound_failures / results.total_samples) << "\n";
    csv_file.close();

    std::cout << "\n=== ANALYSIS COMPLETE ===\n";
    std::cout << "Results saved to: comprehensive_analysis_results.csv\n";

    // Set reasonable expectations for the dataset based on sample size
    double expected_success_rate = results.total_samples >= 1000 ? 60.0 : 65.0;
    double expected_mean_mne = results.total_samples >= 1000 ? 0.08 : 0.07;

    EXPECT_GT(results.success_rate, expected_success_rate)
        << "Overall success rate too low for " << results.total_samples << " samples: " << results.success_rate << "%";
    EXPECT_LT(results.mean_mne, expected_mean_mne)
        << "Overall mean error too high for " << results.total_samples << " samples: " << results.mean_mne;
}

// Detailed attribute-by-attribute analysis
TEST_F(WFLWIntegrationTest, AttributeBasedAnalysis)
{
    std::cout << "\n========================================\n";
    std::cout << "ATTRIBUTE-BASED PERFORMANCE ANALYSIS\n";
    std::cout << "========================================\n";

    // Analyze by individual attributes
    struct AttributeComparison
    {
        std::string name;
        std::vector<int> normal_indices;
        std::vector<int> challenging_indices;
    };

    std::vector<AttributeComparison> comparisons = {
        {"POSE",         wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true), // Normal pose
         wflw_loader_->getExamplesByAttribute(false, true,  true,  true,  true,  true) }, // Challenging pose
        {"EXPRESSION",   wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true), // Normal expression
         wflw_loader_->getExamplesByAttribute(true,  false, true,  true,  true,  true) }, // Challenging expression
        {"ILLUMINATION",
         wflw_loader_->getExamplesByAttribute(true,                 true, true, true, true, true), // Normal illumination
         wflw_loader_->getExamplesByAttribute(true,  true,  false, true,  true,  true) }, // Challenging illumination
        {"MAKEUP",       wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true), // No makeup
         wflw_loader_->getExamplesByAttribute(true,  true,  true,  false, true,  true) }, // With makeup
        {"OCCLUSION",    wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true), // No occlusion
         wflw_loader_->getExamplesByAttribute(true,  true,  true,  true,  false, true) }, // With occlusion
        {"BLUR",         wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true), // Clear
         wflw_loader_->getExamplesByAttribute(true,  true,  true,  true,  true,  false)}  // Blurred
    };

    // CSV output for detailed analysis
    std::ofstream attr_csv("attribute_analysis.csv");
    attr_csv << "Attribute,Condition,Sample_Size,Success_Rate,Mean_MNE,Median_MNE,SCRFD_Failures,PFLD_Failures,"
                "Excellent,Good,Fair,Poor\n";

    for (const auto& comp : comparisons)
    {
        if (!comp.normal_indices.empty())
        {
            // Limit sample size for testing performance
            std::vector<int> normal_sample = comp.normal_indices;
            if (normal_sample.size() > 100)
            {
                normal_sample.resize(100);
            }

            auto normal_results = runBenchmarkOnSubset(normal_sample);
            printAttributeAnalysis(comp.name + " - NORMAL", normal_sample, normal_results);

            attr_csv << comp.name << ",Normal," << normal_sample.size() << "," << normal_results.success_rate << ","
                     << normal_results.mean_mne << "," << normal_results.median_mne << ","
                     << normal_results.scrfd_failures << "," << normal_results.pfld_failures << ","
                     << normal_results.excellent_count << "," << normal_results.good_count << ","
                     << normal_results.fair_count << "," << normal_results.poor_count << "\n";
        }

        if (!comp.challenging_indices.empty())
        {
            // Limit sample size for testing performance
            std::vector<int> challenging_sample = comp.challenging_indices;
            if (challenging_sample.size() > 100)
            {
                challenging_sample.resize(100);
            }

            auto challenging_results = runBenchmarkOnSubset(challenging_sample);
            printAttributeAnalysis(comp.name + " - CHALLENGING", challenging_sample, challenging_results);

            attr_csv << comp.name << ",Challenging," << challenging_sample.size() << ","
                     << challenging_results.success_rate << "," << challenging_results.mean_mne << ","
                     << challenging_results.median_mne << "," << challenging_results.scrfd_failures << ","
                     << challenging_results.pfld_failures << "," << challenging_results.excellent_count << ","
                     << challenging_results.good_count << "," << challenging_results.fair_count << ","
                     << challenging_results.poor_count << "\n";
        }
    }

    attr_csv.close();
    std::cout << "\nAttribute analysis saved to: attribute_analysis.csv\n";
}

// Generate detailed failure report for debugging
TEST_F(WFLWIntegrationTest, FailureDebuggingReport)
{
    std::cout << "\n========================================\n";
    std::cout << "FAILURE DEBUGGING REPORT\n";
    std::cout << "========================================\n";

    // Test a representative sample for debugging
    std::vector<int> debug_indices;
    for (int i = 0; i < std::min(200, wflw_loader_->get_num_examples()); i += 5)
    {
        debug_indices.push_back(i);
    }

    std::ofstream debug_report("failure_debug_report.csv");
    debug_report << "Index,Image_Name,Success,Failure_Type,MNE_Score,Pose,Expression,Illumination,Makeup,Occlusion,"
                    "Blur,SCRFD_Time_ms,PFLD_Time_ms,Num_Faces,Num_Landmarks,IOD\n";

    int total_processed = 0;
    int scrfd_failures = 0;
    int pfld_failures = 0;
    int iod_failures = 0;
    int bound_failures = 0;
    int successes = 0;

    for (int idx : debug_indices)
    {
        WFLWExample example;
        if (!wflw_loader_->load_example(idx, example))
        {
            debug_report << idx << ",FAILED_TO_LOAD,0,IMAGE_LOAD_FAILURE,,,,,,,,,,,\n";
            continue;
        }

        total_processed++;

        // Face detection timing
        auto start_time = std::chrono::high_resolution_clock::now();
        auto detected_faces = scrfd_detector_->detect(example.image);
        auto scrfd_end = std::chrono::high_resolution_clock::now();
        auto scrfd_duration = std::chrono::duration_cast<std::chrono::microseconds>(scrfd_end - start_time);
        double scrfd_time_ms = scrfd_duration.count() / 1000.0;

        if (detected_faces.empty())
        {
            scrfd_failures++;
            debug_report << idx << "," << example.image_name << ",0,SCRFD_NO_FACE,,";
            debug_report << example.attributes[0] << "," << example.attributes[1] << "," << example.attributes[2] << ","
                         << example.attributes[3] << "," << example.attributes[4] << "," << example.attributes[5]
                         << ",";
            debug_report << scrfd_time_ms << ",0,0,0,0\n";
            continue;
        }

        Face& face = detected_faces[0];

        // Landmark detection timing
        auto pfld_start = std::chrono::high_resolution_clock::now();
        pfld_detector_->detect(example.image, face);
        auto pfld_end = std::chrono::high_resolution_clock::now();
        auto pfld_duration = std::chrono::duration_cast<std::chrono::microseconds>(pfld_end - pfld_start);
        double pfld_time_ms = pfld_duration.count() / 1000.0;

        auto predicted_landmarks = face.getLandmarks();

        if (predicted_landmarks.size() != 106)
        {
            pfld_failures++;
            debug_report << idx << "," << example.image_name << ",0,PFLD_WRONG_LANDMARK_COUNT,,";
            debug_report << example.attributes[0] << "," << example.attributes[1] << "," << example.attributes[2] << ","
                         << example.attributes[3] << "," << example.attributes[4] << "," << example.attributes[5]
                         << ",";
            debug_report << scrfd_time_ms << "," << pfld_time_ms << "," << detected_faces.size() << ","
                         << predicted_landmarks.size() << ",0\n";
            continue;
        }

        double iod = calculateInterocularDistance(face);
        if (iod <= 0.0)
        {
            iod_failures++;
            debug_report << idx << "," << example.image_name << ",0,IOD_INVALID,,";
            debug_report << example.attributes[0] << "," << example.attributes[1] << "," << example.attributes[2] << ","
                         << example.attributes[3] << "," << example.attributes[4] << "," << example.attributes[5]
                         << ",";
            debug_report << scrfd_time_ms << "," << pfld_time_ms << "," << detected_faces.size() << ","
                         << predicted_landmarks.size() << "," << iod << "\n";
            continue;
        }

        // Check bounds
        bool landmarks_valid = true;
        for (const auto& landmark : predicted_landmarks)
        {
            if (landmark.p.x < 0.0 || landmark.p.x >= example.image->info.width || landmark.p.y < 0.0
                || landmark.p.y >= example.image->info.height)
            {
                landmarks_valid = false;
                break;
            }
        }

        if (!landmarks_valid)
        {
            bound_failures++;
            debug_report << idx << "," << example.image_name << ",0,LANDMARKS_OUT_OF_BOUNDS,,";
            debug_report << example.attributes[0] << "," << example.attributes[1] << "," << example.attributes[2] << ","
                         << example.attributes[3] << "," << example.attributes[4] << "," << example.attributes[5]
                         << ",";
            debug_report << scrfd_time_ms << "," << pfld_time_ms << "," << detected_faces.size() << ","
                         << predicted_landmarks.size() << "," << iod << "\n";
            continue;
        }

        // Success case - calculate MNE
        std::vector<FaceLandmark> pfld_98_landmarks(predicted_landmarks.begin(), predicted_landmarks.begin() + 98);
        double mne = calculateMNE(pfld_98_landmarks, example.landmarks, iod);

        if (mne >= 0.0)
        {
            successes++;
            debug_report << idx << "," << example.image_name << ",1,SUCCESS," << mne << ",";
            debug_report << example.attributes[0] << "," << example.attributes[1] << "," << example.attributes[2] << ","
                         << example.attributes[3] << "," << example.attributes[4] << "," << example.attributes[5]
                         << ",";
            debug_report << scrfd_time_ms << "," << pfld_time_ms << "," << detected_faces.size() << ","
                         << predicted_landmarks.size() << "," << iod << "\n";
        }
    }

    debug_report.close();

    std::cout << "\n=== DEBUG SUMMARY ===\n";
    std::cout << "Total Processed: " << total_processed << "\n";
    std::cout << "Successes: " << successes << " (" << (100.0 * successes / total_processed) << "%)\n";
    std::cout << "SCRFD Failures: " << scrfd_failures << " (" << (100.0 * scrfd_failures / total_processed) << "%)\n";
    std::cout << "PFLD Failures: " << pfld_failures << " (" << (100.0 * pfld_failures / total_processed) << "%)\n";
    std::cout << "IOD Failures: " << iod_failures << " (" << (100.0 * iod_failures / total_processed) << "%)\n";
    std::cout << "Bound Failures: " << bound_failures << " (" << (100.0 * bound_failures / total_processed) << "%)\n";
    std::cout << "\nDetailed failure report saved to: failure_debug_report.csv\n";
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

TEST_F(WFLWIntegrationTest, MultiImageDetectionVisualization)
{
    std::cout << "\n=== Multi-Image Detection-Only Visualization ===\n";
    std::cout << "Processing multiple images from WFLW dataset for landmark detection visualization\n";

    // Check if images should be saved
    const char* save_images_env = std::getenv("SAVE_IMAGES");
    if (!save_images_env)
    {
        std::cout << "Set SAVE_IMAGES=1 to save debug visualizations\n";
        return;
    }

    // Select a diverse set of images to visualize (first 10 images)
    int num_images_to_process = std::min(10, wflw_loader_->get_num_examples());
    std::cout << "Processing " << num_images_to_process << " images for visualization\n";

    for (int img_idx = 0; img_idx < num_images_to_process; ++img_idx)
    {
        WFLWExample example;
        if (!wflw_loader_->load_example(img_idx, example))
        {
            std::cout << "Failed to load image " << img_idx << ", skipping...\n";
            continue;
        }

        ASSERT_TRUE(example.image != nullptr);
        std::cout << "\nProcessing image " << img_idx << " (" << example.image->info.width << "x"
                  << example.image->info.height << ")\n";

        // Face detection
        auto detected_faces = scrfd_detector_->detect(example.image);
        if (detected_faces.empty())
        {
            std::cout << "No faces detected in image " << img_idx << ", skipping...\n";
            continue;
        }

        std::cout << "Detected " << detected_faces.size() << " faces\n";

        // Find the best matching face for the ground truth landmarks
        Face* best_matching_face = nullptr;
        double best_overlap = 0.0;
        int best_face_idx = -1;

        // Convert WFLW bounding box to Face bounding box format for comparison
        const auto& gt_bbox = example.bounding_box;
        std::cout << "Ground truth bbox: [" << gt_bbox.l << "," << gt_bbox.t << "," << gt_bbox.r << "," << gt_bbox.b
                  << "]\n";

        for (size_t face_idx = 0; face_idx < detected_faces.size(); ++face_idx)
        {
            auto& face = detected_faces[face_idx];
            const auto& det_bbox = face.getBoundingBox();

            std::cout << "Detected face " << face_idx << " bbox: [" << det_bbox.rect.l << "," << det_bbox.rect.t << ","
                      << det_bbox.rect.r << "," << det_bbox.rect.b << "]\n";

            // Calculate IoU (Intersection over Union) between detected face and ground truth
            double x1 = std::max(static_cast<double>(det_bbox.rect.l), gt_bbox.l);
            double y1 = std::max(static_cast<double>(det_bbox.rect.t), gt_bbox.t);
            double x2 = std::min(static_cast<double>(det_bbox.rect.r), gt_bbox.r);
            double y2 = std::min(static_cast<double>(det_bbox.rect.b), gt_bbox.b);

            if (x1 < x2 && y1 < y2)
            {
                double intersection = (x2 - x1) * (y2 - y1);
                double det_area = (det_bbox.rect.r - det_bbox.rect.l) * (det_bbox.rect.b - det_bbox.rect.t);
                double gt_area = (gt_bbox.r - gt_bbox.l) * (gt_bbox.b - gt_bbox.t);
                double union_area = det_area + gt_area - intersection;
                double iou = intersection / union_area;

                std::cout << "Face " << face_idx << " IoU: " << std::fixed << std::setprecision(3) << iou << "\n";

                if (iou > best_overlap)
                {
                    best_overlap = iou;
                    best_matching_face = &face;
                    best_face_idx = static_cast<int>(face_idx);
                }
            }
            else
            {
                std::cout << "Face " << face_idx << " IoU: 0.000 (no overlap)\n";
            }
        }

        if (!best_matching_face || best_overlap < 0.1) // Require at least 10% overlap
        {
            std::cout << "No good face match found (best IoU: " << best_overlap << "), skipping image " << img_idx
                      << "\n";
            continue;
        }

        std::cout << "Using face " << best_face_idx << " (IoU: " << std::fixed << std::setprecision(3) << best_overlap
                  << ")\n";

        // Landmark detection using standard detect() method on the best matching face
        pfld_detector_->detect(example.image, *best_matching_face);
        auto pfld_landmarks = best_matching_face->getLandmarks();

        if (pfld_landmarks.size() != 106)
        {
            std::cout << "Invalid landmark count for image " << img_idx << ": " << pfld_landmarks.size()
                      << ", skipping...\n";
            continue;
        }

        // Convert to WFLW format for comparison
        auto wflw_landmarks = LandmarkConverter::pfldToWflw(pfld_landmarks);
        double iod = calculateInterocularDistance(*best_matching_face);
        double mne = (iod > 0.0) ? calculateMNE(wflw_landmarks, example.landmarks, iod) : -1.0;

        std::cout << "Landmarks detected: " << pfld_landmarks.size() << " -> " << wflw_landmarks.size()
                  << ", IOD: " << std::fixed << std::setprecision(2) << iod << ", MNE: " << mne << "\n";

        // Create visualization with detected landmarks and error lines to ground truth
        saveDetectionVisualizationWithFaceInfo(example, wflw_landmarks, img_idx, mne, best_face_idx, best_overlap,
                                               detected_faces.size());
    }
}
