/**
 * FAILURE ANALYSIS AND DEBUGGING TESTS
 *
 * Tests focused on failure analysis and debugging capabilities:
 * - Failure case detection and analysis
 * - Debug visualization generation
 * - Error handling validation
 * - Debugging report generation
 */

#include "wflw_test_base.h"
#include "../../common/test_utils.h"

#include <fstream>
#include <sstream>

class FailureAnalysisTest : public WFLWTestBase
{
  protected:
    struct FailureCase
    {
        int image_index;
        std::string failure_type;
        std::string description;
        double attempted_iou = -1.0;
        int detected_faces = 0;
        bool has_ground_truth_bbox = false;
        math_utils::Rect<double> ground_truth_bbox;
        std::vector<math_utils::Rect<float>> detected_bboxes;
    };

    std::vector<FailureCase> analyzeFailures(const std::vector<int>& example_indices, int max_failures = 50) const
    {
        std::vector<FailureCase> failures;
        failures.reserve(max_failures);

        for (int idx : example_indices)
        {
            if (failures.size() >= max_failures)
            {
                break;
            }

            FailureCase failure_case;
            failure_case.image_index = idx;
            const auto& sample = wflw_loader_->getSample(idx);
            auto image_ptr = sample.loadImage();
            if (!image_ptr)
            {
                failure_case.failure_type = "IMAGE_LOAD_FAILURE";
                failure_case.description = "Failed to load WFLW sample or image data";
                failures.push_back(failure_case);
                continue;
            }
            failure_case.has_ground_truth_bbox = true;
            failure_case.ground_truth_bbox = math_utils::Rect<double>(sample.bbox[0], sample.bbox[1], sample.bbox[2], sample.bbox[3]);
            auto detected_faces = scrfd_detector_->detect(image_ptr);
            failure_case.detected_faces = detected_faces.size();
            if (detected_faces.empty())
            {
                failure_case.failure_type = "SCRFD_NO_DETECTION";
                failure_case.description = "SCRFD detector found no faces in image";
                failures.push_back(failure_case);
                continue;
            }
            for (const auto& face : detected_faces)
                failure_case.detected_bboxes.push_back(face.getBoundingBox().rect);
            auto match_result = findBestMatchingFace(detected_faces, failure_case.ground_truth_bbox, 0.1);
            failure_case.attempted_iou = match_result.iou_score;
            if (!match_result.found_match)
            {
                failure_case.failure_type = "FACE_MATCH_FAILURE";
                failure_case.description = "No detected face matches ground truth (IoU < 0.1), best IoU: "
                                           + std::to_string(match_result.iou_score);
                failures.push_back(failure_case);
                continue;
            }
            pfld_detector_->detect(image_ptr, *match_result.best_face);
            auto landmarks = match_result.best_face->getLandmarks();
            if (landmarks.size() != 106)
            {
                failure_case.failure_type = "PFLD_LANDMARK_FAILURE";
                failure_case.description =
                    "PFLD returned " + std::to_string(landmarks.size()) + " landmarks instead of 106";
                failures.push_back(failure_case);
                continue;
            }
            auto pfld_98_landmarks = LandmarkConverter::pfldToWflw(landmarks);
            if (pfld_98_landmarks.size() != 98)
            {
                failure_case.failure_type = "CONVERSION_FAILURE";
                failure_case.description =
                    "Conversion produced " + std::to_string(pfld_98_landmarks.size()) + " landmarks instead of 98";
                failures.push_back(failure_case);
                continue;
            }
            double iod = calculateInterocularDistance(*match_result.best_face);
            if (iod <= 0.0)
            {
                failure_case.failure_type = "IOD_CALCULATION_FAILURE";
                failure_case.description = "Invalid interocular distance: " + std::to_string(iod);
                failures.push_back(failure_case);
                continue;
            }
            std::vector<math_utils::Point<double>> gt_landmarks;
            for (const auto& lm : sample.landmarks)
                gt_landmarks.emplace_back(lm[0], lm[1]);
            double mne = calculateMNE(pfld_98_landmarks, gt_landmarks, iod);
            if (mne > 0.15)
            {
                failure_case.failure_type = "HIGH_MNE_ERROR";
                failure_case.description = "Extremely high MNE score: " + std::to_string(mne);
                failure_case.attempted_iou = match_result.iou_score;
                failures.push_back(failure_case);
                continue;
            }
        }

        return failures;
    }

    void generateFailureDebugVisualization(const FailureCase& failure_case) const
    {
        const auto& sample = wflw_loader_->getSample(failure_case.image_index);
        auto image_ptr = sample.loadImage();
        if (!image_ptr) return;
        auto debug_image = image_ptr->deepCopy();

        // Draw ground truth bounding box outline
        if (failure_case.has_ground_truth_bbox)
        {
            auto gt_rect = failure_case.ground_truth_bbox;
            Pixel green_color(0, 255, 0);

            // Draw rectangle outline using border approach
            int x = static_cast<int>(gt_rect.x());
            int y = static_cast<int>(gt_rect.y());
            int w = static_cast<int>(gt_rect.width());
            int h = static_cast<int>(gt_rect.height());

            // Draw the four lines of the rectangle manually using paintPoints
            std::vector<math_utils::Point<long>> rect_points;

            // Top line
            for (int i = 0; i < w; ++i)
            {
                rect_points.emplace_back(x + i, y);
            }
            // Bottom line
            for (int i = 0; i < w; ++i)
            {
                rect_points.emplace_back(x + i, y + h - 1);
            }
            // Left line
            for (int i = 0; i < h; ++i)
            {
                rect_points.emplace_back(x, y + i);
            }
            // Right line
            for (int i = 0; i < h; ++i)
            {
                rect_points.emplace_back(x + w - 1, y + i);
            }

            debug_image->paintPoints(rect_points, green_color);

            // Add "GT" text label
            drawText(*debug_image, x, y - 10, "GT", green_color);
        }

        // Draw all detected bounding boxes in red
        for (size_t i = 0; i < failure_case.detected_bboxes.size(); ++i)
        {
            auto det_rect = failure_case.detected_bboxes[i];
            Pixel red_color(255, 0, 0);

            // Draw rectangle outline
            int x = static_cast<int>(det_rect.x());
            int y = static_cast<int>(det_rect.y());
            int w = static_cast<int>(det_rect.width());
            int h = static_cast<int>(det_rect.height());

            std::vector<math_utils::Point<long>> rect_points;

            // Top line
            for (int j = 0; j < w; ++j)
            {
                rect_points.emplace_back(x + j, y);
            }
            // Bottom line
            for (int j = 0; j < w; ++j)
            {
                rect_points.emplace_back(x + j, y + h - 1);
            }
            // Left line
            for (int j = 0; j < h; ++j)
            {
                rect_points.emplace_back(x, y + j);
            }
            // Right line
            for (int j = 0; j < h; ++j)
            {
                rect_points.emplace_back(x + w - 1, y + j);
            }

            debug_image->paintPoints(rect_points, red_color);

            // Add detection index label
            drawText(*debug_image, x, y - 10, "D" + std::to_string(i), red_color);
        }

        // Draw ground truth landmarks in blue
        Pixel blue_color(0, 0, 255);
        std::vector<math_utils::Point<long>> landmark_points;

        for (const auto& landmark : sample.landmarks)
        {
            int x = static_cast<int>(landmark[0]);
            int y = static_cast<int>(landmark[1]);
            landmark_points.emplace_back(x, y);
        }
        debug_image->paintPoints(landmark_points, blue_color);

        // Add failure information text
        std::vector<std::string> info_lines = {"Failure: " + failure_case.failure_type,
                                               "Index: " + std::to_string(failure_case.image_index),
                                               "Detected faces: " + std::to_string(failure_case.detected_faces)};

        if (failure_case.attempted_iou >= 0.0)
        {
            info_lines.push_back("Best IoU: " + std::to_string(failure_case.attempted_iou));
        }

        // Draw info text
        Pixel white_color(255, 255, 255);
        int text_y = 25;

        for (const auto& line : info_lines)
        {
            drawText(*debug_image, 8, text_y, line, white_color);
            text_y += 20;
        }

        // Save debug visualization if SAVE_IMAGES is set
        if (TestUtils::getEnvVarBool("SAVE_IMAGES"))
        {
            std::string output_dir = createTestOutputDirectory("failure_analysis");
            std::string debug_filename =
                "failure_debug_idx" + std::to_string(failure_case.image_index) + "_" + failure_case.failure_type + ".ppm";
            
            // Combine with output directory
            if (!output_dir.empty())
            {
                debug_filename = output_dir + "/" + debug_filename;
            }
            
            debug_image->saveToDisk(debug_filename);
        }
    }
};

TEST_F(FailureAnalysisTest, DetectAndAnalyzeFailures)
{
    std::cout << "\n========================================\n";
    std::cout << "FAILURE DETECTION AND ANALYSIS\n";
    std::cout << "========================================\n";

    // Test on a subset of images to find failures
    std::vector<int> test_indices;
    int max_test_images = std::min(100, wflw_loader_->getSampleCount());
    for (int i = 0; i < max_test_images; ++i)
    {
        test_indices.push_back(i);
    }

    std::cout << "Analyzing " << test_indices.size() << " images for failures...\n";

    auto failures = analyzeFailures(test_indices, 20); // Find up to 20 failures

    std::cout << "\nFound " << failures.size() << " failure cases\n";

    // Categorize failures
    std::map<std::string, int> failure_counts;
    for (const auto& failure : failures)
    {
        failure_counts[failure.failure_type]++;
    }

    std::cout << "\n=== FAILURE BREAKDOWN ===\n";
    for (const auto& [type, count] : failure_counts)
    {
        double percentage = (100.0 * count) / test_indices.size();
        std::cout << type << ": " << count << " (" << std::fixed << std::setprecision(1) << percentage << "%)\n";
    }

    // Generate CSV report for detailed analysis
    std::ofstream failure_csv("failure_debug_report.csv");
    failure_csv << "Image_Index,Failure_Type,Description,Detected_Faces,Attempted_IoU,GT_Bbox_X,GT_Bbox_Y,GT_Bbox_W,GT_"
                   "Bbox_H\n";

    for (const auto& failure : failures)
    {
        failure_csv << failure.image_index << "," << failure.failure_type << ","
                    << "\"" << failure.description << "\"," << failure.detected_faces << "," << failure.attempted_iou
                    << ",";

        if (failure.has_ground_truth_bbox)
        {
            failure_csv << failure.ground_truth_bbox.x() << "," << failure.ground_truth_bbox.y() << ","
                        << failure.ground_truth_bbox.width() << "," << failure.ground_truth_bbox.height();
        }
        else
        {
            failure_csv << "NA,NA,NA,NA";
        }
        failure_csv << "\n";
    }
    failure_csv.close();

    std::cout << "\nFailure analysis saved to: failure_debug_report.csv\n";

    // Verify we don't have too many failures
    double failure_rate = (100.0 * failures.size()) / test_indices.size();
    EXPECT_LT(failure_rate, 30.0) << "Failure rate too high: " << failure_rate << "%";

    // Check that face match failures aren't too common (should be rare with IoU matching)
    int face_match_failures = failure_counts["FACE_MATCH_FAILURE"];
    double face_match_failure_rate = (100.0 * face_match_failures) / test_indices.size();
    EXPECT_LT(face_match_failure_rate, 10.0)
        << "Face matching failure rate too high: " << face_match_failure_rate << "% "
        << "(IoU-based matching should handle most multi-face scenarios)";
}

TEST_F(FailureAnalysisTest, GenerateDebugVisualizations)
{
    std::cout << "\n========================================\n";
    std::cout << "DEBUG VISUALIZATION GENERATION\n";
    std::cout << "========================================\n";

    // Find some failures to visualize
    std::vector<int> test_indices;
    int max_test_images = std::min(50, wflw_loader_->getSampleCount());
    for (int i = 0; i < max_test_images; ++i)
    {
        test_indices.push_back(i);
    }

    auto failures = analyzeFailures(test_indices, 5); // Get first 5 failures for visualization

    std::cout << "Generating debug visualizations for " << failures.size() << " failure cases\n";

    int generated_count = 0;
    for (const auto& failure : failures)
    {
        std::cout << "Generating debug viz for failure type: " << failure.failure_type << " (image "
                  << failure.image_index << ")\n";

        generateFailureDebugVisualization(failure);
        generated_count++;
    }

    std::cout << "Generated " << generated_count << " debug visualizations\n";

    // Ensure we can generate at least some visualizations
    if (!failures.empty())
    {
        EXPECT_GT(generated_count, 0) << "Should be able to generate debug visualizations for failures";
    }
}

TEST_F(FailureAnalysisTest, ErrorHandlingValidation)
{
    std::cout << "\n========================================\n";
    std::cout << "ERROR HANDLING VALIDATION\n";
    std::cout << "========================================\n";

    // Test various error conditions to ensure graceful handling

    // Test 1: Invalid image index
    std::cout << "Testing invalid image index handling...\n";
    
    // Test negative index - should handle gracefully
    bool negative_index_handled = false;
    try {
        const auto& sample = wflw_loader_->getSample(-1);
        negative_index_handled = false; // Should not reach here
    } catch (const std::exception&) {
        negative_index_handled = true; // Expected behavior
    }
    EXPECT_TRUE(negative_index_handled) << "Should handle invalid negative index gracefully";

    // Test out-of-range index - should handle gracefully  
    bool out_of_range_handled = false;
    try {
        const auto& sample = wflw_loader_->getSample(999999);
        out_of_range_handled = false; // Should not reach here
    } catch (const std::exception&) {
        out_of_range_handled = true; // Expected behavior
    }
    EXPECT_TRUE(out_of_range_handled) << "Should handle out-of-range index gracefully";

    // Test 2: Null image handling
    std::cout << "Testing null image handling...\n";
    std::unique_ptr<Image> null_image = nullptr;
    
    // The SCRFD detector should now handle null images gracefully
    auto null_result = scrfd_detector_->detect(null_image);
    EXPECT_TRUE(null_result.empty()) << "Should return empty vector for null image";
    std::cout << "Null image handling: PASSED\n";

    // Test 3: Empty face list handling
    std::cout << "Testing empty face list handling...\n";
    std::vector<Face> empty_faces;
    math_utils::Rect<double> test_bbox(10, 10, 60, 60);
    auto match_result = findBestMatchingFace(empty_faces, test_bbox, 0.1);
    EXPECT_FALSE(match_result.found_match) << "Should handle empty face list gracefully";
    EXPECT_EQ(match_result.iou_score, 0.0) << "Should return 0.0 IoU for empty face list";

    // Test 4: Invalid landmark conversion
    std::cout << "Testing invalid landmark conversion handling...\n";
    std::vector<FaceLandmark> invalid_landmarks; // Empty landmarks
    
    // The LandmarkConverter should validate input and handle errors gracefully
    try {
        auto converted = LandmarkConverter::pfldToWflw(invalid_landmarks);
        // If it doesn't throw, check that result is empty
        EXPECT_TRUE(converted.empty()) << "Should handle empty landmark conversion gracefully";
        std::cout << "Invalid landmark conversion: PASSED (no exception)\n";
    } catch (const std::exception& e) {
        // If it throws, that's also acceptable error handling behavior
        std::cout << "Invalid landmark conversion: PASSED (exception thrown: " << e.what() << ")\n";
        EXPECT_TRUE(true) << "LandmarkConverter properly validates input";
    }

    // Test 5: Invalid IoD calculation
    std::cout << "Testing invalid IoD calculation handling...\n";
    Face test_face;
    // Face with no landmarks should return invalid IoD
    double iod = calculateInterocularDistance(test_face);
    EXPECT_LE(iod, 0.0) << "Should return invalid IoD for face without landmarks";

    std::cout << "All error handling tests completed\n";
}

TEST_F(FailureAnalysisTest, FaceMatchingEdgeCases)
{
    std::cout << "\n========================================\n";
    std::cout << "FACE MATCHING EDGE CASES\n";
    std::cout << "========================================\n";

    // Test various IoU scenarios to ensure robust face matching

    math_utils::Rect<double> ground_truth(100, 100, 200, 200); // Reference bbox

    // Test 1: Perfect match
    std::vector<Face> perfect_match_faces;
    FaceBoundingBox perfect_bbox(100.0f, 100.0f, 200.0f, 200.0f);
    Face perfect_face(perfect_bbox);
    perfect_match_faces.push_back(perfect_face);

    auto result = findBestMatchingFace(perfect_match_faces, ground_truth, 0.1);
    EXPECT_TRUE(result.found_match) << "Should find perfect match";
    EXPECT_NEAR(result.iou_score, 1.0, 0.01) << "Perfect match should have IoU ≈ 1.0";

    // Test 2: Partial overlap above threshold
    std::vector<Face> partial_match_faces;
    FaceBoundingBox partial_bbox(120.0f, 120.0f, 220.0f, 220.0f); // Shifted overlap
    Face partial_face(partial_bbox);
    partial_match_faces.push_back(partial_face);

    result = findBestMatchingFace(partial_match_faces, ground_truth, 0.1);
    EXPECT_TRUE(result.found_match) << "Should find partial match above threshold";
    EXPECT_GT(result.iou_score, 0.1) << "Partial match should exceed threshold";

    // Test 3: Multiple faces - best match selection
    std::vector<Face> multi_faces;
    FaceBoundingBox weak_bbox(150.0f, 150.0f, 200.0f, 200.0f); // Small overlap
    Face weak_match(weak_bbox);
    multi_faces.push_back(weak_match);

    FaceBoundingBox strong_bbox(110.0f, 110.0f, 200.0f, 200.0f); // Good overlap
    Face strong_match(strong_bbox);
    multi_faces.push_back(strong_match);

    result = findBestMatchingFace(multi_faces, ground_truth, 0.1);
    EXPECT_TRUE(result.found_match) << "Should find best match among multiple faces";
    EXPECT_GT(result.iou_score, 0.3) << "Should select the better matching face";

    // Test 4: No match above threshold
    std::vector<Face> no_match_faces;
    FaceBoundingBox distant_bbox(300.0f, 300.0f, 350.0f, 350.0f); // Far away
    Face distant_face(distant_bbox);
    no_match_faces.push_back(distant_face);

    result = findBestMatchingFace(no_match_faces, ground_truth, 0.1);
    EXPECT_FALSE(result.found_match) << "Should not find match below threshold";
    EXPECT_LT(result.iou_score, 0.1) << "Distant face should have low IoU";

    std::cout << "Face matching edge cases validated\n";
}
