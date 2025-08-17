/**
 * PERFORMANCE BENCHMARKING TESTS
 *
 * Tests focused on performance metrics and benchmarking:
 * - Normal vs challenging conditions
 * - Timing analysis
 * - Error distribution analysis
 * - Success rate validation
 */

#include "wflw_test_base.h"

#include <algorithm>
#include <chrono>
#include <numeric>

class PerformanceBenchmarkTest : public WFLWTestBase
{
  protected:
    struct BenchmarkResults
    {
        double mean_mne = 0.0;
        double median_mne = 0.0;
        double std_dev_mne = 0.0;
        double success_rate = 0.0;
        int total_samples = 0;
        int successful_detections = 0;
        std::vector<double> individual_mne_scores;

        // Performance metrics
        double avg_scrfd_time_ms = 0.0;
        double avg_pfld_time_ms = 0.0;
        double total_pipeline_time_ms = 0.0;

        // Failure analysis
        int scrfd_failures = 0;
        int pfld_failures = 0;
        int face_match_failures = 0;
        int iod_failures = 0;
        int image_load_failures = 0;
    };

    BenchmarkResults runBenchmarkOnSubset(const std::vector<int>& example_indices, bool show_progress = false) const
    {
        BenchmarkResults results;
        results.total_samples = example_indices.size();
        results.individual_mne_scores.reserve(example_indices.size());

        double total_scrfd_time = 0.0;
        double total_pfld_time = 0.0;

        for (size_t i = 0; i < example_indices.size(); ++i)
        {
            int idx = example_indices[i];

            if (show_progress && i % 10 == 0)
            {
                std::cout << "Progress: " << i << "/" << example_indices.size() << " images processed\n";
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

            if (pfld_98_landmarks.size() != 98 || iod <= 0.0)
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

    void printBenchmarkResults(const std::string& title, const BenchmarkResults& results) const
    {
        std::cout << "\n=== " << title << " ===\n";
        std::cout << "Sample Size: " << results.total_samples << "\n";
        std::cout << "Success Rate: " << std::fixed << std::setprecision(1) << results.success_rate << "%\n";
        std::cout << "Successful Detections: " << results.successful_detections << "/" << results.total_samples << "\n";

        if (results.successful_detections > 0)
        {
            std::cout << "Mean MNE: " << std::fixed << std::setprecision(4) << results.mean_mne << "\n";
            std::cout << "Median MNE: " << std::fixed << std::setprecision(4) << results.median_mne << "\n";
            std::cout << "Std Dev MNE: " << std::fixed << std::setprecision(4) << results.std_dev_mne << "\n";
        }

        std::cout << "Average SCRFD Time: " << std::fixed << std::setprecision(2) << results.avg_scrfd_time_ms
                  << " ms\n";
        std::cout << "Average PFLD Time: " << std::fixed << std::setprecision(2) << results.avg_pfld_time_ms << " ms\n";
        std::cout << "Total Pipeline Time: " << std::fixed << std::setprecision(2) << results.total_pipeline_time_ms
                  << " ms\n";

        std::cout << "\n=== Failure Analysis ===\n";
        std::cout << "Image Load Failures: " << results.image_load_failures << "\n";
        std::cout << "SCRFD Failures: " << results.scrfd_failures << "\n";
        std::cout << "Face Match Failures: " << results.face_match_failures << "\n";
        std::cout << "PFLD Failures: " << results.pfld_failures << "\n";
        std::cout << "IOD Failures: " << results.iod_failures << "\n";
    }
};

TEST_F(PerformanceBenchmarkTest, BenchmarkNormalConditions)
{
    // Test on images with normal conditions (pose, expression, etc.)
    auto normal_indices = wflw_loader_->getExamplesByAttribute(true, true, true, true, true, true);

    if (normal_indices.empty())
    {
        GTEST_SKIP() << "No normal condition examples found in WFLW dataset";
    }

    // Limit to reasonable number for testing
    if (normal_indices.size() > 50)
    {
        normal_indices.resize(50);
    }

    auto results = runBenchmarkOnSubset(normal_indices, true);
    printBenchmarkResults("Normal Conditions Benchmark", results);

    EXPECT_GT(results.success_rate, 80.0) << "Low success rate on normal conditions: " << results.success_rate << "%";

    if (results.successful_detections > 0)
    {
        EXPECT_LT(results.mean_mne, 0.05) << "High mean error on normal conditions: " << results.mean_mne;
    }
}

TEST_F(PerformanceBenchmarkTest, BenchmarkChallengingConditions)
{
    // Test on challenging conditions (pose variations, occlusions, etc.)
    auto challenging_indices = wflw_loader_->getExamplesByAttribute(false, false, false, false, false, false);

    if (challenging_indices.empty())
    {
        GTEST_SKIP() << "No challenging condition examples found in WFLW dataset";
    }

    // Limit to reasonable number for testing
    if (challenging_indices.size() > 30)
    {
        challenging_indices.resize(30);
    }

    auto results = runBenchmarkOnSubset(challenging_indices, true);
    printBenchmarkResults("Challenging Conditions Benchmark", results);

    // More lenient thresholds for challenging conditions
    EXPECT_GT(results.success_rate, 50.0)
        << "Very low success rate on challenging conditions: " << results.success_rate << "%";

    if (results.successful_detections > 0)
    {
        EXPECT_LT(results.mean_mne, 0.10) << "Very high mean error on challenging conditions: " << results.mean_mne;
    }
}

TEST_F(PerformanceBenchmarkTest, TimingAnalysis)
{
    std::cout << "\n=== Timing Analysis Test ===\n";

    // Test timing on a subset of images
    std::vector<int> timing_indices;
    for (int i = 0; i < std::min(25, wflw_loader_->get_num_examples()); ++i)
    {
        timing_indices.push_back(i);
    }

    auto results = runBenchmarkOnSubset(timing_indices, false);

    std::cout << "Timing results for " << timing_indices.size() << " images:\n";
    std::cout << "SCRFD avg time: " << std::fixed << std::setprecision(2) << results.avg_scrfd_time_ms << " ms\n";
    std::cout << "PFLD avg time: " << std::fixed << std::setprecision(2) << results.avg_pfld_time_ms << " ms\n";
    std::cout << "Total pipeline time: " << std::fixed << std::setprecision(2) << results.total_pipeline_time_ms
              << " ms\n";

    // Basic timing expectations (these are quite lenient for different hardware)
    EXPECT_LT(results.avg_scrfd_time_ms, 500.0) << "SCRFD detection too slow: " << results.avg_scrfd_time_ms << " ms";
    EXPECT_LT(results.avg_pfld_time_ms, 100.0) << "PFLD detection too slow: " << results.avg_pfld_time_ms << " ms";
    EXPECT_LT(results.total_pipeline_time_ms, 600.0)
        << "Total pipeline too slow: " << results.total_pipeline_time_ms << " ms";
}

TEST_F(PerformanceBenchmarkTest, FaceMatchingEffectiveness)
{
    std::cout << "\n=== Face Matching Effectiveness Test ===\n";

    // Test the effectiveness of face matching by comparing with and without it
    std::vector<int> test_indices;
    for (int i = 0; i < std::min(20, wflw_loader_->get_num_examples()); ++i)
    {
        test_indices.push_back(i);
    }

    int images_with_multiple_faces = 0;
    int successful_matches = 0;
    double total_iou = 0.0;

    for (int idx : test_indices)
    {
        WFLWExample example;
        if (!wflw_loader_->load_example(idx, example) || !example.image)
        {
            continue;
        }

        auto detected_faces = scrfd_detector_->detect(example.image);
        if (detected_faces.size() < 2)
        {
            continue; // Skip single-face images
        }

        images_with_multiple_faces++;

        auto match_result = findBestMatchingFace(detected_faces, example.bounding_box, 0.1);
        if (match_result.found_match)
        {
            successful_matches++;
            total_iou += match_result.iou_score;

            std::cout << "Image " << idx << ": " << detected_faces.size() << " faces, selected face "
                      << match_result.face_index << " with IoU " << std::fixed << std::setprecision(3)
                      << match_result.iou_score << "\n";
        }
    }

    std::cout << "\n=== Face Matching Results ===\n";
    std::cout << "Images with multiple faces: " << images_with_multiple_faces << "\n";
    std::cout << "Successful matches: " << successful_matches << "\n";

    if (images_with_multiple_faces > 0)
    {
        double match_rate = 100.0 * successful_matches / images_with_multiple_faces;
        double avg_iou = successful_matches > 0 ? total_iou / successful_matches : 0.0;

        std::cout << "Match success rate: " << std::fixed << std::setprecision(1) << match_rate << "%\n";
        std::cout << "Average IoU of matches: " << std::fixed << std::setprecision(3) << avg_iou << "\n";

        EXPECT_GT(match_rate, 75.0) << "Face matching success rate too low: " << match_rate << "%";
        EXPECT_GT(avg_iou, 0.6) << "Average IoU of matches too low: " << avg_iou;
    }
    else
    {
        std::cout << "No multi-face images found in test subset\n";
        GTEST_SKIP() << "Insufficient multi-face images for effectiveness test";
    }
}
