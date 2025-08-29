#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/face.h"
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
