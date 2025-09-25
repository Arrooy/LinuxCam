#include "test_config.h"

#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/onnx/faceSegmentation.h"
#include "config.hpp"

using namespace linuxface;

class FaceSegmentationDetectorTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Load test configuration - following same pattern as SCRFD tests
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

        if (config_loaded)
        {
            // Get the model path using the config
            std::string models_folder = Config::getInstance().getModelFolderPath();
            modelPath = models_folder + "face_parsing_18.onnx";
        }
        else
        {
            // Fallback to relative path if config loading fails
            modelPath = "../models/face_parsing_18.onnx";
        }

        // Create a simple test image (RGB format)
        testImage = std::make_unique<Image>(512 * 512 * 3);
        testImage->info.width = 512;
        testImage->info.height = 512;
        testImage->info.pixelSizeBytes = 3;
        testImage->info.format = ImageFormat::RGB;

        // Fill with simple gradient pattern for testing
        unsigned char* data = testImage->data();
        for (int y = 0; y < 512; ++y)
        {
            for (int x = 0; x < 512; ++x)
            {
                size_t idx = (y * 512 + x) * 3;
                data[idx + 0] = static_cast<unsigned char>(x / 2);       // R
                data[idx + 1] = static_cast<unsigned char>(y / 2);       // G
                data[idx + 2] = static_cast<unsigned char>((x + y) / 4); // B
            }
        }
    }

    std::string modelPath;
    std::unique_ptr<Image> testImage;
};

TEST_F(FaceSegmentationDetectorTest, ConstructorWithValidModel)
{
    // Test that constructor doesn't crash with a valid model path
    ASSERT_NO_THROW({
        FaceSegmentationDetector detector(modelPath);

        // Check basic properties
        EXPECT_EQ(detector.getModelPath(), modelPath);
        EXPECT_TRUE(detector.isReady()); // May fail if model doesn't exist
    });
}

TEST_F(FaceSegmentationDetectorTest, ConstructorWithInvalidModel)
{
    // Test graceful handling of invalid model path
    std::string invalidPath = "/path/to/nonexistent/model.onnx";

    ASSERT_NO_THROW({
        FaceSegmentationDetector detector(invalidPath);

        // Should not be ready with invalid model
        EXPECT_FALSE(detector.isReady());
        EXPECT_EQ(detector.getModelPath(), invalidPath);
    });
}

TEST_F(FaceSegmentationDetectorTest, TransformValidImage)
{
    FaceSegmentationDetector detector(modelPath);

    // Skip this test if model is not ready (e.g., file doesn't exist)
    if (!detector.isReady())
    {
        GTEST_SKIP() << "Model not ready, skipping transform test";
    }

    ASSERT_NO_THROW({
        Ort::Value tensor = detector.transform(testImage);

        // Verify tensor properties
        EXPECT_TRUE(tensor.IsTensor());

        auto shape = tensor.GetTensorTypeAndShapeInfo().GetShape();
        ASSERT_EQ(shape.size(), 4); // Should be [batch, channels, height, width]
        EXPECT_EQ(shape[0], 1);     // Batch size
        EXPECT_EQ(shape[1], 3);     // RGB channels
        EXPECT_EQ(shape[2], 512);   // Height
        EXPECT_EQ(shape[3], 512);   // Width
    });
}

TEST_F(FaceSegmentationDetectorTest, TransformEmptyImage)
{
    FaceSegmentationDetector detector(modelPath);

    // Skip this test if model is not ready
    if (!detector.isReady())
    {
        GTEST_SKIP() << "Model not ready, skipping empty image test";
    }

    // Create empty image
    auto emptyImage = std::make_unique<Image>();

    // Transform should handle empty image gracefully
    ASSERT_NO_THROW({ Ort::Value tensor = detector.transform(emptyImage); });
}

TEST_F(FaceSegmentationDetectorTest, DetectWithValidImage)
{
    FaceSegmentationDetector detector(modelPath);

    // Skip this test if model is not ready
    if (!detector.isReady())
    {
        GTEST_SKIP() << "Model not ready, skipping detection test";
    }

    std::unique_ptr<Image> labelMask;

    ASSERT_NO_THROW({ detector.detect(testImage, labelMask); });

    // Verify output mask is created
    EXPECT_NE(labelMask, nullptr);
    if (labelMask)
    {
        EXPECT_EQ(labelMask->info.format, ImageFormat::GRAYSCALE);
        EXPECT_EQ(labelMask->info.pixelSizeBytes, 1);
        EXPECT_EQ(labelMask->info.width, testImage->info.width);
        EXPECT_EQ(labelMask->info.height, testImage->info.height);
        EXPECT_FALSE(labelMask->empty());

        // Verify that mask contains valid class labels (0-18)
        const unsigned char* maskData = labelMask->data();
        for (size_t i = 0; i < labelMask->info.width * labelMask->info.height; ++i)
        {
            EXPECT_LT(maskData[i], static_cast<unsigned char>(FaceSegmentationClass::NUM_CLASSES)) 
                << "Invalid class label found at pixel " << i;
        }
    }
}

TEST_F(FaceSegmentationDetectorTest, DetectWithEmptyImage)
{
    FaceSegmentationDetector detector(modelPath);

    // Skip this test if model is not ready
    if (!detector.isReady())
    {
        GTEST_SKIP() << "Model not ready, skipping empty image detection test";
    }

    auto emptyImage = std::make_unique<Image>();
    std::unique_ptr<Image> labelMask;

    // Should handle empty image gracefully without crashing
    ASSERT_NO_THROW({ detector.detect(emptyImage, labelMask); });

    // Output should remain null or empty for empty input
    EXPECT_TRUE(!labelMask || labelMask->empty());
}

// Test argmax function behavior (if it were accessible)
TEST_F(FaceSegmentationDetectorTest, BasicFunctionality)
{
    FaceSegmentationDetector detector(modelPath);

    // Test basic properties regardless of model readiness
    EXPECT_EQ(detector.getModelPath(), modelPath);

    // Test that constructor doesn't throw
    ASSERT_NO_THROW({ FaceSegmentationDetector detector2(modelPath); });
}
