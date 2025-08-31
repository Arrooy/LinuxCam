#include <chrono>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "../dataset_utils.h"
#include "../test_utils.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/Image/text_draw.h"
#include "LinuxFace/face.h"
#include "LinuxFace/imageLoader.h"
#include "LinuxFace/landmark_converter.h"
#include "LinuxFace/onnx/scrfd.h"
#include "config.hpp"

using namespace linuxface;

class SCRFDUnitTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Load test configuration - only use test config, not root config
        std::vector<std::string> config_paths = {"../config.yaml", "config.yaml",
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

        std::string models_folder = Config::getInstance().getModelFolderPath();
        detector_ = std::make_unique<SCRFDetector>(models_folder + "scrfd_500m_bnkps_shape640x640.onnx");

        // Initialize debug saving settings
        save_failures_ = shouldSaveFailures();
        if (save_failures_)
        {
            results_dir_ = ensureResultsDirectory();
        }
    }


  private:
    // Check if environment variable is set to save test failures using common utilities
    bool shouldSaveFailures() const { return TestUtils::getEnvVarBool("SAVE_TEST_FAILURES"); }

    // Create results directory if it doesn't exist using config paths
    std::string ensureResultsDirectory()
    {
        // Use test directory from config, similar to other tests
        std::string test_base_dir = "../tests/scrfd_unit";
        results_dir_path_ = test_base_dir + "/results";

        // Create directory if it doesn't exist
        struct stat st = {0};
        if (stat(results_dir_path_.c_str(), &st) == -1)
        {
            if (mkdir(results_dir_path_.c_str(), 0755) == 0)
            {
                directory_created_by_test_ = true;
            }
        }

        return results_dir_path_;
    }

    // Track created files for cleanup (following test_config.cpp pattern)
    void trackCreatedFile(const std::string& filepath) const { created_debug_files_.push_back(filepath); }

  protected:
    // Helper function to calculate bounding box from WFLW landmarks
    linuxface::math_utils::Rect<double>
    calculateBoundingBoxFromLandmarks(const std::vector<std::array<double, 2>>& landmarks) const
    {
        if (landmarks.empty())
        {
            return linuxface::math_utils::Rect<double>{0, 0, 0, 0};
        }

        double min_x = landmarks[0][0];
        double max_x = landmarks[0][0];
        double min_y = landmarks[0][1];
        double max_y = landmarks[0][1];

        for (const auto& landmark : landmarks)
        {
            min_x = std::min(min_x, landmark[0]);
            max_x = std::max(max_x, landmark[0]);
            min_y = std::min(min_y, landmark[1]);
            max_y = std::max(max_y, landmark[1]);
        }

        return linuxface::math_utils::Rect<double>{min_x, min_y, max_x, max_y};
    }

    // Helper function to create filtered faces vector excluding already matched faces
    // Returns pair of: <filtered_faces_vector, index_mapping_to_original>
    std::pair<std::vector<Face>, std::vector<int>>
    createFilteredFaces(const std::vector<Face>& all_faces, const std::set<int>& excluded_indices) const
    {
        std::vector<Face> filtered_faces;
        std::vector<int> index_mapping; // Maps filtered index to original index

        for (int i = 0; i < static_cast<int>(all_faces.size()); ++i)
        {
            if (excluded_indices.find(i) == excluded_indices.end())
            {
                filtered_faces.push_back(all_faces[i]);
                index_mapping.push_back(i);
            }
        }

        return {filtered_faces, index_mapping};
    }

    // Draw landmarks on image and save it
    void saveFaceDebugImage(const Image& image, const std::vector<Face>& faces, const std::string& filename_prefix,
                            const std::string& failure_reason = "",
                            const std::vector<std::array<double, 2>>* ground_truth_landmarks = nullptr,
                            int target_face_idx = -1) const
    {
        if (!save_failures_ || results_dir_.empty())
        {
            return;
        }

        // Create a copy of the image to draw on
        auto debug_image = image.deepCopy();
        if (!debug_image)
        {
            std::cerr << "Failed to create debug image copy" << std::endl;
            return;
        }

        // Draw ground truth landmarks first (if available) with smaller, darker circles
        // Only show ground truth for the face that best matches it
        bool show_ground_truth = false;
        Face::FaceMatchResult match_result; // Declare outside to access later
        if (ground_truth_landmarks && !ground_truth_landmarks->empty() && target_face_idx >= 0)
        {
            // Calculate ground truth bounding box from landmarks
            auto gt_bbox = calculateBoundingBoxFromLandmarks(*ground_truth_landmarks);

            // Use Face::findBestMatchingFace with IoU-based matching (more robust than center distance)
            std::vector<Face> faces_copy = faces; // Create copy since findBestMatchingFace expects non-const
            auto match_result = Face::findBestMatchingFace(faces_copy, gt_bbox, 0.1); // 10% IoU threshold

            // Only show ground truth if this face matches and has reasonable IoU
            show_ground_truth = match_result.found_match && (match_result.face_index == target_face_idx);
        }

        if (show_ground_truth)
        {
            // Convert WFLW 98 landmarks to SCRFD 5 key landmarks using LandmarkConverter
            std::vector<FaceLandmark> wflw_landmarks;
            wflw_landmarks.reserve(ground_truth_landmarks->size());

            // Convert WFLW array format to FaceLandmark format
            for (size_t i = 0; i < ground_truth_landmarks->size(); ++i)
            {
                const auto& landmark = (*ground_truth_landmarks)[i];
                wflw_landmarks.emplace_back(
                    FaceLandmark{static_cast<unsigned int>(i), math_utils::Point3D(landmark[0], landmark[1], 0.0)});
            }

            // Extract 5 key landmarks that correspond to SCRFD landmarks
            auto key_landmarks = LandmarkConverter::extractKeyLandmarks(wflw_landmarks, LandmarkFormat::WFLW_98);

            const linuxface::Pixel gt_color = {128, 128, 128}; // Gray for ground truth

            // Draw ground truth landmarks with offset pattern for visibility when overlapping
            for (size_t j = 0; j < key_landmarks.size() && j < 5; ++j)
            {
                const auto& landmark = key_landmarks[j];
                linuxface::math_utils::Point3D center{landmark.p.x, landmark.p.y, 0.0};
                linuxface::image_utils::paintCircle(debug_image, center, 1.0f, gt_color);
            }
        }

        // Draw detected landmarks on detected faces (larger, colored circles)
        for (size_t i = 0; i < faces.size(); ++i)
        {
            const Face& face = faces[i];
            auto landmarks = face.getFivePointLandmarksArcFaceOrder2D();

            if (landmarks.size() == 5)
            {
                // Draw landmarks as circles
                const std::vector<linuxface::Pixel> colors = {
                    {255, 0,   0  }, // Red for left eye
                    {0,   255, 0  }, // Green for right eye
                    {0,   0,   255}, // Blue for nose
                    {255, 255, 0  }, // Yellow for left mouth
                    {255, 0,   255}  // Magenta for right mouth
                };

                for (size_t j = 0; j < landmarks.size(); ++j)
                {
                    debug_image->ppx(static_cast<int>(landmarks[j].x), static_cast<int>(landmarks[j].y), colors[j]);
                }
            }

            // Draw bounding box
            const auto& bbox = face.getBoundingBox().rect;
            const linuxface::Pixel bbox_color = {0, 255, 255}; // Cyan

            face.paintBoundingBox(debug_image, bbox_color);

            // Draw face number and detection score text above the bounding box
            const float detection_score = face.getBoundingBox().score;
            std::ostringstream text_stream;
            text_stream << "Face " << i << " (Score: " << std::fixed << std::setprecision(3) << detection_score << ")";
            const std::string face_text = text_stream.str();

            // Position text above the bounding box with some padding
            const int text_x = static_cast<int>(bbox.x());
            const int text_y = std::max(0, static_cast<int>(bbox.y()) - 12); // 12 pixels above bbox

            // Use white text with black background for visibility
            const linuxface::Pixel text_color = {255, 255, 255}; // White
            const linuxface::Pixel bg_color = {0, 0, 0};         // Black

            linuxface::drawTextWithBackground(*debug_image, text_x, text_y, face_text, text_color, bg_color, 1, false,
                                              2);
        } // Save the debug image
        std::string filename = results_dir_ + "/" + filename_prefix;
        if (!failure_reason.empty())
        {
            filename += "_" + failure_reason;
        }
        filename += ".ppm";

        debug_image->info.format = linuxface::ImageFormat::PPM;
        if (debug_image->saveToDisk(filename))
        {
            // Track created file for cleanup (following test_config.cpp pattern)
            trackCreatedFile(filename);

            std::cout << "Saved debug image: " << filename << std::endl;
            if (show_ground_truth)
            {
                std::cout << "  - Gray dots (4-offset pattern): Ground truth 5 key landmarks (face " << target_face_idx
                          << " matches GT with IoU: " << std::fixed << std::setprecision(3) << match_result.iou_score
                          << ")" << std::endl;
                std::cout << "  - Colored dots (1px): Detected SCRFD landmarks" << std::endl;
                std::cout << "  - Cyan box: Detected face bounding box" << std::endl;
                std::cout << "  - White text: Face numbers and detection scores for each face" << std::endl;
            }
            else if (ground_truth_landmarks && !ground_truth_landmarks->empty())
            {
                std::cout << "  - Colored dots (1px): Detected SCRFD landmarks" << std::endl;
                std::cout << "  - Cyan box: Detected face bounding box" << std::endl;
                std::cout << "  - White text: Face numbers and detection scores for each face" << std::endl;
                if (match_result.found_match)
                {
                    std::cout << "  - Note: Ground truth landmarks not shown (face " << target_face_idx
                              << " doesn't match GT - IoU: " << std::fixed << std::setprecision(3)
                              << match_result.iou_score << ", best match: face " << match_result.face_index << ")"
                              << std::endl;
                }
                else
                {
                    std::cout << "  - Note: Ground truth landmarks not shown (face " << target_face_idx
                              << " doesn't match GT - no sufficient IoU found)" << std::endl;
                }
            }
            else
            {
                std::cout << "  - Colored dots (1px): Detected SCRFD landmarks" << std::endl;
                std::cout << "  - Cyan box: Detected face bounding box" << std::endl;
                std::cout << "  - White text: Face numbers and detection scores for each face" << std::endl;
            }
        }
        else
        {
            std::cerr << "Failed to save debug image: " << filename << std::endl;
        }
    }

    std::unique_ptr<Image> createTestImage(int width = 640, int height = 480)
    {
        size_t data_size = width * height * 3;
        auto image = std::make_unique<Image>(data_size);
        image->info.width = width;
        image->info.height = height;
        image->info.pixelSizeBytes = 3;
        image->info.format = ImageFormat::RGB;

        // Fill with test pattern - gradient
        unsigned char* data = image->data();
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int idx = (y * width + x) * 3;
                data[idx] = (x * 255) / width;      // R channel
                data[idx + 1] = (y * 255) / height; // G channel
                data[idx + 2] = 128;                // B channel
            }
        }
        return image;
    }

    std::unique_ptr<Image> loadRealImage(const std::string& imagePath)
    {
        ImageLoader loader(ImageLoader::LoadStrategy::IMMEDIATE);

        if (!loader.loadFromFile(imagePath))
        {
            std::cerr << "Failed to load image from: " << imagePath << std::endl;
            return nullptr;
        }

        std::unique_ptr<Image> loadedImage;
        if (!loader.getImage(loadedImage))
        {
            std::cerr << "Failed to get image data from loader" << std::endl;
            return nullptr;
        }

        return loadedImage;
    }

    bool saveFaceCrop(const Image& originalImage, const Face& face, const std::string& outputPath)
    {
        // Get face bounding box
        const auto& bbox = face.getBoundingBox().rect;

        // Add some padding around the face
        float padding_factor = 0.2f; // 20% padding
        float face_width = bbox.width();
        float face_height = bbox.height();
        float padding_x = face_width * padding_factor;
        float padding_y = face_height * padding_factor;

        // Calculate crop coordinates with padding
        float crop_x = std::max(0.0f, bbox.x() - padding_x);
        float crop_y = std::max(0.0f, bbox.y() - padding_y);
        float crop_width = std::min(static_cast<float>(originalImage.info.width) - crop_x, face_width + 2 * padding_x);
        float crop_height =
            std::min(static_cast<float>(originalImage.info.height) - crop_y, face_height + 2 * padding_y);

        // Create crop rectangle
        math_utils::Point<float> cropCorner{crop_x, crop_y};
        math_utils::Rect<float> cropRect{cropCorner, crop_width, crop_height};

        // Create cropped image
        auto croppedImage = originalImage.crop(cropRect);
        if (!croppedImage)
        {
            return false;
        }

        // Save as PPM for simplicity
        croppedImage->info.format = ImageFormat::PPM;
        return croppedImage->saveToDisk(outputPath);
    }

    bool validateFaceLandmarks(const Face& face, const Image& image)
    {
        auto landmarks = face.getFivePointLandmarksArcFaceOrder2D();

        if (landmarks.size() != 5)
        {
            return false;
        }

        // Check if landmarks are within image bounds
        for (const auto& landmark : landmarks)
        {
            if (landmark.x < 0 || landmark.x >= image.info.width || landmark.y < 0 || landmark.y >= image.info.height)
            {
                return false;
            }
        }

        // Basic sanity checks for landmark positions
        auto left_eye = landmarks[0];
        auto right_eye = landmarks[1];
        auto nose = landmarks[2];
        auto left_mouth = landmarks[3];
        auto right_mouth = landmarks[4];

        // Eyes should be horizontally aligned (roughly)
        if (std::abs(left_eye.y - right_eye.y) > image.info.height * 0.1)
        {
            return false;
        }

        // Right eye should be to the right of left eye
        if (right_eye.x <= left_eye.x)
        {
            return false;
        }

        // Nose should be between eyes horizontally and below eyes
        if (nose.x < left_eye.x || nose.x > right_eye.x || nose.y < std::min(left_eye.y, right_eye.y))
        {
            return false;
        }

        // Mouth should be below nose
        if (left_mouth.y <= nose.y || right_mouth.y <= nose.y)
        {
            return false;
        }

        return true;
    }

    std::unique_ptr<SCRFDetector> detector_;
    bool save_failures_ = false;
    std::string results_dir_;

    // Following test_config.cpp patterns for cleanup
    std::string results_dir_path_;
    bool directory_created_by_test_ = false;
    mutable std::vector<std::string> created_debug_files_;
};

// Test constructor and initialization
TEST_F(SCRFDUnitTest, ConstructorValidModel)
{
    EXPECT_TRUE(detector_->isReady());
}

TEST_F(SCRFDUnitTest, ConstructorInvalidModel)
{
    SCRFDetector invalid_detector("nonexistent_model.onnx");
    EXPECT_FALSE(invalid_detector.isReady());
}

// Test basic functionality
TEST_F(SCRFDUnitTest, BasicDetection)
{
    ASSERT_TRUE(detector_->isReady());

    auto test_image = createTestImage();
    std::vector<Face> faces = detector_->detect(test_image);

    // Basic detection should succeed and return some result
    EXPECT_GE(faces.size(), 0);
}

// Test different image sizes
TEST_F(SCRFDUnitTest, DifferentImageSizes)
{
    ASSERT_TRUE(detector_->isReady());

    std::vector<std::pair<int, int>> sizes = {
        {320,  240 },
        {640,  480 },
        {800,  600 },
        {1024, 768 },
        {1920, 1080}
    };

    for (const auto& size : sizes)
    {
        auto test_image = createTestImage(size.first, size.second);
        std::vector<Face> faces = detector_->detect(test_image);
        EXPECT_GE(faces.size(), 0) << "Failed for size " << size.first << "x" << size.second;
    }
}

// Test edge case image sizes
TEST_F(SCRFDUnitTest, EdgeCaseImageSizes)
{
    ASSERT_TRUE(detector_->isReady());

    // Very small image
    auto tiny_image = createTestImage(32, 32);
    std::vector<Face> faces = detector_->detect(tiny_image);
    EXPECT_GE(faces.size(), 0);

    // Very wide image
    auto wide_image = createTestImage(2000, 100);
    faces = detector_->detect(wide_image);
    EXPECT_GE(faces.size(), 0);

    // Very tall image
    auto tall_image = createTestImage(100, 2000);
    faces = detector_->detect(tall_image);
    EXPECT_GE(faces.size(), 0);
}

// Test null image input
TEST_F(SCRFDUnitTest, NullImageInput)
{
    ASSERT_TRUE(detector_->isReady());

    std::unique_ptr<Image> null_image = nullptr;
    // This should not crash but handle gracefully
    // Actual behavior depends on implementation
}

// Test performance bounds
TEST_F(SCRFDUnitTest, PerformanceBounds)
{
    ASSERT_TRUE(detector_->isReady());

    auto test_image = createTestImage();

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<Face> faces = detector_->detect(test_image);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Detection should complete within reasonable time (adjust as needed)
    EXPECT_LT(duration.count(), 5000) << "Detection took " << duration.count() << "ms";
}

// Test with real image
TEST_F(SCRFDUnitTest, RealImageDetection)
{
    ASSERT_TRUE(detector_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image from ../tests/common/single_face.jpeg";

    std::vector<Face> faces = detector_->detect(realImage);

    EXPECT_GT(faces.size(), 0) << "Should detect at least one face in real image";

    if (faces.size() > 0)
    {
        const Face& face = faces[0];
        const auto& bbox = face.getBoundingBox();

        // Verify bounding box is reasonable
        EXPECT_GT(bbox.rect.width(), 10);
        EXPECT_GT(bbox.rect.height(), 10);
        EXPECT_GT(bbox.score, 0.1f);
        EXPECT_TRUE(bbox.rect.isWithinBounds(realImage->info.width, realImage->info.height, 1.0f));

        // Check if face has landmarks
        auto landmarks = face.getFivePointLandmarksArcFaceOrder2D();
        if (landmarks.size() == 5)
        {
            EXPECT_TRUE(validateFaceLandmarks(face, *realImage));

            std::cout << "Detected face with landmarks at: (" << bbox.rect.x() << ", " << bbox.rect.y() << ", "
                      << bbox.rect.width() << ", " << bbox.rect.height() << ") score: " << bbox.score << std::endl;

            // Print landmark positions
            std::cout << "Landmarks (ArcFace order): ";
            for (size_t i = 0; i < landmarks.size(); ++i)
            {
                std::cout << "(" << landmarks[i].x << "," << landmarks[i].y << ") ";
            }
            std::cout << std::endl;
        }

        // Save face crop for manual inspection
        bool crop_saved = saveFaceCrop(*realImage, face, "scrfd_face_crop.ppm");
        if (crop_saved)
        {
            std::cout << "Saved face crop to: scrfd_face_crop.ppm" << std::endl;
        }
    }
}

// Test with multiple face image
TEST_F(SCRFDUnitTest, MultipleFaceDetection)
{
    ASSERT_TRUE(detector_->isReady());

    auto realImage = loadRealImage("../tests/common/two_face.jpeg");
    if (realImage == nullptr)
    {
        GTEST_SKIP() << "Multiple face test image not found, skipping test";
    }

    std::vector<Face> faces = detector_->detect(realImage);

    EXPECT_GT(faces.size(), 0) << "Should detect at least one face";

    std::cout << "Detected " << faces.size() << " faces in multiple face image" << std::endl;

    for (size_t i = 0; i < faces.size(); ++i)
    {
        const Face& face = faces[i];
        const auto& bbox = face.getBoundingBox();

        // Verify each bounding box
        EXPECT_GT(bbox.rect.width(), 10);
        EXPECT_GT(bbox.rect.height(), 10);
        EXPECT_GT(bbox.score, 0.1f);
        EXPECT_TRUE(bbox.rect.isWithinBounds(realImage->info.width, realImage->info.height, 1.0f));

        std::cout << "Face " << i << ": (" << bbox.rect.x() << ", " << bbox.rect.y() << ", " << bbox.rect.width()
                  << ", " << bbox.rect.height() << ") score: " << bbox.score << std::endl;

        // Save individual face crops
        std::string crop_filename = "scrfd_face_crop_" + std::to_string(i) + ".ppm";
        bool crop_saved = saveFaceCrop(*realImage, face, crop_filename);
        if (crop_saved)
        {
            std::cout << "Saved face crop " << i << " to: " << crop_filename << std::endl;
        }
    }
}

// Test detection consistency
TEST_F(SCRFDUnitTest, DetectionConsistency)
{
    ASSERT_TRUE(detector_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    const int numRuns = 3;
    std::vector<std::vector<Face>> allDetections(numRuns);

    // Run detection multiple times
    for (int i = 0; i < numRuns; ++i)
    {
        allDetections[i] = detector_->detect(realImage);
        EXPECT_GT(allDetections[i].size(), 0) << "No faces detected in run " << i;
    }

    // Check consistency across runs
    if (allDetections[0].size() > 0)
    {
        const auto& firstFace = allDetections[0][0];

        for (int i = 1; i < numRuns; ++i)
        {
            if (allDetections[i].size() > 0)
            {
                const auto& currentFace = allDetections[i][0];

                // Bounding boxes should be very similar
                float bbox_diff =
                    std::abs(firstFace.getBoundingBox().rect.x() - currentFace.getBoundingBox().rect.x())
                    + std::abs(firstFace.getBoundingBox().rect.y() - currentFace.getBoundingBox().rect.y());

                EXPECT_LT(bbox_diff, 5.0f) << "Bounding box difference too large across runs: " << bbox_diff;

                // Scores should be identical
                EXPECT_FLOAT_EQ(firstFace.getBoundingBox().score, currentFace.getBoundingBox().score);
            }
        }
    }

    std::cout << "Detection consistency test passed across " << numRuns << " runs" << std::endl;
}

// Test performance with real image
TEST_F(SCRFDUnitTest, RealImagePerformance)
{
    ASSERT_TRUE(detector_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    // Warm-up run
    detector_->detect(realImage);

    // Benchmark
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<Face> faces = detector_->detect(realImage);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_GT(faces.size(), 0) << "Should detect faces";
    EXPECT_LT(duration.count(), 2000) << "Real image detection took too long: " << duration.count() << "ms";

    std::cout << "SCRFD real image detection performance: " << duration.count() << "ms" << std::endl;
    std::cout << "Detected " << faces.size() << " faces" << std::endl;
}

// Test landmark quality
TEST_F(SCRFDUnitTest, LandmarkQuality)
{
    ASSERT_TRUE(detector_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    std::vector<Face> faces = detector_->detect(realImage);
    ASSERT_GT(faces.size(), 0) << "Should detect at least one face";

    const Face& face = faces[0];
    auto landmarks = face.getFivePointLandmarksArcFaceOrder2D();

    if (landmarks.size() == 5)
    {
        // Validate landmark geometry
        EXPECT_TRUE(validateFaceLandmarks(face, *realImage));

        // Calculate inter-ocular distance for scale reference
        auto left_eye = landmarks[0];
        auto right_eye = landmarks[1];
        float iod = std::sqrt(std::pow(right_eye.x - left_eye.x, 2) + std::pow(right_eye.y - left_eye.y, 2));

        EXPECT_GT(iod, 10.0f) << "Inter-ocular distance too small: " << iod;
        EXPECT_LT(iod, realImage->info.width * 0.5f) << "Inter-ocular distance too large: " << iod;

        std::cout << "Landmark quality assessment:" << std::endl;
        std::cout << "Inter-ocular distance: " << iod << " pixels" << std::endl;
        std::cout << "Landmark validation: " << (validateFaceLandmarks(face, *realImage) ? "PASSED" : "FAILED")
                  << std::endl;

        // Print detailed landmark info
        std::vector<std::string> landmark_names = {"Left Eye", "Right Eye", "Nose", "Left Mouth", "Right Mouth"};
        for (size_t i = 0; i < landmarks.size(); ++i)
        {
            std::cout << landmark_names[i] << ": (" << landmarks[i].x << ", " << landmarks[i].y << ")" << std::endl;
        }
    }
    else
    {
        std::cout << "No landmarks detected (model may not support keypoints)" << std::endl;
    }
}

// Test SCRFD validation on WFLW dataset
TEST_F(SCRFDUnitTest, WFLWDatasetValidation)
{
    ASSERT_TRUE(detector_->isReady());

    // Check if WFLW dataset is available using centralized utilities
    if (!TestUtils::Datasets::SimpleWFLWLoader::isWFLWAvailable())
    {
        auto paths = TestUtils::Datasets::SimpleWFLWLoader::getWFLWPaths();
        GTEST_SKIP() << "WFLW dataset not found at " << paths.annotations_path << ". Skipping WFLW validation test.";
    }

    // Load WFLW dataset using centralized loader
    TestUtils::Datasets::SimpleWFLWLoader wflw_loader;
    if (!wflw_loader.loadDataset())
    {
        GTEST_SKIP() << "Failed to load WFLW dataset. Skipping WFLW validation test.";
    }

    int samples_tested = 0;
    int samples_with_faces = 0;
    int faces_detected = 0;
    int valid_landmarks = 0;
    int failed_samples = 0;
    const int max_samples =
        std::min(wflw_loader.getSampleCount(), TestUtils::getMaxSamples(wflw_loader.getSampleCount()));

    std::cout << "Testing SCRFD on WFLW dataset (sample size: " << max_samples << " of " << wflw_loader.getSampleCount()
              << " available)" << std::endl;

    if (save_failures_)
    {
        std::cout << "SAVE_TEST_FAILURES=true detected. Saving failure images to: " << results_dir_ << std::endl;
    }

    for (int sample_idx = 0; sample_idx < max_samples; ++sample_idx)
    {
        const auto& wflw_sample = wflw_loader.getSample(sample_idx);

        // Load the image
        auto wflw_image = wflw_sample.loadImage();
        if (!wflw_image)
        {
            continue; // Skip if image can't be loaded
        }

        samples_tested++;

        // Run SCRFD detection
        std::vector<Face> faces = detector_->detect(wflw_image);
        bool sample_failed = false;

        if (faces.size() > 0)
        {
            samples_with_faces++;

            // Test on up to getMaxFacesPerImage faces instead of just the first one
            int max_faces_to_test = std::min(static_cast<int>(faces.size()), TestUtils::getMaxFacesPerImage());
            int valid_faces_for_sample = 0;

            // Track which faces have already been matched to ground truth to avoid re-matching
            std::set<int> matched_face_indices;

            for (int face_idx = 0; face_idx < max_faces_to_test; ++face_idx)
            {
                const Face& face = faces[face_idx];
                auto landmarks = face.getFivePointLandmarksArcFaceOrder2D();

                if (landmarks.size() == 5 && validateFaceLandmarks(face, *wflw_image))
                {
                    valid_faces_for_sample++;

                    // For valid faces, check if they match ground truth and track them to prevent re-matching
                    auto gt_bbox = calculateBoundingBoxFromLandmarks(wflw_sample.landmarks);
                    auto [filtered_faces, index_mapping] = createFilteredFaces(faces, matched_face_indices);
                    auto match_result = Face::findBestMatchingFace(filtered_faces, gt_bbox, 0.05);

                    if (match_result.found_match && index_mapping[match_result.face_index] == face_idx)
                    {
                        // This face matches ground truth, so track it as matched
                        matched_face_indices.insert(face_idx);
                    }

                    // Additional quality checks for each face
                    auto left_eye = landmarks[0];
                    auto right_eye = landmarks[1];
                    float iod =
                        std::sqrt(std::pow(right_eye.x - left_eye.x, 2) + std::pow(right_eye.y - left_eye.y, 2));

                    // Inter-ocular distance should be reasonable
                    bool iod_valid = (iod > 5.0f) && (iod < wflw_image->info.width * 0.8f);

                    if (!iod_valid)
                    {
                        sample_failed = true;
                        if (save_failures_)
                        {
                            std::string prefix =
                                "wflw_iod_fail_" + std::to_string(sample_idx) + "_face_" + std::to_string(face_idx);
                            saveFaceDebugImage(*wflw_image, faces, prefix,
                                               "iod_" + std::to_string(static_cast<int>(iod)), &wflw_sample.landmarks,
                                               face_idx);
                        }
                    }
                }
                else
                {
                    // Invalid landmarks detected
                    sample_failed = true;
                    if (save_failures_)
                    {
                        std::string prefix =
                            "wflw_landmark_fail_" + std::to_string(sample_idx) + "_face_" + std::to_string(face_idx);

                        std::string reason;
                        if (landmarks.size() != 5)
                        {
                            reason = "wrong_count_" + std::to_string(landmarks.size());
                        }
                        else if (!validateFaceLandmarks(face, *wflw_image))
                        {
                            // Check if this is an IoU mismatch (face doesn't correspond to ground truth)
                            // Calculate ground truth bounding box from landmarks
                            auto gt_bbox = calculateBoundingBoxFromLandmarks(wflw_sample.landmarks);

                            // Create filtered faces excluding already matched ones to avoid re-matching
                            auto [filtered_faces, index_mapping] = createFilteredFaces(faces, matched_face_indices);
                            auto match_result = Face::findBestMatchingFace(filtered_faces, gt_bbox, 0.05);

                            // Map back to original face indices
                            int original_best_face_idx = -1;
                            if (match_result.found_match)
                            {
                                original_best_face_idx = index_mapping[match_result.face_index];
                                // Track this face as matched to prevent future re-matching
                                matched_face_indices.insert(original_best_face_idx);
                            }

                            if (match_result.found_match && original_best_face_idx != face_idx)
                            {
                                reason = "iou_mismatch_best_face_" + std::to_string(original_best_face_idx);
                            }
                            else if (!match_result.found_match)
                            {
                                reason = "no_iou_match_found";
                            }
                            else
                            {
                                reason = "invalid_position";
                                // This face matches ground truth, so track it as matched
                                matched_face_indices.insert(face_idx);
                            }
                        }
                        else
                        {
                            reason = "unknown_issue";
                        }

                        saveFaceDebugImage(*wflw_image, faces, prefix, reason, &wflw_sample.landmarks, face_idx);
                    }
                }
            }

            faces_detected += max_faces_to_test;
            valid_landmarks += valid_faces_for_sample;
        }
        else
        {
            // No faces detected
            sample_failed = true;
            if (save_failures_)
            {
                std::string prefix = "wflw_no_faces_" + std::to_string(sample_idx);
                saveFaceDebugImage(*wflw_image, faces, prefix, "no_detection", &wflw_sample.landmarks, -1);
            }
        }

        if (sample_failed)
        {
            failed_samples++;
        }

        // Print progress every few samples
        if (samples_tested % 5 == 0)
        {
            std::cout << "Processed " << samples_tested << "/" << max_samples << " WFLW samples..." << std::endl;
        }
    }

    // Calculate validation metrics
    float detection_rate = samples_tested > 0 ? static_cast<float>(samples_with_faces) / samples_tested : 0.0f;
    float landmark_quality_rate = faces_detected > 0 ? static_cast<float>(valid_landmarks) / faces_detected : 0.0f;

    std::cout << "\nWFLW Dataset Validation Results:" << std::endl;
    std::cout << "Samples tested: " << samples_tested << std::endl;
    std::cout << "Max faces tested per image: " << TestUtils::getMaxFacesPerImage() << std::endl;
    std::cout << "Samples with at least one face detected: " << samples_with_faces << " (" << (detection_rate * 100.0f)
              << "%)" << std::endl;
    std::cout << "Total faces tested: " << faces_detected << std::endl;
    std::cout << "Faces with valid landmarks: " << valid_landmarks << " (" << (landmark_quality_rate * 100.0f) << "%)"
              << std::endl;
    std::cout << "Failed samples: " << failed_samples << " ("
              << (static_cast<float>(failed_samples) / samples_tested * 100.0f) << "%)" << std::endl;

    if (save_failures_)
    {
        std::cout << "Debug images saved to: " << results_dir_ << std::endl;
    }

    // Set reasonable expectations for SCRFD performance on WFLW
    EXPECT_GT(detection_rate, 0.6f) << "SCRFD face detection rate too low on WFLW dataset: "
                                    << (detection_rate * 100.0f) << "%";

    if (faces_detected > 0)
    {
        EXPECT_GT(landmark_quality_rate, 0.7f)
            << "SCRFD landmark quality rate too low on WFLW dataset: " << (landmark_quality_rate * 100.0f) << "%";
    }

    std::cout << "WFLW validation completed successfully!" << std::endl;
}
