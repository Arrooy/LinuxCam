// Unit test for PFLD detector with mocked SCRFD dependency
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include "config.hpp"
#include "LinuxFace/onnx/pfld.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/face.h"
#include "LinuxFace/math_utils.h"

using namespace linuxface;

class PFLDUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Load test configuration - only use test config, not root config
        std::string config_paths[] = {
            "tests/wflw_integration/test_config.yaml",
            "../tests/wflw_integration/test_config.yaml"
        };
        
        bool config_loaded = false;
        for (const auto& config_path : config_paths) {
            if (Config::getInstance().reloadFromFile(config_path.c_str())) {
                config_loaded = Config::getInstance().loadConfiguration();
                if (config_loaded) break;
            }
        }
        ASSERT_TRUE(config_loaded) << "Could not find test_config.yaml in expected test paths";
        
        std::string models_folder = Config::getInstance().getModelFolderPath();
        pfld_detector_ = std::make_unique<PFLDDetector>(models_folder + "pfld-106-v3.onnx");
        scrfd_detector_ = std::make_unique<SCRFDetector>(models_folder + "scrfd_500m_bnkps_shape640x640.onnx");
    }
    
    std::unique_ptr<Image> createTestImage(int width = 640, int height = 480) {
        size_t data_size = width * height * 3;
        auto image = std::make_unique<Image>(data_size);
        image->info.width = width;
        image->info.height = height;
        image->info.pixelSizeBytes = 3;
        image->info.format = ImageFormat::RGB;
        
        // Create a more realistic face-like test pattern
        unsigned char* data = image->data();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = (y * width + x) * 3;
                // Create a circular gradient that might resemble a face
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
    
    Face createMockFace(int width = 640, int height = 480) {
        // Create a face with realistic 5-point landmarks
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
    
    std::unique_ptr<PFLDDetector> pfld_detector_;
    std::unique_ptr<SCRFDetector> scrfd_detector_;
};

// Test constructor and initialization
TEST_F(PFLDUnitTest, ConstructorValidModel) {
    EXPECT_TRUE(pfld_detector_->isReady());
}

TEST_F(PFLDUnitTest, ConstructorInvalidModel) {
    PFLDDetector invalid_detector("nonexistent_model.onnx");
    EXPECT_FALSE(invalid_detector.isReady());
}

// Test basic functionality
TEST_F(PFLDUnitTest, BasicFunctionality) {
    ASSERT_TRUE(pfld_detector_->isReady());
    
    auto test_image = createTestImage();
    Face mock_face = createMockFace();
    
    // Basic detection should work without crashing
    pfld_detector_->detect(test_image, mock_face);
    
    // The face should have landmarks after detection
    auto landmarks = mock_face.getLandmarks();
    EXPECT_GT(landmarks.size(), 0);
}

// Test basic detection with mock face
TEST_F(PFLDUnitTest, BasicDetectionWithMockFace) {
    ASSERT_TRUE(pfld_detector_->isReady());
    
    auto test_image = createTestImage();
    Face mock_face = createMockFace();
    
    // Should not crash
    pfld_detector_->detect(test_image, mock_face);
    
    // Face should now have 106 landmarks from PFLD
    auto pfld_landmarks = mock_face.getLandmarks();
    EXPECT_EQ(pfld_landmarks.size(), 106);
}

// Test detection with real SCRFD integration
TEST_F(PFLDUnitTest, DetectionWithRealSCRFD) {
    ASSERT_TRUE(pfld_detector_->isReady());
    ASSERT_TRUE(scrfd_detector_->isReady());
    
    auto test_image = createTestImage();
    
    // First detect faces with SCRFD
    std::vector<Face> faces = scrfd_detector_->detect(test_image);
    
    // If faces detected, run PFLD on them
    for (auto& face : faces) {
        pfld_detector_->detect(test_image, face);
        
        // After PFLD, should have 106 landmarks
        EXPECT_EQ(face.getLandmarks().size(), 106);
    }
}

// Test detectSimilar method
TEST_F(PFLDUnitTest, DetectSimilarMethod) {
    ASSERT_TRUE(pfld_detector_->isReady());
    
    auto test_image = createTestImage();
    Face mock_face = createMockFace();
    
    pfld_detector_->detectSimilar(test_image, mock_face);
    
    // Should have 106 landmarks
    auto landmarks = mock_face.getLandmarks();
    EXPECT_EQ(landmarks.size(), 106);
    
    // All landmarks should be within reasonable bounds
    for (const auto& landmark : landmarks) {
        EXPECT_GE(landmark.p.x, -100); // Allow some margin for transformation
        EXPECT_LE(landmark.p.x, test_image->info.width + 100);
        EXPECT_GE(landmark.p.y, -100);
        EXPECT_LE(landmark.p.y, test_image->info.height + 100);
    }
}

// Test detectOpenCv method  
TEST_F(PFLDUnitTest, DetectOpenCvMethod) {
    ASSERT_TRUE(pfld_detector_->isReady());
    
    auto test_image = createTestImage();
    Face mock_face = createMockFace();
    
    pfld_detector_->detectOpenCv(test_image, mock_face);
    
    // Should have 106 landmarks
    auto landmarks = mock_face.getLandmarks();
    EXPECT_EQ(landmarks.size(), 106);
    
    // Check landmarks are reasonable
    for (const auto& landmark : landmarks) {
        EXPECT_GT(landmark.p.x, -200); // More lenient bounds for OpenCV method
        EXPECT_LT(landmark.p.x, test_image->info.width + 200);
        EXPECT_GT(landmark.p.y, -200);
        EXPECT_LT(landmark.p.y, test_image->info.height + 200);
    }
}

// Test edge cases - face too small
TEST_F(PFLDUnitTest, FaceTooSmall) {
    ASSERT_TRUE(pfld_detector_->isReady());
    
    auto test_image = createTestImage();
    
    // Create very small face
    std::vector<FaceLandmark> tiny_landmarks;
    double cx = 320, cy = 240;
    double eye_dist = 5; // Very small distance
    
    tiny_landmarks.emplace_back(FaceLandmark{0, math_utils::Point3D(cx - eye_dist/2, cy, 0.0)});
    tiny_landmarks.emplace_back(FaceLandmark{1, math_utils::Point3D(cx + eye_dist/2, cy, 0.0)});
    tiny_landmarks.emplace_back(FaceLandmark{2, math_utils::Point3D(cx, cy + 2, 0.0)});
    tiny_landmarks.emplace_back(FaceLandmark{3, math_utils::Point3D(cx - 2, cy + 3, 0.0)});
    tiny_landmarks.emplace_back(FaceLandmark{4, math_utils::Point3D(cx + 2, cy + 3, 0.0)});
    
    FaceBoundingBox tiny_bbox(cx - 10, cy - 10, cx + 10, cy + 10);
    tiny_bbox.score = 0.9f;
    
    Face tiny_face(std::move(tiny_landmarks), tiny_bbox);
    
    // Should handle gracefully
    pfld_detector_->detect(test_image, tiny_face);
}

// Test edge cases - face at image border
TEST_F(PFLDUnitTest, FaceAtImageBorder) {
    ASSERT_TRUE(pfld_detector_->isReady());
    
    auto test_image = createTestImage();
    
    // Create face at border
    std::vector<FaceLandmark> border_landmarks;
    double cx = 50, cy = 50; // Near top-left corner
    double eye_dist = 30;
    
    border_landmarks.emplace_back(FaceLandmark{0, math_utils::Point3D(cx - eye_dist/2, cy, 0.0)});
    border_landmarks.emplace_back(FaceLandmark{1, math_utils::Point3D(cx + eye_dist/2, cy, 0.0)});
    border_landmarks.emplace_back(FaceLandmark{2, math_utils::Point3D(cx, cy + 10, 0.0)});
    border_landmarks.emplace_back(FaceLandmark{3, math_utils::Point3D(cx - 10, cy + 15, 0.0)});
    border_landmarks.emplace_back(FaceLandmark{4, math_utils::Point3D(cx + 10, cy + 15, 0.0)});
    
    FaceBoundingBox border_bbox(10, 10, 90, 90);
    border_bbox.score = 0.8f;
    
    Face border_face(std::move(border_landmarks), border_bbox);
    
    pfld_detector_->detect(test_image, border_face);
    
    // Should have landmarks
    EXPECT_EQ(border_face.getLandmarks().size(), 106);
}

// Test missing landmarks (less than 5 points)
TEST_F(PFLDUnitTest, InsufficientLandmarks) {
    ASSERT_TRUE(pfld_detector_->isReady());
    
    auto test_image = createTestImage();
    
    // Face with only 3 landmarks (insufficient for PFLD)
    std::vector<FaceLandmark> insufficient_landmarks;
    insufficient_landmarks.emplace_back(FaceLandmark{0, math_utils::Point3D(300, 230, 0.0)});
    insufficient_landmarks.emplace_back(FaceLandmark{1, math_utils::Point3D(340, 230, 0.0)});
    insufficient_landmarks.emplace_back(FaceLandmark{2, math_utils::Point3D(320, 250, 0.0)});
    
    FaceBoundingBox bbox(280, 210, 360, 290);
    bbox.score = 0.7f;
    
    Face incomplete_face(std::move(insufficient_landmarks), bbox);
    
    // Should handle gracefully - detectSimilar requires exactly 5 landmarks
    pfld_detector_->detectSimilar(test_image, incomplete_face);
    
    // Face might not be processed, but shouldn't crash
}

// Test landmark accuracy after detection  
TEST_F(PFLDUnitTest, LandmarkAccuracy) {
    ASSERT_TRUE(pfld_detector_->isReady());
    
    auto test_image = createTestImage();
    Face mock_face = createMockFace();
    
    pfld_detector_->detect(test_image, mock_face);
    
    auto landmarks = mock_face.getLandmarks();
    EXPECT_EQ(landmarks.size(), 106);
    
    // Check that landmarks are within image bounds
    for (const auto& landmark : landmarks) {
        EXPECT_GE(landmark.p.x, 0) << "Landmark x coordinate out of bounds";
        EXPECT_LT(landmark.p.x, test_image->info.width) << "Landmark x coordinate out of bounds";
        EXPECT_GE(landmark.p.y, 0) << "Landmark y coordinate out of bounds";
        EXPECT_LT(landmark.p.y, test_image->info.height) << "Landmark y coordinate out of bounds";
    }
}

// Test memory management with repeated calls
TEST_F(PFLDUnitTest, MemoryManagementRepeatedCalls) {
    ASSERT_TRUE(pfld_detector_->isReady());

    
    auto test_image = createTestImage();
    
    for (int i = 0; i < 5; ++i) {
        Face mock_face = createMockFace();
        pfld_detector_->detect(test_image, mock_face);
        
        // Each call should successfully produce 106 landmarks
        EXPECT_EQ(mock_face.getLandmarks().size(), 106);
    }
}

// Test different image sizes
TEST_F(PFLDUnitTest, DifferentImageSizes) {
    ASSERT_TRUE(pfld_detector_->isReady());
    
    std::vector<std::pair<int, int>> sizes = {
        {320, 240}, {640, 480}, {1280, 720}, {1920, 1080}
    };
    
    for (const auto& size : sizes) {
        auto test_image = createTestImage(size.first, size.second);
        Face mock_face = createMockFace(size.first, size.second);
        
        pfld_detector_->detect(test_image, mock_face);
        
        // Should still produce 106 landmarks regardless of image size
        EXPECT_EQ(mock_face.getLandmarks().size(), 106) 
            << "Failed for image size " << size.first << "x" << size.second;
    }
}

// Test performance bounds
TEST_F(PFLDUnitTest, PerformanceBounds) {
    ASSERT_TRUE(pfld_detector_->isReady());
    
    auto test_image = createTestImage();
    Face mock_face = createMockFace();
    
    auto start = std::chrono::high_resolution_clock::now();
    pfld_detector_->detect(test_image, mock_face);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // PFLD detection should complete within reasonable time
    EXPECT_LT(duration.count(), 10000) << "PFLD detection took too long: " << duration.count() << "ms";
}
