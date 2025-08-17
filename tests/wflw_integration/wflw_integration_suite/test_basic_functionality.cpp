/**
 * BASIC FUNCTIONALITY TESTS
 *
 * Tests core functionality including:
 * - Model initialization
 * - Configuration validation
 * - Single image pipeline
 * - CUDA availability detection
 */

#include "wflw_test_base.h"

class BasicFunctionalityTest : public WFLWTestBase
{
};

TEST_F(BasicFunctionalityTest, DetectorInitialization)
{
    EXPECT_TRUE(scrfd_detector_->isReady());
    EXPECT_TRUE(pfld_detector_->isReady());
    EXPECT_GT(wflw_loader_->get_num_examples(), 0);
}

TEST_F(BasicFunctionalityTest, ConfigurationValidation)
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

TEST_F(BasicFunctionalityTest, SingleImagePipeline)
{
    WFLWExample example;
    ASSERT_TRUE(wflw_loader_->load_example(0, example));
    ASSERT_TRUE(example.image != nullptr);

    // Face detection
    auto detected_faces = scrfd_detector_->detect(example.image);
    EXPECT_GT(detected_faces.size(), 0) << "No faces detected in test image";

    if (!detected_faces.empty())
    {
        // Use face matching to find the best corresponding face
        auto match_result = findBestMatchingFace(detected_faces, example.bounding_box);

        if (match_result.found_match)
        {
            Face& face = *match_result.best_face;

            std::cout << "\n=== Single Image Pipeline Test ===\n";
            std::cout << "Image size: " << example.image->info.width << "x" << example.image->info.height << "\n";
            std::cout << "WFLW ground truth landmarks: " << example.landmarks.size() << "\n";
            std::cout << "Selected face " << match_result.face_index << " with IoU: " << match_result.iou_score << "\n";

            // Run PFLD landmark detection
            pfld_detector_->detect(example.image, face);
            auto landmarks = face.getLandmarks();

            std::vector<FaceLandmark> pfld_98_landmarks;
            double mne = 0.0;
            double iod = calculateInterocularDistance(face);

            if (landmarks.size() == 106 && iod > 0.0)
            {
                // Convert PFLD 106 landmarks to WFLW 98 format
                pfld_98_landmarks = LandmarkConverter::pfldToWflw(landmarks);

                if (pfld_98_landmarks.size() == example.landmarks.size())
                {
                    mne = calculateMNE(pfld_98_landmarks, example.landmarks, iod);
                    std::cout << "Landmarks detected: 106 -> 98, IOD: " << std::fixed << std::setprecision(2) << iod
                              << ", MNE: " << mne << "\n";

                    EXPECT_LT(mne, 0.10) << "MNE too high for basic pipeline test: " << mne;
                }
                else
                {
                    FAIL() << "Landmark conversion failed: expected " << example.landmarks.size() << " landmarks, got "
                           << pfld_98_landmarks.size();
                }
            }
            else
            {
                FAIL() << "PFLD detection failed: landmarks=" << landmarks.size() << ", IOD=" << iod;
            }

            // Save visualization if requested
            const char* save_images_env = std::getenv("SAVE_IMAGES");
            if (save_images_env && !pfld_98_landmarks.empty())
            {
                saveDetectionVisualizationWithFaceInfo(example, pfld_98_landmarks, 0, mne, match_result.face_index,
                                                       match_result.iou_score, static_cast<int>(detected_faces.size()));
            }
        }
        else
        {
            FAIL() << "No matching face found with sufficient IoU overlap";
        }
    }
}
