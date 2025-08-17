/**
 * COMPREHENSIVE ANALYSIS TESTS
 *
 * Tests focused on comprehensive dataset analysis:
 * - Full dataset analysis with detailed metrics
 * - Attribute-based performance analysis
 * - Failure analysis and debugging
 * - CSV report generation
 */

#include "wflw_test_base.h"

#include <fstream>
#include <sstream>

class ComprehensiveAnalysisTest : public WFLWTestBase
{
  protected:
    struct ComprehensiveResults
    {
        double mean_mne = 0.0;
        double median_mne = 0.0;
        double std_dev_mne = 0.0;
        double success_rate = 0.0;
        int total_samples = 0;
        int successful_detections = 0;
        std::vector<double> individual_mne_scores;

        // Detailed failure analysis
        int image_load_failures = 0;
        int scrfd_failures = 0;
        int face_match_failures = 0;
        int pfld_failures = 0;
        int iod_failures = 0;
        int conversion_failures = 0;

        // Performance by MNE ranges
        int excellent_count = 0; // MNE < 0.03
        int good_count = 0;      // 0.03 <= MNE < 0.05
        int fair_count = 0;      // 0.05 <= MNE < 0.08
        int poor_count = 0;      // MNE >= 0.08

        // Timing metrics
        double avg_scrfd_time_ms = 0.0;
        double avg_pfld_time_ms = 0.0;
        double total_pipeline_time_ms = 0.0;
    };

    ComprehensiveResults
    runComprehensiveAnalysis(const std::vector<int>& example_indices, bool show_progress = true) const
    {
        ComprehensiveResults results;
        results.total_samples = example_indices.size();
        results.individual_mne_scores.reserve(example_indices.size());

        double total_scrfd_time = 0.0;
        double total_pfld_time = 0.0;
        int progress_interval = std::max(1, static_cast<int>(example_indices.size() / 20));

        for (size_t i = 0; i < example_indices.size(); ++i)
        {
            int idx = example_indices[i];

            if (show_progress && i % progress_interval == 0)
            {
                double progress = 100.0 * i / example_indices.size();
                std::cout << "Analysis progress: " << std::fixed << std::setprecision(1) << progress << "% (" << i
                          << "/" << example_indices.size() << ")\n";
            }

            WFLWExample example;
            if (!wflw_loader_->load_example(idx, example) || !example.image)
            {
                results.image_load_failures++;
                continue;
            }

            // Time SCRFD detection
            auto scrfd_start = std::chrono::high_resolution_clock::now();
            auto detected_faces = scrfd_detector_->detect(example.image);
            auto scrfd_end = std::chrono::high_resolution_clock::now();

            double scrfd_time = std::chrono::duration<double, std::milli>(scrfd_end - scrfd_start).count();
            total_scrfd_time += scrfd_time;

            if (detected_faces.empty())
            {
                results.scrfd_failures++;
                continue;
            }

            // Find best matching face
            auto match_result = findBestMatchingFace(detected_faces, example.bounding_box, 0.1);
            if (!match_result.found_match)
            {
                results.face_match_failures++;
                continue;
            }

            // Time PFLD detection
            auto pfld_start = std::chrono::high_resolution_clock::now();
            pfld_detector_->detect(example.image, *match_result.best_face);
            auto pfld_end = std::chrono::high_resolution_clock::now();

            double pfld_time = std::chrono::duration<double, std::milli>(pfld_end - pfld_start).count();
            total_pfld_time += pfld_time;

            auto landmarks = match_result.best_face->getLandmarks();
            if (landmarks.size() != 106)
            {
                results.pfld_failures++;
                continue;
            }

            // Convert and validate
            auto pfld_98_landmarks = LandmarkConverter::pfldToWflw(landmarks);
            double iod = calculateInterocularDistance(*match_result.best_face);

            if (pfld_98_landmarks.size() != 98)
            {
                results.conversion_failures++;
                continue;
            }

            if (iod <= 0.0)
            {
                results.iod_failures++;
                continue;
            }

            // Calculate MNE
            double mne = calculateMNE(pfld_98_landmarks, example.landmarks, iod);
            if (mne < 100.0) // Sanity check
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
                }
            }
        }

        // Calculate statistics
        if (!results.individual_mne_scores.empty())
        {
            std::sort(results.individual_mne_scores.begin(), results.individual_mne_scores.end());

            double sum =
                std::accumulate(results.individual_mne_scores.begin(), results.individual_mne_scores.end(), 0.0);
            results.mean_mne = sum / results.individual_mne_scores.size();
            results.median_mne = results.individual_mne_scores[results.individual_mne_scores.size() / 2];

            // Calculate standard deviation
            double sq_sum =
                std::inner_product(results.individual_mne_scores.begin(), results.individual_mne_scores.end(),
                                   results.individual_mne_scores.begin(), 0.0);
            results.std_dev_mne =
                std::sqrt(sq_sum / results.individual_mne_scores.size() - results.mean_mne * results.mean_mne);
        }

        results.success_rate = (static_cast<double>(results.successful_detections) / results.total_samples) * 100.0;
        results.avg_scrfd_time_ms = total_scrfd_time / results.total_samples;
        results.avg_pfld_time_ms = total_pfld_time / results.total_samples;
        results.total_pipeline_time_ms = total_scrfd_time + total_pfld_time;

        return results;
    }
};

TEST_F(ComprehensiveAnalysisTest, FullDatasetAnalysis)
{
    std::cout << "\n========================================\n";
    std::cout << "COMPREHENSIVE DATASET ANALYSIS\n";
    std::cout << "========================================\n";

    // Analyze all available images (limited by configuration)
    std::vector<int> all_indices;
    for (int i = 0; i < wflw_loader_->get_num_examples(); ++i)
    {
        all_indices.push_back(i);
    }

    std::cout << "Total images to analyze: " << all_indices.size() << std::endl;

    auto results = runComprehensiveAnalysis(all_indices, true);

    // Print detailed results
    std::cout << "\n=== OVERALL PERFORMANCE METRICS ===\n";
    std::cout << "Success Rate: " << std::fixed << std::setprecision(1) << results.success_rate << "%\n";
    std::cout << "Successful Detections: " << results.successful_detections << "/" << results.total_samples << "\n";

    if (results.successful_detections > 0)
    {
        std::cout << "Mean MNE: " << std::fixed << std::setprecision(4) << results.mean_mne << "\n";
        std::cout << "Median MNE: " << std::fixed << std::setprecision(4) << results.median_mne << "\n";
        std::cout << "Std Dev MNE: " << std::fixed << std::setprecision(4) << results.std_dev_mne << "\n";
    }

    std::cout << "\n=== PERFORMANCE DISTRIBUTION ===\n";
    if (results.successful_detections > 0)
    {
        std::cout << "Excellent (MNE < 0.03): " << results.excellent_count << " (" << std::fixed << std::setprecision(1)
                  << (100.0 * results.excellent_count / results.successful_detections) << "%)\n";
        std::cout << "Good (0.03 <= MNE < 0.05): " << results.good_count << " ("
                  << (100.0 * results.good_count / results.successful_detections) << "%)\n";
        std::cout << "Fair (0.05 <= MNE < 0.08): " << results.fair_count << " ("
                  << (100.0 * results.fair_count / results.successful_detections) << "%)\n";
        std::cout << "Poor (MNE >= 0.08): " << results.poor_count << " ("
                  << (100.0 * results.poor_count / results.successful_detections) << "%)\n";
    }

    std::cout << "\n=== TIMING ANALYSIS ===\n";
    std::cout << "Average SCRFD Time: " << std::fixed << std::setprecision(2) << results.avg_scrfd_time_ms << " ms\n";
    std::cout << "Average PFLD Time: " << std::fixed << std::setprecision(2) << results.avg_pfld_time_ms << " ms\n";
    std::cout << "Total Pipeline Time: " << std::fixed << std::setprecision(2) << results.total_pipeline_time_ms
              << " ms\n";

    std::cout << "\n=== FAILURE ANALYSIS ===\n";
    std::cout << "Image Load Failures: " << results.image_load_failures << " (" << std::fixed << std::setprecision(1)
              << (100.0 * results.image_load_failures / results.total_samples) << "%)\n";
    std::cout << "SCRFD Detection Failures: " << results.scrfd_failures << " ("
              << (100.0 * results.scrfd_failures / results.total_samples) << "%)\n";
    std::cout << "Face Match Failures: " << results.face_match_failures << " ("
              << (100.0 * results.face_match_failures / results.total_samples) << "%)\n";
    std::cout << "PFLD Landmark Failures: " << results.pfld_failures << " ("
              << (100.0 * results.pfld_failures / results.total_samples) << "%)\n";
    std::cout << "Conversion Failures: " << results.conversion_failures << " ("
              << (100.0 * results.conversion_failures / results.total_samples) << "%)\n";
    std::cout << "Invalid Interocular Distance: " << results.iod_failures << " ("
              << (100.0 * results.iod_failures / results.total_samples) << "%)\n";

    // Generate summary CSV file for further analysis
    std::ofstream csv_file("comprehensive_analysis_results.csv");
    csv_file << "Metric,Value,Percentage\n";
    csv_file << "Total Samples," << results.total_samples << ",100.0\n";
    csv_file << "Successful Detections," << results.successful_detections << "," << results.success_rate << "\n";
    csv_file << "Image Load Failures," << results.image_load_failures << ","
             << (100.0 * results.image_load_failures / results.total_samples) << "\n";
    csv_file << "SCRFD Failures," << results.scrfd_failures << ","
             << (100.0 * results.scrfd_failures / results.total_samples) << "\n";
    csv_file << "Face Match Failures," << results.face_match_failures << ","
             << (100.0 * results.face_match_failures / results.total_samples) << "\n";
    csv_file << "PFLD Failures," << results.pfld_failures << ","
             << (100.0 * results.pfld_failures / results.total_samples) << "\n";
    csv_file << "Conversion Failures," << results.conversion_failures << ","
             << (100.0 * results.conversion_failures / results.total_samples) << "\n";
    csv_file << "IOD Failures," << results.iod_failures << "," << (100.0 * results.iod_failures / results.total_samples)
             << "\n";
    csv_file.close();

    std::cout << "\n=== ANALYSIS COMPLETE ===\n";
    std::cout << "Results saved to: comprehensive_analysis_results.csv\n";

    // Set reasonable expectations based on the improved face matching
    double expected_success_rate = results.total_samples >= 1000 ? 75.0 : 80.0;
    double expected_mean_mne = results.total_samples >= 1000 ? 0.06 : 0.05;

    EXPECT_GT(results.success_rate, expected_success_rate)
        << "Overall success rate too low for " << results.total_samples << " samples: " << results.success_rate << "%";

    if (results.successful_detections > 0)
    {
        EXPECT_LT(results.mean_mne, expected_mean_mne)
            << "Overall mean error too high for " << results.total_samples << " samples: " << results.mean_mne;
    }
}

TEST_F(ComprehensiveAnalysisTest, AttributeBasedAnalysis)
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

    // Allow dynamic sample size control via environment variable
    int max_normal = 50;
    int max_challenging = 30;
    const char* env_max_normal = std::getenv("MAX_ATTRIBUTE_EXAMPLES_NORMAL");
    const char* env_max_challenging = std::getenv("MAX_ATTRIBUTE_EXAMPLES_CHALLENGING");
    if (env_max_normal) {
        max_normal = std::atoi(env_max_normal);
    }
    if (env_max_challenging) {
        max_challenging = std::atoi(env_max_challenging);
    }

    // CSV output for detailed analysis
    std::ofstream attr_csv("attribute_analysis.csv");
    attr_csv << "Attribute,Condition,Sample_Size,Success_Rate,Mean_MNE,Median_MNE,SCRFD_Failures,PFLD_Failures,"
                "Face_Match_Failures,Excellent,Good,Fair,Poor\n";

    for (const auto& comp : comparisons)
    {
        if (!comp.normal_indices.empty())
        {
            std::vector<int> normal_subset = comp.normal_indices;
            if (normal_subset.size() > max_normal)
            {
                normal_subset.resize(max_normal);
            }

            auto normal_results = runComprehensiveAnalysis(normal_subset, false);

            std::cout << "\n=== " << comp.name << " - NORMAL ===\n";
            std::cout << "Sample Size: " << normal_subset.size() << "\n";
            std::cout << "Success Rate: " << std::fixed << std::setprecision(1) << normal_results.success_rate << "%\n";
            if (normal_results.successful_detections > 0)
            {
                std::cout << "Mean MNE: " << std::fixed << std::setprecision(4) << normal_results.mean_mne << "\n";
                std::cout << "Median MNE: " << std::fixed << std::setprecision(4) << normal_results.median_mne << "\n";
            }

            attr_csv << comp.name << ",Normal," << normal_subset.size() << "," << normal_results.success_rate << ","
                     << normal_results.mean_mne << "," << normal_results.median_mne << ","
                     << normal_results.scrfd_failures << "," << normal_results.pfld_failures << ","
                     << normal_results.face_match_failures << "," << normal_results.excellent_count << ","
                     << normal_results.good_count << "," << normal_results.fair_count << ","
                     << normal_results.poor_count << "\n";
        }

        if (!comp.challenging_indices.empty())
        {
            std::vector<int> challenging_subset = comp.challenging_indices;
            if (challenging_subset.size() > max_challenging)
            {
                challenging_subset.resize(max_challenging);
            }

            auto challenging_results = runComprehensiveAnalysis(challenging_subset, false);

            std::cout << "\n=== " << comp.name << " - CHALLENGING ===\n";
            std::cout << "Sample Size: " << challenging_subset.size() << "\n";
            std::cout << "Success Rate: " << std::fixed << std::setprecision(1) << challenging_results.success_rate
                      << "%\n";
            if (challenging_results.successful_detections > 0)
            {
                std::cout << "Mean MNE: " << std::fixed << std::setprecision(4) << challenging_results.mean_mne << "\n";
                std::cout << "Median MNE: " << std::fixed << std::setprecision(4) << challenging_results.median_mne
                          << "\n";
            }

            attr_csv << comp.name << ",Challenging," << challenging_subset.size() << ","
                     << challenging_results.success_rate << "," << challenging_results.mean_mne << ","
                     << challenging_results.median_mne << "," << challenging_results.scrfd_failures << ","
                     << challenging_results.pfld_failures << "," << challenging_results.face_match_failures << ","
                     << challenging_results.excellent_count << "," << challenging_results.good_count << ","
                     << challenging_results.fair_count << "," << challenging_results.poor_count << "\n";
        }
    }

    attr_csv.close();
    std::cout << "\nAttribute analysis saved to: attribute_analysis.csv\n";
}
