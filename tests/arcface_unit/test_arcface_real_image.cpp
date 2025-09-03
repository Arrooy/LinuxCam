#include <chrono>
#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <string>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/imageLoader.h"
#include "LinuxFace/onnx/arcfaceRecognizer.h"
#include "LinuxFace/onnx/scrfd.h"
#include "../common/test_utils.h"
#include "config.hpp"

using namespace linuxface;

class ArcfaceRecognizerRealImageTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Load test configuration
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
        arcface_recognizer_ = std::make_unique<ArcfaceRecognizer>(models_folder + "arcface_w600k_r50.onnx");

        // Also create SCRFD for landmark detection
        scrfd_detector_ = std::make_unique<SCRFDetector>(models_folder + "scrfd_500m_bnkps_shape640x640.onnx");
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

    std::vector<math_utils::Point<>> createManualLandmarks(int imageWidth, int imageHeight)
    {
        // Create manually defined 5-point face landmarks for testing
        // These should be reasonable positions for a face in the image center
        std::vector<math_utils::Point<>> landmarks(5);

        int centerX = imageWidth / 2;
        int centerY = imageHeight / 2;
        int eyeDistance = imageWidth / 8;

        // ArcFace order: [left eye, right eye, nose, left mouth, right mouth]
        landmarks[0] = math_utils::Point<>(centerX - eyeDistance / 2, centerY - imageHeight / 12); // Left eye
        landmarks[1] = math_utils::Point<>(centerX + eyeDistance / 2, centerY - imageHeight / 12); // Right eye
        landmarks[2] = math_utils::Point<>(centerX, centerY);                                      // Nose
        landmarks[3] = math_utils::Point<>(centerX - eyeDistance / 3, centerY + imageHeight / 8);  // Left mouth
        landmarks[4] = math_utils::Point<>(centerX + eyeDistance / 3, centerY + imageHeight / 8);  // Right mouth

        return landmarks;
    }

    // Extract landmarks from SCRFD detection
    std::vector<math_utils::Point<>> extractLandmarksFromSCRFD(const Face& face)
    {
        std::vector<math_utils::Point<>> landmarks(5);

        // Get landmarks from SCRFD in ArcFace order
        auto faceLandmarks = face.getFivePointLandmarksArcFaceOrder2D();

        for (size_t i = 0; i < 5 && i < faceLandmarks.size(); ++i)
        {
            landmarks[i] = faceLandmarks[i];
        }

        return landmarks;
    }

    bool validateEmbedding(const std::vector<float>& embedding)
    {
        if (embedding.size() != 512)
        {
            return false;
        }

        // Check if embedding is normalized (L2 norm should be ~1.0)
        float norm = 0.0f;
        for (float val : embedding)
        {
            norm += val * val;
        }
        norm = std::sqrt(norm);

        // Allow some tolerance for floating point precision
        return (norm > 0.99f && norm < 1.01f);
    }

    float calculateCosineSimilarity(const std::vector<float>& embed1, const std::vector<float>& embed2)
    {
        if (embed1.size() != embed2.size() || embed1.size() != 512)
        {
            return -1.0f; // Invalid
        }

        float dot_product = 0.0f;
        for (size_t i = 0; i < embed1.size(); ++i)
        {
            dot_product += embed1[i] * embed2[i];
        }

        return dot_product; // Already normalized embeddings
    }

    float calculateL2Norm(const std::vector<float>& embedding)
    {
        float norm = 0.0f;
        for (const float& val : embedding)
        {
            norm += val * val;
        }
        return std::sqrt(norm);
    }

    std::unique_ptr<ArcfaceRecognizer> arcface_recognizer_;
    std::unique_ptr<SCRFDetector> scrfd_detector_;
};

// Test constructor and initialization
TEST_F(ArcfaceRecognizerRealImageTest, ConstructorValidModel)
{
    EXPECT_TRUE(arcface_recognizer_->isReady());
}

// Test loading real image
TEST_F(ArcfaceRecognizerRealImageTest, LoadRealImageFromFile)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image from ../tests/common/single_face.jpeg";

    // Verify image properties
    EXPECT_GT(realImage->info.width, 0);
    EXPECT_GT(realImage->info.height, 0);
    EXPECT_GT(realImage->size(), 0);
    EXPECT_EQ(realImage->info.format, ImageFormat::RGB);

    std::cout << "Loaded real image for ArcFace: " << realImage->info.width << "x" << realImage->info.height
              << ", format: " << static_cast<int>(realImage->info.format) << ", size: " << realImage->size() << " bytes"
              << std::endl;
}

// Test basic recognition with manual landmarks
TEST_F(ArcfaceRecognizerRealImageTest, BasicRecognitionWithManualLandmarks)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    auto landmarks = createManualLandmarks(realImage->info.width, realImage->info.height);
    std::vector<float> embedding;

    const bool recognizeResult = arcface_recognizer_->recognize(*realImage, landmarks, embedding);

    EXPECT_TRUE(recognizeResult);
    if (recognizeResult)
    {
        EXPECT_EQ(embedding.size(), 512);
        EXPECT_TRUE(validateEmbedding(embedding));

        std::cout << "Successfully generated embedding with manual landmarks" << std::endl;
        std::cout << "Embedding norm validation: " << (validateEmbedding(embedding) ? "PASSED" : "FAILED") << std::endl;
    }
}

// Test recognition with SCRFD-detected landmarks
TEST_F(ArcfaceRecognizerRealImageTest, RecognitionWithSCRFDLandmarks)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());
    ASSERT_TRUE(scrfd_detector_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    // Detect faces with SCRFD
    std::vector<Face> faces = scrfd_detector_->detect(realImage);
    ASSERT_GT(faces.size(), 0) << "SCRFD failed to detect any faces";

    // Use the first detected face
    const Face& detectedFace = faces[0];
    auto landmarks = extractLandmarksFromSCRFD(detectedFace);

    std::vector<float> embedding;
    const bool recognizeResult = arcface_recognizer_->recognize(*realImage, landmarks, embedding);

    EXPECT_TRUE(recognizeResult);
    if (recognizeResult)
    {
        EXPECT_EQ(embedding.size(), 512);
        EXPECT_TRUE(validateEmbedding(embedding));

        std::cout << "Successfully generated embedding with SCRFD landmarks" << std::endl;
        std::cout << "Face bounding box: (" << detectedFace.getBoundingBox().rect.x() << ", "
                  << detectedFace.getBoundingBox().rect.y() << ", " << detectedFace.getBoundingBox().rect.width()
                  << ", " << detectedFace.getBoundingBox().rect.height() << ")" << std::endl;
    }
}

// Test multiple recognitions for consistency
TEST_F(ArcfaceRecognizerRealImageTest, ConsistencyAcrossMultipleRecognitions)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    auto landmarks = createManualLandmarks(realImage->info.width, realImage->info.height);

    const int numRecognitions = 3;
    std::vector<std::vector<float>> embeddings(numRecognitions);

    // Generate multiple embeddings
    for (int i = 0; i < numRecognitions; ++i)
    {
        const bool recognizeResult = arcface_recognizer_->recognize(*realImage, landmarks, embeddings[i]);
        EXPECT_TRUE(recognizeResult) << "Recognition failed on iteration " << i;
        EXPECT_EQ(embeddings[i].size(), 512);
        EXPECT_TRUE(validateEmbedding(embeddings[i]));
    }

    // Compare embeddings for consistency
    for (int i = 1; i < numRecognitions; ++i)
    {
        float similarity = calculateCosineSimilarity(embeddings[0], embeddings[i]);
        EXPECT_GT(similarity, 0.99f) << "Embeddings should be highly consistent, got similarity: " << similarity;
    }

    std::cout << "Consistency test passed - all " << numRecognitions << " recognitions were consistent" << std::endl;
}

// Test performance benchmark
TEST_F(ArcfaceRecognizerRealImageTest, PerformanceBenchmark)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    auto landmarks = createManualLandmarks(realImage->info.width, realImage->info.height);

    // Warm-up run
    std::vector<float> warmup_embedding;
    arcface_recognizer_->recognize(*realImage, landmarks, warmup_embedding);

    // Benchmark
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<float> embedding;
    const bool recognizeResult = arcface_recognizer_->recognize(*realImage, landmarks, embedding);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_TRUE(recognizeResult);
    EXPECT_LT(duration.count(), 1000) << "Recognition took too long: " << duration.count() << "ms";

    std::cout << "ArcFace recognition performance: " << duration.count() << "ms" << std::endl;
}

// Test with different image types
TEST_F(ArcfaceRecognizerRealImageTest, RecognitionWithDifferentImages)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());

    std::vector<std::string> test_images = {"../tests/common/single_face.jpeg", "../tests/common/single_face_2.jpg",
                                            "../tests/common/two_face.jpeg"};

    std::vector<std::vector<float>> embeddings;

    for (const auto& image_path : test_images)
    {
        auto realImage = loadRealImage(image_path);
        if (realImage == nullptr)
        {
            std::cout << "Skipping " << image_path << " - not found" << std::endl;
            continue;
        }

        auto landmarks = createManualLandmarks(realImage->info.width, realImage->info.height);
        std::vector<float> embedding;

        const bool recognizeResult = arcface_recognizer_->recognize(*realImage, landmarks, embedding);

        if (recognizeResult)
        {
            EXPECT_EQ(embedding.size(), 512);
            EXPECT_TRUE(validateEmbedding(embedding));
            embeddings.push_back(embedding);

            std::cout << "Successfully processed: " << image_path << std::endl;
        }
        else
        {
            std::cout << "Failed to process: " << image_path << std::endl;
        }
    }

    // If we have multiple embeddings, check that they're different
    // (since they're different faces)
    if (embeddings.size() >= 2)
    {
        float similarity = calculateCosineSimilarity(embeddings[0], embeddings[1]);
        EXPECT_LT(similarity, 0.8f) << "Different faces should have dissimilar embeddings, got similarity: "
                                    << similarity;

        std::cout << "Cross-face similarity: " << similarity << " (should be < 0.8)" << std::endl;
    }
}

// Test invalid input handling
TEST_F(ArcfaceRecognizerRealImageTest, InvalidInputHandling)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    // Test with empty landmarks
    std::vector<math_utils::Point<>> empty_landmarks;
    std::vector<float> embedding;

    // This should handle gracefully (may succeed with fallback or fail gracefully)
    bool result = arcface_recognizer_->recognize(*realImage, empty_landmarks, embedding);

    if (!result)
    {
        std::cout << "Empty landmarks correctly rejected" << std::endl;
    }
    else
    {
        std::cout << "Empty landmarks handled with fallback" << std::endl;
        EXPECT_EQ(embedding.size(), 512);
    }

    // Test with invalid landmarks (outside image bounds)
    std::vector<math_utils::Point<>> invalid_landmarks(5);
    for (auto& landmark : invalid_landmarks)
    {
        landmark.x = -100;
        landmark.y = -100;
    }

    embedding.clear();
    result = arcface_recognizer_->recognize(*realImage, invalid_landmarks, embedding);

    if (result)
    {
        EXPECT_EQ(embedding.size(), 512);
        std::cout << "Invalid landmarks handled with fallback processing" << std::endl;
    }
    else
    {
        std::cout << "Invalid landmarks correctly rejected" << std::endl;
    }
}

// Test embedding similarity with same face
TEST_F(ArcfaceRecognizerRealImageTest, SameFaceSimilarity)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());
    ASSERT_TRUE(scrfd_detector_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    // Generate embedding with manual landmarks
    auto manual_landmarks = createManualLandmarks(realImage->info.width, realImage->info.height);
    std::vector<float> manual_embedding;
    bool manual_result = arcface_recognizer_->recognize(*realImage, manual_landmarks, manual_embedding);
    ASSERT_TRUE(manual_result);

    // Generate embedding with SCRFD landmarks
    std::vector<Face> faces = scrfd_detector_->detect(realImage);
    if (faces.size() > 0)
    {
        auto scrfd_landmarks = extractLandmarksFromSCRFD(faces[0]);
        std::vector<float> scrfd_embedding;
        bool scrfd_result = arcface_recognizer_->recognize(*realImage, scrfd_landmarks, scrfd_embedding);

        if (scrfd_result)
        {
            float similarity = calculateCosineSimilarity(manual_embedding, scrfd_embedding);

            // Same face with different landmark detection should still be similar
            EXPECT_GT(similarity, 0.5f) << "Same face with different landmarks should be similar, got: " << similarity;

            std::cout << "Same face similarity (manual vs SCRFD landmarks): " << similarity << std::endl;
        }
    }
}

// Test inswapper compatibility feature
TEST_F(ArcfaceRecognizerRealImageTest, InswapperCompatibilityBasic)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());

    // Test that inswapper compatibility is initially disabled
    EXPECT_FALSE(arcface_recognizer_->isInswapperCompatibilityEnabled());

    // Enable inswapper compatibility
    std::string inswapperModelPath = TestUtils::getModelPath("inswapper_128.onnx");
    bool enableResult = arcface_recognizer_->enableInswapperCompatibility(inswapperModelPath);
    
    EXPECT_TRUE(enableResult) << "Failed to enable inswapper compatibility";
    EXPECT_TRUE(arcface_recognizer_->isInswapperCompatibilityEnabled());
}

// Test embedding transformation with inswapper compatibility
TEST_F(ArcfaceRecognizerRealImageTest, InswapperCompatibilityEmbeddingTransformation)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    auto landmarks = createManualLandmarks(realImage->info.width, realImage->info.height);

    // Get regular ArcFace embedding
    std::vector<float> regularEmbedding;
    bool regularResult = arcface_recognizer_->recognize(*realImage, landmarks, regularEmbedding);
    ASSERT_TRUE(regularResult);
    ASSERT_EQ(regularEmbedding.size(), 512);

    // Enable inswapper compatibility
    std::string inswapperModelPath = TestUtils::getModelPath("inswapper_128.onnx");
    bool enableResult = arcface_recognizer_->enableInswapperCompatibility(inswapperModelPath);
    ASSERT_TRUE(enableResult) << "Failed to enable inswapper compatibility";

    // Get inswapper-compatible embedding
    std::vector<float> inswapperEmbedding;
    bool inswapperResult = arcface_recognizer_->recognize(*realImage, landmarks, inswapperEmbedding, true);
    ASSERT_TRUE(inswapperResult);
    ASSERT_EQ(inswapperEmbedding.size(), 512);

    // Verify embeddings are different (transformation was applied)
    float similarity = calculateCosineSimilarity(regularEmbedding, inswapperEmbedding);
    EXPECT_LT(similarity, 0.99f) << "Inswapper transformation should modify the embedding significantly. Similarity: " << similarity;

    // Verify both embeddings are normalized (L2 norm should be close to 1)
    float regularNorm = calculateL2Norm(regularEmbedding);
    float inswapperNorm = calculateL2Norm(inswapperEmbedding);
    
    EXPECT_NEAR(regularNorm, 1.0f, 0.01f) << "Regular embedding should be normalized. Norm: " << regularNorm;
    EXPECT_NEAR(inswapperNorm, 1.0f, 0.01f) << "Inswapper embedding should be normalized. Norm: " << inswapperNorm;

    std::cout << "Regular embedding norm: " << regularNorm << std::endl;
    std::cout << "Inswapper embedding norm: " << inswapperNorm << std::endl;
    std::cout << "Embedding similarity: " << similarity << std::endl;
}

// Test that default behavior still works (backward compatibility)
TEST_F(ArcfaceRecognizerRealImageTest, InswapperCompatibilityDefaultBehavior)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());

    auto realImage = loadRealImage("../tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image";

    auto landmarks = createManualLandmarks(realImage->info.width, realImage->info.height);

    // Enable inswapper compatibility
    std::string inswapperModelPath = TestUtils::getModelPath("inswapper_128.onnx");
    bool enableResult = arcface_recognizer_->enableInswapperCompatibility(inswapperModelPath);
    ASSERT_TRUE(enableResult);

    // Get embedding with default parameter (should be regular ArcFace)
    std::vector<float> defaultEmbedding;
    bool defaultResult = arcface_recognizer_->recognize(*realImage, landmarks, defaultEmbedding);
    ASSERT_TRUE(defaultResult);

    // Get embedding with explicit false parameter
    std::vector<float> explicitRegularEmbedding;
    bool explicitResult = arcface_recognizer_->recognize(*realImage, landmarks, explicitRegularEmbedding, false);
    ASSERT_TRUE(explicitResult);

    // They should be identical (both regular ArcFace embeddings)
    float similarity = calculateCosineSimilarity(defaultEmbedding, explicitRegularEmbedding);
    EXPECT_NEAR(similarity, 1.0f, 0.001f) << "Default and explicit false should produce identical embeddings";
}
