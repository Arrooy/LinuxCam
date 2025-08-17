/**
 * COMPREHENSIVE DATASET ANALYSIS
 *
 * This test performs extensive analysis of the WFLW dataset using the SCRFD+PFLD pipeline.
 * Based on the original comprehensive test but refactored to use WFLWTestBase.
 */

#include "wflw_test_base.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <numeric>

#include "LinuxFace/landmark_converter.h"

using namespace linuxface;
using namespace linuxface::test;

class ComprehensiveDatasetAnalysis : public WFLWTestBase
{
  protected:
    struct DetailedBenchmarkResults : public BenchmarkResults
    {
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

    DetailedBenchmarkResults runDetailedBenchmark(const std::vector<int>& example_indices, bool show_progress = false,
                                                  bool save_debug_images = false) const
    {
        DetailedBenchmarkResults results;
        results.total_samples = example_indices.size();
        results.individual_mne_scores.reserve(example_indices.size());

        double total_scrfd_time = 0.0;
        double total_pfld_time = 0.0;

        // Progress tracking
        int progress_interval = std::max(1, static_cast<int>(example_indices.size() / 20));
        auto last_progress_time = std::chrono::steady_clock::now();

        for (size_t i = 0; i < example_indices.size(); ++i)
        {
            int idx = example_indices[i];
            WFLWExample example;

            if (!wflw_loader_->load_example(idx, example))
            {
                results.image_load_failures++;
                continue;
            }

            // Progress reporting
            if (show_progress && (i % progress_interval == 0 || i == example_indices.size() - 1))
            {
                auto current_time = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - last_progress_time);

                if (elapsed.count() >= 2 || i == example_indices.size() - 1)
                {
                    double progress = (double) (i + 1) / example_indices.size() * 100.0;
                    std::cout << "Progress: " << std::fixed << std::setprecision(1) << progress << "% (" << (i + 1)
                              << "/" << example_indices.size() << ")" << std::endl;
                    last_progress_time = current_time;
                }
            }

            // Face detection with timing
            auto scrfd_start = std::chrono::high_resolution_clock::now();
            auto detected_faces = scrfd_detector_->detect(example.image);
            auto scrfd_end = std::chrono::high_resolution_clock::now();

            double scrfd_time = std::chrono::duration<double, std::milli>(scrfd_end - scrfd_start).count();
            total_scrfd_time += scrfd_time;

            if (detected_faces.empty())
            {
                results.scrfd_failures++;

                // Track attribute-specific failures
                if (example.attributes[0] == 0)
                {
                    results.attribute_failures.pose_failures++;
                }
                if (example.attributes[1] == 0)
                {
                    results.attribute_failures.expression_failures++;
                }
                if (example.attributes[2] == 0)
                {
                    results.attribute_failures.illumination_failures++;
                }
                if (example.attributes[3] == 0)
                {
                    results.attribute_failures.makeup_failures++;
                }
                if (example.attributes[4] == 0)
                {
                    results.attribute_failures.occlusion_failures++;
                }
                if (example.attributes[5] == 0)
                {
                    results.attribute_failures.blur_failures++;
                }

                continue;
            }

            // Find best matching face
            auto face_match = findBestMatchingFace(detected_faces, example.bounding_box, 0.3);
            if (!face_match.found_match)
            {
                results.scrfd_failures++;
                continue;
            }

            Face& best_face = *face_match.best_face;

            // Landmark detection with timing
            auto pfld_start = std::chrono::high_resolution_clock::now();
            pfld_detector_->detect(example.image, best_face);
            auto pfld_end = std::chrono::high_resolution_clock::now();

            double pfld_time = std::chrono::duration<double, std::milli>(pfld_end - pfld_start).count();
            total_pfld_time += pfld_time;

            auto landmarks = best_face.getLandmarks();
            if (landmarks.size() != 106)
            {
                results.pfld_failures++;
                continue;
            }

            // Convert to 98-point WFLW format
            std::vector<FaceLandmark> wflw_landmarks;
            try
            {
                wflw_landmarks = linuxface::LandmarkConverter::pfldToWflw(landmarks);
            }
            catch (const std::exception& e)
            {
                results.pfld_failures++;
                continue;
            }

            if (wflw_landmarks.size() != 98)
            {
                results.pfld_failures++;
                continue;
            }

            // Calculate interocular distance
            double iod = calculateInterocularDistance(best_face);
            if (iod <= 0.0)
            {
                results.iod_failures++;
                continue;
            }

            // Check if landmarks are within image bounds
            bool landmarks_valid = true;
            for (const auto& landmark : wflw_landmarks)
            {
                if (landmark.p.x < 0 || landmark.p.x >= example.image->info.width || landmark.p.y < 0
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

            // Calculate MNE
            double mne = calculateMNE(wflw_landmarks, example.landmarks, iod);
            if (mne < 0.0)
            {
                continue; // Invalid MNE calculation
            }

            // Success - record metrics
            results.successful_detections++;
            results.individual_mne_scores.push_back(mne);

            // Categorize by MNE range
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
            }

            // Track worst examples
            if (mne > 0.08 || results.worst_examples.size() < 10)
            {
                DetailedBenchmarkResults::FailureExample failure_example;
                failure_example.index = idx;
                failure_example.mne_score = mne;
                failure_example.failure_reason = (mne > 0.08) ? "High MNE" : "Normal";
                for (int j = 0; j < 6; ++j)
                {
                    failure_example.attributes[j] = example.attributes[j];
                }
                results.worst_examples.push_back(failure_example);
            }

            // Save debug visualization for problematic cases
            if (save_debug_images && mne > 0.1)
            {
                saveDetectionVisualizationWithFaceInfo(example, wflw_landmarks, idx, mne, face_match.face_index,
                                                       face_match.iou_score, detected_faces.size());
            }
        }

        // Sort worst examples by MNE score
        std::sort(results.worst_examples.begin(), results.worst_examples.end(),
                  [](const auto& a, const auto& b) { return a.mne_score > b.mne_score; });

        if (results.worst_examples.size() > 10)
        {
            results.worst_examples.resize(10);
        }

        // Calculate statistics
        if (!results.individual_mne_scores.empty())
        {
            results.mean_mne =
                std::accumulate(results.individual_mne_scores.begin(), results.individual_mne_scores.end(), 0.0)
                / results.individual_mne_scores.size();

            auto sorted_scores = results.individual_mne_scores;
            std::sort(sorted_scores.begin(), sorted_scores.end());
            size_t median_idx = sorted_scores.size() / 2;
            results.median_mne = (sorted_scores.size() % 2 == 0)
                                     ? (sorted_scores[median_idx - 1] + sorted_scores[median_idx]) / 2.0
                                     : sorted_scores[median_idx];

            double variance = 0.0;
            for (double score : results.individual_mne_scores)
            {
                variance += (score - results.mean_mne) * (score - results.mean_mne);
            }
            results.std_dev_mne = std::sqrt(variance / results.individual_mne_scores.size());
        }

        results.success_rate = (static_cast<double>(results.successful_detections) / results.total_samples) * 100.0;
        results.avg_scrfd_time_ms = total_scrfd_time / results.total_samples;
        results.avg_pfld_time_ms = total_pfld_time / results.total_samples;
        results.total_pipeline_time_ms = total_scrfd_time + total_pfld_time;

        return results;
    }

    void printDetailedResults(const DetailedBenchmarkResults& results) const
    {
        std::cout << "\n=== COMPREHENSIVE DATASET ANALYSIS RESULTS ===\n";
        std::cout << "Total images analyzed: " << results.total_samples << std::endl;
        std::cout << "Successful detections: " << results.successful_detections << "/" << results.total_samples << " ("
                  << results.success_rate << "%)" << std::endl;
        std::cout << "Mean MNE: " << std::fixed << std::setprecision(4) << results.mean_mne << std::endl;
        std::cout << "Median MNE: " << results.median_mne << std::endl;
        std::cout << "Std Dev MNE: " << results.std_dev_mne << std::endl;

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
    }

    void saveComprehensiveResultsCSV(const DetailedBenchmarkResults& results) const
    {
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
        csv_file << "IOD Failures," << results.iod_failures << ","
                 << (100.0 * results.iod_failures / results.total_samples) << "\n";
        csv_file << "Bound Failures," << results.landmark_bound_failures << ","
                 << (100.0 * results.landmark_bound_failures / results.total_samples) << "\n";
        csv_file.close();

        std::cout << "\nResults saved to: comprehensive_analysis_results.csv\n";
    }
};

// Test full dataset analysis
TEST_F(ComprehensiveDatasetAnalysis, FullDatasetAnalysis)
{
    // Get all available examples
    std::vector<int> all_indices;
    for (int i = 0; i < wflw_loader_->get_num_examples(); ++i)
    {
        all_indices.push_back(i);
    }

    std::cout << "\n========================================\n";
    std::cout << "COMPREHENSIVE DATASET ANALYSIS\n";
    std::cout << "========================================\n";
    std::cout << "Total images to analyze: " << all_indices.size() << std::endl;

    // Run comprehensive benchmark with progress reporting and debug image saving
    auto results = runDetailedBenchmark(all_indices, true, true);

    // Print detailed results
    printDetailedResults(results);

    // Save results to CSV
    saveComprehensiveResultsCSV(results);

    // Set reasonable expectations based on dataset size
    double expected_success_rate = results.total_samples >= 1000 ? 60.0 : 65.0;
    double expected_mean_mne = results.total_samples >= 1000 ? 0.08 : 0.07;

    EXPECT_GT(results.success_rate, expected_success_rate)
        << "Overall success rate too low for " << results.total_samples << " samples: " << results.success_rate << "%";
    EXPECT_LT(results.mean_mne, expected_mean_mne)
        << "Overall mean error too high for " << results.total_samples << " samples: " << results.mean_mne;
}

// Test attribute-based analysis
TEST_F(ComprehensiveDatasetAnalysis, AttributeBasedAnalysis)
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
        {"POSE",         wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true),
         wflw_loader_->getExamplesByAttribute(false, true,  true,  true,  true,  true) },
        {"EXPRESSION",   wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true),
         wflw_loader_->getExamplesByAttribute(true,  false, true,  true,  true,  true) },
        {"ILLUMINATION", wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true),
         wflw_loader_->getExamplesByAttribute(true,  true,  false, true,  true,  true) },
        {"MAKEUP",       wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true),
         wflw_loader_->getExamplesByAttribute(true,  true,  true,  false, true,  true) },
        {"OCCLUSION",    wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true),
         wflw_loader_->getExamplesByAttribute(true,  true,  true,  true,  false, true) },
        {"BLUR",         wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true),
         wflw_loader_->getExamplesByAttribute(true,  true,  true,  true,  true,  false)}
    };

    // CSV output for detailed analysis
    std::ofstream attr_csv("attribute_analysis.csv");
    attr_csv << "Attribute,Condition,Sample_Size,Success_Rate,Mean_MNE,Median_MNE,SCRFD_Failures,PFLD_Failures,"
                "Excellent,Good,Fair,Poor\n";

    for (const auto& comp : comparisons)
    {
        if (comp.normal_indices.empty() && comp.challenging_indices.empty())
        {
            std::cout << "\n" << comp.name << ": No samples found for analysis\n";
            continue;
        }

        // Analyze normal conditions
        if (!comp.normal_indices.empty())
        {
            auto normal_results = runDetailedBenchmark(comp.normal_indices);
            std::cout << "\n=== " << comp.name << " - NORMAL CONDITIONS ===\n";
            std::cout << "Sample Size: " << comp.normal_indices.size() << "\n";
            std::cout << "Success Rate: " << normal_results.success_rate << "%\n";
            std::cout << "Mean MNE: " << normal_results.mean_mne << "\n";

            attr_csv << comp.name << ",Normal," << comp.normal_indices.size() << "," << normal_results.success_rate
                     << "," << normal_results.mean_mne << "," << normal_results.median_mne << ","
                     << normal_results.scrfd_failures << "," << normal_results.pfld_failures << ","
                     << normal_results.excellent_count << "," << normal_results.good_count << ","
                     << normal_results.fair_count << "," << normal_results.poor_count << "\n";
        }

        // Analyze challenging conditions
        if (!comp.challenging_indices.empty())
        {
            auto challenging_results = runDetailedBenchmark(comp.challenging_indices);
            std::cout << "\n=== " << comp.name << " - CHALLENGING CONDITIONS ===\n";
            std::cout << "Sample Size: " << comp.challenging_indices.size() << "\n";
            std::cout << "Success Rate: " << challenging_results.success_rate << "%\n";
            std::cout << "Mean MNE: " << challenging_results.mean_mne << "\n";

            attr_csv << comp.name << ",Challenging," << comp.challenging_indices.size() << ","
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
