// Unit test for MediaPipe Face Landmarks detector
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <iostream>
#include <onnxruntime_cxx_api.h>
#include "config.hpp"
#include "LinuxFace/onnx/mediaPipe_FaceLandmarks.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/face.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/landmark_converter.h"

using namespace linuxface;

class MediaPipeUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
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
        mediapipe_detector_ = std::make_unique<MediaPipeFaceLandmarks>(models_folder + "MediaPipeFaceLandmarkDetector.onnx");
        scrfd_detector_ = std::make_unique<SCRFDetector>(models_folder + "scrfd_500m_bnkps_shape640x640.onnx");
    }
    
    std::unique_ptr<Image> createTestImage(int width = 192, int height = 192) {
        size_t data_size = width * height * 3;
        auto image = std::make_unique<Image>(data_size);
        image->info.width = width;
        image->info.height = height;
        image->info.pixelSizeBytes = 3;
        image->info.format = ImageFormat::RGB;
        
        // Create a realistic face-like pattern suitable for MediaPipe (expects 192x192)
        unsigned char* data = image->data();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = (y * width + x) * 3;
                // Create a circular gradient that might resemble a face
                double cx = width / 2.0;
                double cy = height / 2.0;
                double dist = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
                double normalized_dist = std::min(1.0, dist / (std::min(width, height) / 2.0));
                
                // Add some facial features
                unsigned char intensity = static_cast<unsigned char>(255 * (1.0 - normalized_dist * 0.3));
                
                // Add eye-like features
                if ((x > width*0.3 && x < width*0.4 && y > height*0.4 && y < height*0.5) ||
                    (x > width*0.6 && x < width*0.7 && y > height*0.4 && y < height*0.5)) {
                    intensity = static_cast<unsigned char>(intensity * 0.3); // Darker for eyes
                }
                
                // Add mouth-like feature
                if (x > width*0.4 && x < width*0.6 && y > height*0.65 && y < height*0.7) {
                    intensity = static_cast<unsigned char>(intensity * 0.5); // Darker for mouth
                }
                
                data[idx] = intensity;         // R
                data[idx + 1] = intensity;     // G  
                data[idx + 2] = intensity;     // B
            }
        }
        return image;
    }
    
    Face createMockFace(int width = 192, int height = 192) {
        // Create a face with realistic 5-point landmarks for MediaPipe input
        double cx = width / 2.0;
        double cy = height / 2.0;
        double eye_dist = width * 0.15; // 15% of image width between eyes
        
        std::vector<FaceLandmark> landmarks;
        // Left eye
        landmarks.emplace_back(FaceLandmark{0, math_utils::Point3D(cx - eye_dist/2, cy - height*0.05, 0.0)});
        // Right eye  
        landmarks.emplace_back(FaceLandmark{1, math_utils::Point3D(cx + eye_dist/2, cy - height*0.05, 0.0)});
        // Nose
        landmarks.emplace_back(FaceLandmark{2, math_utils::Point3D(cx, cy + height*0.02, 0.0)});
        // Left mouth corner
        landmarks.emplace_back(FaceLandmark{3, math_utils::Point3D(cx - eye_dist/3, cy + height*0.08, 0.0)});
        // Right mouth corner
        landmarks.emplace_back(FaceLandmark{4, math_utils::Point3D(cx + eye_dist/3, cy + height*0.08, 0.0)});
        
        // Create bounding box around the face
        FaceBoundingBox bbox(cx - eye_dist, cy - height*0.15, cx + eye_dist, cy + height*0.2);
        bbox.score = 0.95f;
        
        return Face(std::move(landmarks), bbox);
    }
    
    std::unique_ptr<MediaPipeFaceLandmarks> mediapipe_detector_;
    std::unique_ptr<SCRFDetector> scrfd_detector_;
};

// Test constructor and initialization
TEST_F(MediaPipeUnitTest, ConstructorValidModel) {
    EXPECT_TRUE(mediapipe_detector_->isReady());
}

TEST_F(MediaPipeUnitTest, ConstructorInvalidModel) {
    MediaPipeFaceLandmarks invalid_detector("nonexistent_model.onnx");
    EXPECT_FALSE(invalid_detector.isReady());
}

// Test basic functionality
TEST_F(MediaPipeUnitTest, BasicFunctionality) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    
    // Basic detection should work without crashing
    auto result = mediapipe_detector_->detectAligned(test_image);
    
    // MediaPipe should return 468 landmarks
    EXPECT_EQ(result.landmarks.size(), 468);
    
    // Each landmark should have 3 coordinates (x, y, z)
    for (const auto& landmark : result.landmarks) {
        EXPECT_EQ(landmark.size(), 3);
    }
    
    // Score should be within reasonable range
    EXPECT_GE(result.score, 0.0f);
    EXPECT_LE(result.score, 1.0f);
}

// Test detection with different image sizes - should work as MediaPipe expects cropped faces
TEST_F(MediaPipeUnitTest, DifferentImageSizes) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    std::vector<std::pair<int, int>> sizes = {
        {96, 96}, {192, 192}, {384, 384}, {512, 512}
    };
    
    for (const auto& size : sizes) {
        auto test_image = createTestImage(size.first, size.second);
        
        auto result = mediapipe_detector_->detectAligned(test_image);
        
        // Should still produce 468 landmarks regardless of input size (gets resized to 192x192)
        EXPECT_EQ(result.landmarks.size(), 468) 
            << "Failed for image size " << size.first << "x" << size.second;
            
        // Score should be valid
        EXPECT_GE(result.score, 0.0f) << "Invalid score for size " << size.first << "x" << size.second;
        EXPECT_LE(result.score, 1.0f) << "Invalid score for size " << size.first << "x" << size.second;
    }
}

// Test landmark coordinate validation
TEST_F(MediaPipeUnitTest, LandmarkCoordinateValidation) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    auto result = mediapipe_detector_->detectAligned(test_image);
    
    EXPECT_EQ(result.landmarks.size(), 468);
    
    // All landmarks should have valid coordinates
    // MediaPipe outputs normalized coordinates [0,1] for x,y and depth for z
    for (size_t i = 0; i < result.landmarks.size(); ++i) {
        const auto& landmark = result.landmarks[i];
        
        // X and Y should be normalized coordinates (typically 0-1 range, but can exceed)
        EXPECT_GE(landmark[0], -0.5f) << "Landmark " << i << " x coordinate out of reasonable bounds";
        EXPECT_LE(landmark[0], 1.5f) << "Landmark " << i << " x coordinate out of reasonable bounds";
        
        EXPECT_GE(landmark[1], -0.5f) << "Landmark " << i << " y coordinate out of reasonable bounds";
        EXPECT_LE(landmark[1], 1.5f) << "Landmark " << i << " y coordinate out of reasonable bounds";
        
        // Z coordinate represents depth (can be negative or positive)
        EXPECT_GE(landmark[2], -1.0f) << "Landmark " << i << " z coordinate out of reasonable bounds";
        EXPECT_LE(landmark[2], 1.0f) << "Landmark " << i << " z coordinate out of reasonable bounds";
    }
}

// Test edge cases - empty or null image
TEST_F(MediaPipeUnitTest, EmptyImage) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    std::unique_ptr<Image> null_image;
    auto result = mediapipe_detector_->detectAligned(null_image);
    
    // Should return empty result gracefully
    EXPECT_EQ(result.landmarks.size(), 0);
    EXPECT_EQ(result.score, 0.0f);
}

// Test edge cases - very small image
TEST_F(MediaPipeUnitTest, VerySmallImage) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto tiny_image = createTestImage(10, 10);
    auto result = mediapipe_detector_->detectAligned(tiny_image);
    
    // Should handle gracefully and still produce landmarks (gets upscaled to 192x192)
    EXPECT_EQ(result.landmarks.size(), 468);
}

// Test memory management with repeated calls
TEST_F(MediaPipeUnitTest, MemoryManagementRepeatedCalls) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    
    for (int i = 0; i < 10; ++i) {
        auto result = mediapipe_detector_->detectAligned(test_image);
        
        // Each call should successfully produce 468 landmarks
        EXPECT_EQ(result.landmarks.size(), 468);
        EXPECT_GE(result.score, 0.0f);
        EXPECT_LE(result.score, 1.0f);
    }
}

// Test performance bounds
TEST_F(MediaPipeUnitTest, PerformanceBounds) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    
    auto start = std::chrono::high_resolution_clock::now();
    auto result = mediapipe_detector_->detectAligned(test_image);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // MediaPipe detection should complete within reasonable time
    EXPECT_LT(duration.count(), 15000) << "MediaPipe detection took too long: " << duration.count() << "ms";
    EXPECT_EQ(result.landmarks.size(), 468);
}

// Test integration with SCRFD face detection
TEST_F(MediaPipeUnitTest, IntegrationWithSCRFD) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    ASSERT_TRUE(scrfd_detector_->isReady());
    
    // Create larger image for SCRFD to detect faces
    auto test_image = createTestImage(640, 480);
    
    // First detect faces with SCRFD
    std::vector<Face> faces = scrfd_detector_->detect(test_image);
    
    // If faces detected, we could potentially crop them for MediaPipe
    // For this test, we'll just verify both detectors work
    EXPECT_NO_THROW({
        auto result = mediapipe_detector_->detectAligned(createTestImage(192, 192));
        EXPECT_EQ(result.landmarks.size(), 468);
    });
}

// Test landmark consistency across multiple calls
TEST_F(MediaPipeUnitTest, LandmarkConsistency) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    
    // Run detection multiple times on same image
    auto result1 = mediapipe_detector_->detectAligned(test_image);
    auto result2 = mediapipe_detector_->detectAligned(test_image);
    
    ASSERT_EQ(result1.landmarks.size(), 468);
    ASSERT_EQ(result2.landmarks.size(), 468);
    
    // Results should be identical for same input
    for (size_t i = 0; i < 468; ++i) {
        for (int j = 0; j < 3; ++j) {
            EXPECT_FLOAT_EQ(result1.landmarks[i][j], result2.landmarks[i][j])
                << "Landmark " << i << " coordinate " << j << " differs between runs";
        }
    }
    
    EXPECT_FLOAT_EQ(result1.score, result2.score) << "Score differs between runs";
}

// Test tensor normalization (MediaPipe expects MINMAX normalization)
TEST_F(MediaPipeUnitTest, TensorNormalization) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    // Create test image with known pixel values
    auto test_image = createTestImage(192, 192);
    
    // Manually set some pixels to known values to test normalization
    unsigned char* data = test_image->data();
    data[0] = 0;     // Minimum value
    data[1] = 0;
    data[2] = 0;
    
    data[3] = 255;   // Maximum value  
    data[4] = 255;
    data[5] = 255;
    
    data[6] = 128;   // Middle value
    data[7] = 128;
    data[8] = 128;
    
    auto result = mediapipe_detector_->detectAligned(test_image);
    
    // Should still work with extreme pixel values
    EXPECT_EQ(result.landmarks.size(), 468);
    EXPECT_GE(result.score, 0.0f);
    EXPECT_LE(result.score, 1.0f);
}

// Test landmark format and structure
TEST_F(MediaPipeUnitTest, LandmarkFormatStructure) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    auto result = mediapipe_detector_->detectAligned(test_image);
    
    // Verify MediaPipe-specific landmark structure
    EXPECT_EQ(result.landmarks.size(), 468);
    
    // MediaPipe landmarks are organized by facial regions
    // Face outline: 0-16 (17 points)
    // Eyebrows: left 17-21, right 22-26 (10 points total) 
    // Nose: 27-35 (9 points)
    // Eyes: left 36-41, right 42-47 (12 points total)
    // Lips: outer 48-59, inner 60-67 (20 points total)
    // Plus additional detailed landmarks up to 468
    
    // Each landmark should be a valid 3D point
    for (const auto& landmark : result.landmarks) {
        EXPECT_EQ(landmark.size(), 3) << "Each landmark should have 3 coordinates";
        
        // Coordinates should be finite numbers
        for (int i = 0; i < 3; ++i) {
            EXPECT_TRUE(std::isfinite(landmark[i])) << "Landmark coordinate should be finite";
        }
    }
}

// Test score validation
TEST_F(MediaPipeUnitTest, ScoreValidation) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    auto result = mediapipe_detector_->detectAligned(test_image);
    
    // Score should be a valid probability-like value
    EXPECT_TRUE(std::isfinite(result.score)) << "Score should be finite";
    EXPECT_GE(result.score, 0.0f) << "Score should be non-negative";
    EXPECT_LE(result.score, 1.0f) << "Score should not exceed 1.0";
}

// Test conversion to FaceLandmark format (for integration with existing code)
TEST_F(MediaPipeUnitTest, ConversionToFaceLandmarkFormat) {
    ASSERT_TRUE(mediapipe_detector_->isReady());
    
    auto test_image = createTestImage();
    auto result = mediapipe_detector_->detectAligned(test_image);
    
    ASSERT_EQ(result.landmarks.size(), 468);
    
    // Convert MediaPipe result to FaceLandmark vector for compatibility
    std::vector<FaceLandmark> face_landmarks;
    face_landmarks.reserve(468);
    
    for (size_t i = 0; i < result.landmarks.size(); ++i) {
        const auto& mp_landmark = result.landmarks[i];
        // Convert normalized coordinates to image coordinates (assuming 192x192)
        double x = mp_landmark[0] * 192.0;
        double y = mp_landmark[1] * 192.0;
        double z = mp_landmark[2];
        
        face_landmarks.emplace_back(FaceLandmark{
            static_cast<unsigned int>(i),
            math_utils::Point3D(x, y, z)
        });
    }
    
    EXPECT_EQ(face_landmarks.size(), 468);
    
    // Verify converted landmarks have reasonable coordinates
    for (const auto& landmark : face_landmarks) {
        EXPECT_GE(landmark.p.x, -50.0) << "X coordinate out of reasonable range";
        EXPECT_LE(landmark.p.x, 250.0) << "X coordinate out of reasonable range";
        EXPECT_GE(landmark.p.y, -50.0) << "Y coordinate out of reasonable range";
        EXPECT_LE(landmark.p.y, 250.0) << "Y coordinate out of reasonable range";
    }
}
