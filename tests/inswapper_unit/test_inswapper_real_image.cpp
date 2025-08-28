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
#include "LinuxFace/imageLoader.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/onnx/inswapper.h"
#include "config.hpp"

using namespace linuxface;

class InSwapperRealImageTest : public ::testing::Test
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
        inswapper_ = std::make_unique<InSwapper>(models_folder + "inswapper_128.onnx");
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

    std::vector<float> createTestEmbedding()
    {
        // Create a mock 512-dimensional embedding (typical for face recognition)
        std::vector<float> embedding(512);
        for (size_t i = 0; i < embedding.size(); ++i)
        {
            embedding[i] = static_cast<float>(i % 100) / 100.0f - 0.5f; // Values between -0.5 and 0.5
        }
        return embedding;
    }

    std::vector<math_utils::Point<>> createTestLandmarks(int imageWidth = 640, int imageHeight = 480)
    {
        // Create 5-point face landmarks in ArcFace order for a face in the center
        std::vector<math_utils::Point<>> landmarks(5);

        // Standard face landmark positions (left eye, right eye, nose, left mouth, right mouth)
        // Position them in the center of the image
        int centerX = imageWidth / 2;
        int centerY = imageHeight / 2;
        int eyeDistance = imageWidth / 8; // Distance between eyes

        landmarks[0] = math_utils::Point<>(centerX - eyeDistance / 2, centerY - imageHeight / 12); // Left eye
        landmarks[1] = math_utils::Point<>(centerX + eyeDistance / 2, centerY - imageHeight / 12); // Right eye
        landmarks[2] = math_utils::Point<>(centerX, centerY);                                      // Nose
        landmarks[3] = math_utils::Point<>(centerX - eyeDistance / 3, centerY + imageHeight / 8);  // Left mouth
        landmarks[4] = math_utils::Point<>(centerX + eyeDistance / 3, centerY + imageHeight / 8);  // Right mouth

        return landmarks;
    }

    std::unique_ptr<InSwapper> inswapper_;
};

// Test constructor and initialization
TEST_F(InSwapperRealImageTest, ConstructorValidModel)
{
    EXPECT_TRUE(inswapper_->isReady());
}

// Test loading real image from file
TEST_F(InSwapperRealImageTest, LoadRealImageFromFile)
{
    ASSERT_TRUE(inswapper_->isReady());

    // Try to load the real image.jpeg file
    auto realImage = loadRealImage("../tests/common/image.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image from ../tests/common/image.jpeg";

    // Verify image properties
    EXPECT_GT(realImage->info.width, 0);
    EXPECT_GT(realImage->info.height, 0);
    EXPECT_GT(realImage->size(), 0);
    EXPECT_TRUE(realImage->info.format == ImageFormat::RGB || realImage->info.format == ImageFormat::JPEG
                || realImage->info.format == ImageFormat::PNG || realImage->info.format == ImageFormat::BMP
                || realImage->info.format == ImageFormat::PPM);

    std::cout << "Loaded real image: " << realImage->info.width << "x" << realImage->info.height
              << ", format: " << static_cast<int>(realImage->info.format) << ", size: " << realImage->size() << " bytes"
              << std::endl;
}

// Test basic swap operation with real image
TEST_F(InSwapperRealImageTest, BasicSwapWithRealImage)
{
    ASSERT_TRUE(inswapper_->isReady());

    // Try to load the real image.jpeg file
    auto realImage = loadRealImage("../tests/common/image.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    auto testEmbedding = createTestEmbedding();
    auto testLandmarks = createTestLandmarks(realImage->info.width, realImage->info.height);

    Image outputImage;
    const bool swapResult = inswapper_->swap(testEmbedding, testLandmarks, *realImage, outputImage);

    EXPECT_TRUE(swapResult);
    if (swapResult)
    {
        EXPECT_EQ(outputImage.info.width, 128);
        EXPECT_EQ(outputImage.info.height, 128);
        EXPECT_EQ(outputImage.info.format, ImageFormat::RGB);
        std::cout << "Successfully swapped with real image: " << realImage->info.width << "x" << realImage->info.height
                  << std::endl;
    }
}

// Test swap with different landmark positions
TEST_F(InSwapperRealImageTest, SwapWithDifferentLandmarks)
{
    ASSERT_TRUE(inswapper_->isReady());

    // Load real image
    auto realImage = loadRealImage("../tests/common/image.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    auto testEmbedding = createTestEmbedding();

    // Test with landmarks at different positions
    std::vector<std::pair<int, int>> landmarkOffsets = {
        {0,   0  }, // Center
        {50,  30 }, // Offset from center
        {-30, -20}, // Negative offset
        {80,  60 }  // Large offset
    };

    for (const auto& offset : landmarkOffsets)
    {
        auto testLandmarks = createTestLandmarks(realImage->info.width, realImage->info.height);

        // Apply offset to all landmarks
        for (auto& landmark : testLandmarks)
        {
            landmark.x += offset.first;
            landmark.y += offset.second;

            // Ensure landmarks stay within image bounds
            landmark.x = std::max(static_cast<long int>(0),
                                  std::min(static_cast<long int>(realImage->info.width - 1), landmark.x));
            landmark.y = std::max(static_cast<long int>(0),
                                  std::min(static_cast<long int>(realImage->info.height - 1), landmark.y));
        }

        Image outputImage;
        const bool swapResult = inswapper_->swap(testEmbedding, testLandmarks, *realImage, outputImage);

        EXPECT_TRUE(swapResult) << "Failed swap with landmark offset: (" << offset.first << ", " << offset.second
                                << ")";
        if (swapResult)
        {
            EXPECT_EQ(outputImage.info.width, 128);
            EXPECT_EQ(outputImage.info.height, 128);
        }
    }
}

// Test performance with real image
TEST_F(InSwapperRealImageTest, PerformanceWithRealImage)
{
    ASSERT_TRUE(inswapper_->isReady());

    // Load real image
    auto realImage = loadRealImage("../tests/common/image.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    auto testEmbedding = createTestEmbedding();
    auto testLandmarks = createTestLandmarks(realImage->info.width, realImage->info.height);

    // Measure performance
    auto start = std::chrono::high_resolution_clock::now();

    Image outputImage;
    const bool swapResult = inswapper_->swap(testEmbedding, testLandmarks, *realImage, outputImage);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_TRUE(swapResult);
    EXPECT_LT(duration.count(), 3000) << "Swap took too long: " << duration.count() << "ms";

    std::cout << "Real image swap performance: " << duration.count() << "ms" << std::endl;
}

// Test multiple operations with real image
TEST_F(InSwapperRealImageTest, MultipleOperationsWithRealImage)
{
    ASSERT_TRUE(inswapper_->isReady());

    // Load real image
    auto realImage = loadRealImage("../tests/common/image.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    auto testEmbedding = createTestEmbedding();
    auto testLandmarks = createTestLandmarks(realImage->info.width, realImage->info.height);

    const int numOperations = 3;
    std::vector<Image> outputImages(numOperations);

    for (int i = 0; i < numOperations; ++i)
    {
        const bool swapResult = inswapper_->swap(testEmbedding, testLandmarks, *realImage, outputImages[i]);
        EXPECT_TRUE(swapResult) << "Failed on operation " << i;

        if (swapResult)
        {
            EXPECT_EQ(outputImages[i].info.width, 128);
            EXPECT_EQ(outputImages[i].info.height, 128);
            EXPECT_EQ(outputImages[i].info.format, ImageFormat::RGB);
        }
    }

    std::cout << "Successfully performed " << numOperations << " swap operations with real image" << std::endl;
}

// Test error handling with invalid image paths
TEST_F(InSwapperRealImageTest, InvalidImagePath)
{
    ASSERT_TRUE(inswapper_->isReady());

    // Try to load non-existent image
    auto invalidImage = loadRealImage("../tests/common/nonexistent_image.png");
    EXPECT_TRUE(invalidImage == nullptr) << "Should return nullptr for invalid image path";

    // Try to load with empty path
    auto emptyPathImage = loadRealImage("");
    EXPECT_TRUE(emptyPathImage == nullptr) << "Should return nullptr for empty path";
}
