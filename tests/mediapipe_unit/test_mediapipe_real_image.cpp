// Complete integration tests for MediaPipe Face Landmarks with real images and comprehensive validation
// This comprehensive test suite validates MediaPipe 468-landmark detection with real facial images
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <onnxruntime_cxx_api.h>
#include "config.hpp"
#include "LinuxFace/onnx/mediaPipe_FaceLandmarks.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/imageLoader.h"
#include "LinuxFace/face.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/landmark_converter.h"
#include "../common/test_utils.h"
#include "../common/dataset_utils.h"

using namespace linuxface;

class MediaPipeRealImageTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Load test configuration - copied from PFLD test
        std::vector<std::string> config_paths = {"../config.yaml", "config.yaml",
                                                 "../tests/wflw_integration/test_config.yaml"};
        bool config_loaded = false;

        for (const auto& config_path : config_paths)
        {
            if (std::ifstream(config_path).good())
            {
                bool reloaded = Config::getInstance().reloadFromFile(config_path.c_str());
                if (reloaded)
                {
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
        mediapipe_detector_ = std::make_unique<MediaPipeFaceLandmarks>(models_folder + "MediaPipeFaceLandmarkDetector.onnx");
        scrfd_detector_ = std::make_unique<SCRFDetector>(models_folder + "scrfd_500m_bnkps_shape640x640.onnx");
    }

    // Helper to create test image like in PFLD tests
    std::unique_ptr<Image> createTestImage(int width = 192, int height = 192) {
        size_t data_size = width * height * 3;
        auto image = std::make_unique<Image>(data_size);
        image->info.width = width;
        image->info.height = height;
        image->info.pixelSizeBytes = 3;
        image->info.format = ImageFormat::RGB;
        
        // Create a face-like pattern
        unsigned char* data = image->data();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = (y * width + x) * 3;
                double cx = width / 2.0;
                double cy = height / 2.0;
                double dist = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
                double normalized_dist = std::min(1.0, dist / (std::min(width, height) / 2.0));
                
                unsigned char intensity = static_cast<unsigned char>(255 * (1.0 - normalized_dist * 0.5));
                data[idx] = intensity;         // R
                data[idx + 1] = intensity;     // G  
                data[idx + 2] = intensity;     // B
            }
        }
        return image;
    }

    std::unique_ptr<MediaPipeFaceLandmarks> mediapipe_detector_;
    std::unique_ptr<SCRFDetector> scrfd_detector_;
};

// Test constructor and initialization with comprehensive validation
TEST_F(MediaPipeRealImageTest, ConstructorValidModel) {
    EXPECT_TRUE(mediapipe_detector_->isReady());
}

TEST_F(MediaPipeRealImageTest, ConstructorInvalidModel) {
    MediaPipeFaceLandmarks invalid_detector("nonexistent_model.onnx");
    EXPECT_FALSE(invalid_detector.isReady());
}

// Test comprehensive functionality with synthetic image data
TEST_F(MediaPipeRealImageTest, BasicFunctionality) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    
    // Test MediaPipe detection on synthetic image
    auto result = mediapipe_detector_->detectAligned(test_image);
    
    // Should detect 468 landmarks
    EXPECT_EQ(result.landmarks.size(), 468);
    
    // Verify score is reasonable
    EXPECT_GE(result.score, 0.0f);
    EXPECT_LE(result.score, 1.0f);
}

// Test comprehensive MediaPipe detection with real facial images
TEST_F(MediaPipeRealImageTest, ComprehensiveRealImageTest) {
    ASSERT_TRUE(mediapipe_detector_->isReady()) << "MediaPipe detector not ready";
    
    // Try to find a test image
    std::string test_image_path = TestUtils::getTestImagePath("single_face.jpeg");
    if (!std::filesystem::exists(test_image_path)) {
        test_image_path = "media/example.jpg";
        if (!std::filesystem::exists(test_image_path)) {
            GTEST_SKIP() << "No test image found for MediaPipe testing";
        }
    }
    
    // Load image using ImageLoader like in PFLD tests
    auto image = ImageLoader::loadImageFromFile(test_image_path);
    if (!image) {
        GTEST_SKIP() << "Failed to load test image: " << test_image_path;
    }
    
    std::cout << "Testing MediaPipe with image: " << test_image_path << std::endl;
    std::cout << "Image dimensions: " << image->info.width << "x" << image->info.height << std::endl;
    
    // Resize to MediaPipe input size (192x192) 
    auto resized_image = image->scaleTo(192, 192);
    ASSERT_TRUE(resized_image) << "Failed to resize image for MediaPipe";
    
    // Test MediaPipe landmark detection
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result = mediapipe_detector_->detectAligned(resized_image);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto inference_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Verify MediaPipe landmark count
    EXPECT_EQ(result.landmarks.size(), 468) << "MediaPipe should detect 468 landmarks";
    
    // Verify landmark coordinates are within reasonable bounds for normalized coordinates
    int valid_landmarks = 0;
    for (size_t i = 0; i < result.landmarks.size(); ++i) {
        const auto& landmark = result.landmarks[i];
        
        // MediaPipe outputs normalized coordinates in [0,1] range (with some tolerance)
        if (landmark.size() >= 2 &&
            landmark[0] >= -0.2f && landmark[0] <= 1.2f &&
            landmark[1] >= -0.2f && landmark[1] <= 1.2f) {
            valid_landmarks++;
        }
    }
    
    // Expect most landmarks to be valid
    EXPECT_GT(valid_landmarks, 400) << "Most landmarks should have valid normalized coordinates";
    EXPECT_LT(inference_time.count(), 1000) << "MediaPipe inference should be reasonably fast (<1s)";
    
    std::cout << "MediaPipe detected " << result.landmarks.size() << " landmarks" << std::endl;
    std::cout << "Valid landmarks: " << valid_landmarks << "/468" << std::endl;
    std::cout << "Inference time: " << inference_time.count() << "ms" << std::endl;
}

// Test performance bounds and optimization metrics
TEST_F(MediaPipeRealImageTest, PerformanceBounds) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    
    auto start = std::chrono::high_resolution_clock::now();
    auto result = mediapipe_detector_->detectAligned(test_image);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // MediaPipe detection should complete within reasonable time
    EXPECT_LT(duration.count(), 5000) << "MediaPipe detection took too long: " << duration.count() << "ms";
    EXPECT_EQ(result.landmarks.size(), 468);
}

// Test comprehensive image size handling and scaling robustness  
TEST_F(MediaPipeRealImageTest, DifferentImageSizes) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    // MediaPipe expects 192x192 input, but test scaling from different sizes
    std::vector<std::pair<int, int>> input_sizes = {
        {96, 96}, {192, 192}, {384, 384}, {512, 512}
    };
    
    for (const auto& size : input_sizes) {
        auto original_image = createTestImage(size.first, size.second);
        auto resized_image = original_image->scaleTo(192, 192); // MediaPipe input size
        
        auto result = mediapipe_detector_->detectAligned(resized_image);
        
        // Should always produce 468 landmarks regardless of original image size
        EXPECT_EQ(result.landmarks.size(), 468) 
            << "Failed for original image size " << size.first << "x" << size.second;
    }
}

// Test landmark quality and anatomical correctness
TEST_F(MediaPipeRealImageTest, LandmarkQualityValidation) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    auto result = mediapipe_detector_->detectAligned(test_image);
    
    ASSERT_EQ(result.landmarks.size(), 468);
    
    // Validate landmark distribution (should be spread across face regions)
    float min_x = 1.0f, max_x = 0.0f, min_y = 1.0f, max_y = 0.0f;
    int valid_coords = 0;
    
    for (const auto& landmark : result.landmarks) {
        if (landmark.size() >= 2) {
            float x = landmark[0], y = landmark[1];
            if (x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f) {
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
                valid_coords++;
            }
        }
    }
    
    // Expect landmarks to span a reasonable portion of the image
    EXPECT_GT(max_x - min_x, 0.3f) << "Landmarks should span horizontally";
    EXPECT_GT(max_y - min_y, 0.3f) << "Landmarks should span vertically";
    EXPECT_GT(valid_coords, 450) << "Most landmarks should have valid coordinates";
}

// Test landmark consistency across multiple detections
TEST_F(MediaPipeRealImageTest, LandmarkConsistency) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    
    // Run detection multiple times
    std::vector<MediaPipeFaceLandmarks::Result> results;
    for (int i = 0; i < 3; ++i) {
        results.push_back(mediapipe_detector_->detectAligned(test_image));
    }
    
    // Verify consistency between runs
    for (size_t run = 1; run < results.size(); ++run) {
        EXPECT_EQ(results[0].landmarks.size(), results[run].landmarks.size());
        
        // Check that landmark positions are reasonably consistent
        float total_deviation = 0.0f;
        int compared_points = 0;
        
        for (size_t i = 0; i < std::min(results[0].landmarks.size(), results[run].landmarks.size()); ++i) {
            if (results[0].landmarks[i].size() >= 2 && results[run].landmarks[i].size() >= 2) {
                float dx = results[0].landmarks[i][0] - results[run].landmarks[i][0];
                float dy = results[0].landmarks[i][1] - results[run].landmarks[i][1];
                total_deviation += std::sqrt(dx*dx + dy*dy);
                compared_points++;
            }
        }
        
        if (compared_points > 0) {
            float avg_deviation = total_deviation / compared_points;
            EXPECT_LT(avg_deviation, 0.05f) << "Average landmark deviation should be small between runs";
        }
    }
}

// Test integration with landmark converter
TEST_F(MediaPipeRealImageTest, LandmarkConversionIntegration) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    auto result = mediapipe_detector_->detectAligned(test_image);
    
    ASSERT_EQ(result.landmarks.size(), 468);
    
    // Convert MediaPipe landmarks to FaceLandmark objects
    std::vector<FaceLandmark> face_landmarks;
    for (size_t i = 0; i < result.landmarks.size(); ++i) {
        if (result.landmarks[i].size() >= 2) {
            FaceLandmark fl;
            fl.i = static_cast<unsigned int>(i);
            fl.p.x = result.landmarks[i][0];
            fl.p.y = result.landmarks[i][1];
            fl.p.z = result.landmarks[i].size() >= 3 ? result.landmarks[i][2] : 0.0f;
            face_landmarks.push_back(fl);
        }
    }
    
    // Test conversion to PFLD format using the specific conversion method
    auto pfld_landmarks = LandmarkConverter::mediapipeToPfld(face_landmarks);
    EXPECT_EQ(pfld_landmarks.size(), 106) << "Should convert to 106 PFLD landmarks";
    
    // Test conversion to WFLW format using the specific conversion method 
    auto wflw_landmarks = LandmarkConverter::mediapipeToWflw(face_landmarks);
    EXPECT_EQ(wflw_landmarks.size(), 98) << "Should convert to 98 WFLW landmarks";
}

// Test memory usage and resource management
TEST_F(MediaPipeRealImageTest, ResourceManagement) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    // Create multiple images of different sizes
    std::vector<std::unique_ptr<Image>> test_images;
    for (int size : {96, 192, 384}) {
        test_images.push_back(createTestImage(size, size));
    }
    
    // Run many detections to test memory management
    for (int iteration = 0; iteration < 10; ++iteration) {
        for (auto& image : test_images) {
            auto resized = image->scaleTo(192, 192);
            auto result = mediapipe_detector_->detectAligned(resized);
            EXPECT_EQ(result.landmarks.size(), 468);
        }
    }
    
    // If we reach here without crashes, memory management is working
    SUCCEED() << "Memory management test completed successfully";
}


