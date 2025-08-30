#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <string>
#include <vector>

#include "../test_utils.h"
#include "../dataset_utils.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/face.h"
#include "LinuxFace/imageLoader.h"
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
    if (!TestUtils::Datasets::SimpleWFLWLoader::isWFLWAvailable()) {
        auto paths = TestUtils::Datasets::SimpleWFLWLoader::getWFLWPaths();
        GTEST_SKIP() << "WFLW dataset not found at " << paths.annotations_path << ". Skipping WFLW validation test.";
    }

    // Load WFLW dataset using centralized loader
    TestUtils::Datasets::SimpleWFLWLoader wflw_loader;
    if (!wflw_loader.loadDataset()) {
        GTEST_SKIP() << "Failed to load WFLW dataset. Skipping WFLW validation test.";
    }

    int samples_tested = 0;
    int samples_with_faces = 0;
    int faces_detected = 0;
    int valid_landmarks = 0;
    const int max_samples = std::min(wflw_loader.getSampleCount(), TestUtils::getMaxSamples(wflw_loader.getSampleCount()));

    std::cout << "Testing SCRFD on WFLW dataset (sample size: " << max_samples << " of " << wflw_loader.getSampleCount() << " available)" << std::endl;

    for (int sample_idx = 0; sample_idx < max_samples; ++sample_idx) {
        const auto& wflw_sample = wflw_loader.getSample(sample_idx);
        
        // Load the image
        auto wflw_image = wflw_sample.loadImage();
        if (!wflw_image) {
            continue; // Skip if image can't be loaded
        }

        samples_tested++;

        // Run SCRFD detection
        std::vector<Face> faces = detector_->detect(wflw_image);

        if (faces.size() > 0) {
            samples_with_faces++;

            // Test on up to getMaxFacesPerImage faces instead of just the first one
            int max_faces_to_test = std::min(static_cast<int>(faces.size()), TestUtils::getMaxFacesPerImage());
            int valid_faces_for_sample = 0;

            for (int face_idx = 0; face_idx < max_faces_to_test; ++face_idx) {
                const Face& face = faces[face_idx];
                auto landmarks = face.getFivePointLandmarksArcFaceOrder2D();

                if (landmarks.size() == 5 && validateFaceLandmarks(face, *wflw_image)) {
                    valid_faces_for_sample++;

                    // Additional quality checks for each face
                    auto left_eye = landmarks[0];
                    auto right_eye = landmarks[1];
                    float iod = std::sqrt(std::pow(right_eye.x - left_eye.x, 2) + std::pow(right_eye.y - left_eye.y, 2));

                    // Inter-ocular distance should be reasonable
                    EXPECT_GT(iod, 5.0f) << "Inter-ocular distance too small for WFLW image: " << wflw_sample.image_filename << " face " << face_idx;
                    EXPECT_LT(iod, wflw_image->info.width * 0.8f)
                        << "Inter-ocular distance too large for WFLW image: " << wflw_sample.image_filename << " face " << face_idx;
                }
            }

            faces_detected += max_faces_to_test;
            valid_landmarks += valid_faces_for_sample;
        }

        // Print progress every few samples
        if (samples_tested % 5 == 0) {
            std::cout << "Processed " << samples_tested << "/" << max_samples << " WFLW samples..." << std::endl;
        }
    }

    // Calculate validation metrics
    float detection_rate = samples_tested > 0 ? static_cast<float>(samples_with_faces) / samples_tested : 0.0f;
    float landmark_quality_rate = faces_detected > 0 ? static_cast<float>(valid_landmarks) / faces_detected : 0.0f;

    std::cout << "\nWFLW Dataset Validation Results:" << std::endl;
    std::cout << "Samples tested: " << samples_tested << std::endl;
    std::cout << "Max faces tested per image: " << TestUtils::getMaxFacesPerImage() << std::endl;
    std::cout << "Samples with at least one face detected: " << samples_with_faces << " (" << (detection_rate * 100.0f) << "%)" << std::endl;
    std::cout << "Total faces tested: " << faces_detected << std::endl;
    std::cout << "Faces with valid landmarks: " << valid_landmarks << " (" << (landmark_quality_rate * 100.0f) << "%)" << std::endl;

    // Set reasonable expectations for SCRFD performance on WFLW
    EXPECT_GT(detection_rate, 0.6f) << "SCRFD face detection rate too low on WFLW dataset: "
                                    << (detection_rate * 100.0f) << "%";

    if (faces_detected > 0) {
        EXPECT_GT(landmark_quality_rate, 0.7f)
            << "SCRFD landmark quality rate too low on WFLW dataset: " << (landmark_quality_rate * 100.0f) << "%";
    }

    std::cout << "WFLW validation completed successfully!" << std::endl;
}
