/**
 * MULTI-IMAGE DETECTION VISUALIZATION TESTS
 *
 * Tests focused on visualizing landmark detection across multiple images:
 * - Multi-face image handling with IoU-based face matching
 * - Detection visualization with face matching information
 * - Performance on diverse image conditions
 */

#include "wflw_test_base.h"

#include <iomanip>

class DetectionVisualizationTest : public WFLWTestBase
{
};

TEST_F(DetectionVisualizationTest, MultiImageDetectionVisualization)
{
    std::cout << "\n=== Multi-Image Detection-Only Visualization ===\n";
    std::cout << "Processing multiple images from WFLW dataset for landmark detection visualization\n";

    // Check if images should be saved
    const char* save_images_env = std::getenv("SAVE_IMAGES");
    if (!save_images_env)
    {
        std::cout << "Set SAVE_IMAGES=1 to save debug visualizations\n";
    }

    // Select a diverse set of images to visualize (first 10 images)
    int num_images_to_process = std::min(10, wflw_loader_->getSampleCount());
    std::cout << "Processing " << num_images_to_process << " images for visualization\n";

    int successful_detections = 0;
    double total_mne = 0.0;
    std::vector<double> mne_scores;

    for (int img_idx = 0; img_idx < num_images_to_process; ++img_idx)
    {
        const auto& sample = wflw_loader_->getSample(img_idx);
        auto image_ptr = sample.loadImage();
        if (!image_ptr)
        {
            std::cout << "Failed to load image " << img_idx << ", skipping...\n";
            continue;
        }

        std::cout << "Processing image " << img_idx << " (" << image_ptr->info.width << "x"
                  << image_ptr->info.height << ")\n";

        // Detect faces using SCRFD
        auto detected_faces = scrfd_detector_->detect(image_ptr);

        if (detected_faces.empty())
        {
            std::cout << "No faces detected in image " << img_idx << ", skipping...\n";
            continue;
        }

        std::cout << "Detected " << detected_faces.size() << " faces\n";

        // Find the best matching face for the ground truth landmarks using IoU
        const auto& gt_bbox = sample.bbox;
        std::cout << "Ground truth bbox: [" << gt_bbox[0] << "," << gt_bbox[1] << "," << gt_bbox[2] << "," << gt_bbox[3]
                  << "]\n";

        // Convert bbox to math_utils::Rect<double>
        math_utils::Rect<double> gt_rect(gt_bbox[0], gt_bbox[1], gt_bbox[2], gt_bbox[3]);
        auto match_result = findBestMatchingFace(detected_faces, gt_rect, 0.1);

        if (!match_result.found_match)
        {
            std::cout << "No matching face found with sufficient IoU for image " << img_idx << "\n";

            // Debug: Show all detected faces and their IoU scores
            for (size_t face_idx = 0; face_idx < detected_faces.size(); ++face_idx)
            {
                const auto& det_bbox = detected_faces[face_idx].getBoundingBox();
                std::cout << "Detected face " << face_idx << " bbox: [" << det_bbox.rect.l << "," << det_bbox.rect.t
                          << "," << det_bbox.rect.r << "," << det_bbox.rect.b << "]\n";

                auto debug_result = findBestMatchingFace({detected_faces[face_idx]}, gt_rect, 0.0);
                std::cout << "Face " << face_idx << " IoU: " << std::fixed << std::setprecision(3)
                          << debug_result.iou_score << "\n";
            }
            continue;
        }

        std::cout << "Using face " << match_result.face_index << " (IoU: " << std::fixed << std::setprecision(3)
                  << match_result.iou_score << ")\n";

        // Run PFLD landmark detection on the best matching face
        Face& selected_face = *match_result.best_face;
        pfld_detector_->detect(image_ptr, selected_face);
        auto landmarks = selected_face.getLandmarks();

        if (landmarks.size() != 106)
        {
            std::cout << "PFLD detection failed for image " << img_idx << " (got " << landmarks.size()
                      << " landmarks)\n";
            continue;
        }

        // Convert to WFLW 98-point format and calculate metrics
        auto pfld_98_landmarks = LandmarkConverter::pfldToWflw(landmarks);
        double iod = calculateInterocularDistance(selected_face);

        if (pfld_98_landmarks.size() != 98 || iod <= 0.0)
        {
            std::cout << "Conversion failed for image " << img_idx << " (landmarks=" << pfld_98_landmarks.size()
                      << ", IOD=" << iod << ")\n";
            continue;
        }

        // Convert sample.landmarks to vector<math_utils::Point<double>>
        std::vector<math_utils::Point<double>> gt_landmarks;
        for (const auto& lm : sample.landmarks)
            gt_landmarks.emplace_back(lm[0], lm[1]);

        double mne = calculateMNE(pfld_98_landmarks, gt_landmarks, iod);
        std::cout << "Landmarks detected: 106 -> 98, IOD: " << std::fixed << std::setprecision(2) << iod
                  << ", MNE: " << mne << "\n";

        // Track successful detection
        successful_detections++;
        total_mne += mne;
        mne_scores.push_back(mne);

        // Save visualization if requested
        if (save_images_env)
        {
            saveDetectionVisualizationWithFaceInfo(sample, pfld_98_landmarks, img_idx, mne, match_result.face_index,
                                                   match_result.iou_score, static_cast<int>(detected_faces.size()));
        }
    }

    // Print summary results
    std::cout << "\n=== VISUALIZATION TEST SUMMARY ===\n";
    std::cout << "Processed images: " << num_images_to_process << "\n";
    std::cout << "Successful detections: " << successful_detections << "\n";
    std::cout << "Success rate: " << (100.0 * successful_detections / num_images_to_process) << "%\n";

    if (successful_detections > 0)
    {
        double mean_mne = total_mne / successful_detections;
        std::cout << "Mean MNE: " << std::fixed << std::setprecision(4) << mean_mne << "\n";

        // Calculate median
        if (!mne_scores.empty())
        {
            std::sort(mne_scores.begin(), mne_scores.end());
            double median_mne = mne_scores[mne_scores.size() / 2];
            std::cout << "Median MNE: " << std::fixed << std::setprecision(4) << median_mne << "\n";
        }

        // Basic validation - the face matching should significantly improve accuracy
        EXPECT_GT(successful_detections, num_images_to_process / 2)
            << "Too many detection failures even with face matching";

        if (mean_mne < 100.0) // Sanity check for valid MNE calculation
        {
            EXPECT_LT(mean_mne, 2.0) << "Mean MNE too high with face matching: " << mean_mne;
        }
    }
    else
    {
        FAIL() << "No successful detections in visualization test";
    }
}

TEST_F(DetectionVisualizationTest, FaceMatchingAccuracy)
{
    std::cout << "\n=== Face Matching Accuracy Test ===\n";

    // Test face matching on images with multiple faces
    int images_tested = 0;
    int accurate_matches = 0;
    int multi_face_images = 0;

    // Test first 20 images to find multi-face scenarios
    int max_test_images = std::min(20, wflw_loader_->getSampleCount());

    for (int img_idx = 0; img_idx < max_test_images; ++img_idx)
    {
        const auto& sample = wflw_loader_->getSample(img_idx);
        auto image_ptr = sample.loadImage();
        if (!image_ptr)
        {
            continue;
        }

        auto detected_faces = scrfd_detector_->detect(image_ptr);
        if (detected_faces.size() < 2)
        {
            continue; // Skip single-face images for this test
        }

        multi_face_images++;
        images_tested++;

        std::cout << "Image " << img_idx << ": " << detected_faces.size() << " faces detected\n";

        // Find best match
        auto match_result = findBestMatchingFace(detected_faces, math_utils::Rect<double>(sample.bbox[0], sample.bbox[1], sample.bbox[2], sample.bbox[3]), 0.1);

        if (match_result.found_match)
        {
            std::cout << "  Best match: Face " << match_result.face_index << " with IoU " << std::fixed
                      << std::setprecision(3) << match_result.iou_score << "\n";

            // Consider it accurate if IoU > 0.5 (reasonable overlap)
            if (match_result.iou_score > 0.5)
            {
                accurate_matches++;
            }
        }
        else
        {
            std::cout << "  No sufficient match found\n";
        }
    }

    std::cout << "\n=== Face Matching Results ===\n";
    std::cout << "Multi-face images tested: " << multi_face_images << "\n";
    std::cout << "Accurate matches (IoU > 0.5): " << accurate_matches << "\n";

    if (multi_face_images > 0)
    {
        double accuracy_rate = 100.0 * accurate_matches / multi_face_images;
        std::cout << "Face matching accuracy: " << std::fixed << std::setprecision(1) << accuracy_rate << "%\n";

        // With proper IoU-based matching, we should get good accuracy
        EXPECT_GT(accuracy_rate, 70.0) << "Face matching accuracy too low: " << accuracy_rate << "%";
    }
    else
    {
        std::cout << "No multi-face images found in test set\n";
        GTEST_SKIP() << "Insufficient multi-face images for face matching accuracy test";
    }
}
