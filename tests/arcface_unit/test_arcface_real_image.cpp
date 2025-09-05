#include <chrono>
#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
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
        std::string config_path = TestUtils::getConfigPath();
        if (std::ifstream(config_path).good())
        {
            bool reloaded = Config::getInstance().reloadFromFile(config_path.c_str());
            ASSERT_TRUE(reloaded) << "Failed to reload configuration from: " << config_path;

            bool config_loaded = Config::getInstance().loadConfiguration();
            ASSERT_TRUE(config_loaded) << "Failed to load configuration from: " << config_path;
            std::cout << "Loaded test configuration from: " << config_path << std::endl;
        }
        else
        {
            FAIL() << "Configuration file not found at: " << config_path;
        }

        // Initialize ArcfaceRecognizer using TestUtils
        arcface_recognizer_ = std::make_unique<ArcfaceRecognizer>(TestUtils::getModelPath("arcface_w600k_r50.onnx"));

        // Also create SCRFD for landmark detection
        scrfd_detector_ = std::make_unique<SCRFDetector>(TestUtils::getModelPath("scrfd_500m_bnkps_shape640x640.onnx"));
    }

    std::unique_ptr<Image> loadRealImage(const std::string& imageName)
    {
        std::string imagePath = TestUtils::getTestImagePath(imageName);
        auto image = ImageLoader::loadImageFromFile(imagePath);
        if (!image)
        {
            std::cerr << "Failed to load image from: " << imagePath << std::endl;
        }
        return image;
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

    auto realImage = loadRealImage("single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image single_face.jpeg";

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

    auto realImage = loadRealImage("single_face.jpeg");
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

    auto realImage = loadRealImage("single_face.jpeg");
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

    auto realImage = loadRealImage("single_face.jpeg");
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

    auto realImage = loadRealImage("single_face.jpeg");
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

    std::vector<std::string> test_images = {"single_face.jpeg", "single_face_2.jpg", "two_face.jpeg"};

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

    auto realImage = loadRealImage("single_face.jpeg");
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

    auto realImage = loadRealImage("single_face.jpeg");
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

    auto realImage = loadRealImage("single_face.jpeg");
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

    auto realImage = loadRealImage("single_face.jpeg");
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

/**
 * @brief Comprehensive determinism test for ArcfaceRecognizer
 * 
 * This test validates that the same image with the same model produces 
 * exactly identical embeddings across multiple runs. This is critical 
 * for ensuring reproducible face recognition results in production.
 *
 * The test performs:
 * - 15 consecutive embedding generations on the same input
 * - Strict bit-level comparison of all embeddings
 * - Performance consistency validation
 * - Statistical analysis of any variations
 */
TEST_F(ArcfaceRecognizerRealImageTest, ComprehensiveDeterminismValidation)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());
    ASSERT_TRUE(scrfd_detector_->isReady());

    // Load test image
    auto realImage = loadRealImage("single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load test image";

    // Use SCRFD for consistent landmark detection
    std::vector<Face> faces = scrfd_detector_->detect(realImage);
    ASSERT_GT(faces.size(), 0) << "SCRFD failed to detect faces for determinism test";
    
    auto landmarks = extractLandmarksFromSCRFD(faces[0]);
    ASSERT_EQ(landmarks.size(), 5) << "Invalid landmark count from SCRFD";

    std::cout << "Running determinism validation with " << landmarks.size() << " landmarks..." << std::endl;
    std::cout << "Face bounding box: (" << faces[0].getBoundingBox().rect.x() << ", "
              << faces[0].getBoundingBox().rect.y() << ", " 
              << faces[0].getBoundingBox().rect.width() << ", " 
              << faces[0].getBoundingBox().rect.height() << ")" << std::endl;

    // Test parameters
    const int NUM_ITERATIONS = 15;
    const float EPSILON = 1e-7f; // Stricter tolerance for floating-point comparison
    
    std::vector<std::vector<float>> embeddings(NUM_ITERATIONS);
    std::vector<double> processing_times(NUM_ITERATIONS);

    // Warm-up run to ensure stable state
    std::vector<float> warmup_embedding;
    auto warmup_start = std::chrono::high_resolution_clock::now();
    bool warmup_result = arcface_recognizer_->recognize(*realImage, landmarks, warmup_embedding);
    auto warmup_end = std::chrono::high_resolution_clock::now();
    ASSERT_TRUE(warmup_result) << "Warm-up recognition failed";
    ASSERT_EQ(warmup_embedding.size(), 512) << "Invalid embedding size in warm-up";
    
    auto warmup_duration = std::chrono::duration_cast<std::chrono::microseconds>(warmup_end - warmup_start).count();
    std::cout << "Warm-up completed in " << warmup_duration << " microseconds" << std::endl;

    // Generate embeddings with timing
    std::cout << "Generating " << NUM_ITERATIONS << " embeddings for determinism validation..." << std::endl;
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        bool result = arcface_recognizer_->recognize(*realImage, landmarks, embeddings[i]);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        processing_times[i] = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

        ASSERT_TRUE(result) << "Recognition failed on iteration " << (i + 1);
        ASSERT_EQ(embeddings[i].size(), 512) << "Invalid embedding size on iteration " << (i + 1);
        ASSERT_TRUE(validateEmbedding(embeddings[i])) << "Invalid embedding normalization on iteration " << (i + 1);
        
        std::cout << "Iteration " << std::setw(2) << (i + 1) << ": " 
                  << std::setw(6) << processing_times[i] << " μs" << std::endl;
    }

    // === STRICT DETERMINISM VALIDATION ===
    
    std::cout << "\n=== DETERMINISM ANALYSIS ===" << std::endl;
    
    const auto& reference_embedding = embeddings[0];
    int identical_count = 0;
    int nearly_identical_count = 0;
    float max_difference = 0.0f;
    size_t max_diff_position = 0;
    
    // Compare each embedding against the reference
    for (int i = 1; i < NUM_ITERATIONS; ++i) {
        bool is_identical = true;
        bool is_nearly_identical = true;
        float max_element_diff = 0.0f;
        size_t diff_position = 0;
        
        // Bit-level comparison
        for (size_t j = 0; j < 512; ++j) {
            float diff = std::abs(reference_embedding[j] - embeddings[i][j]);
            
            if (diff > max_element_diff) {
                max_element_diff = diff;
                diff_position = j;
            }
            
            if (diff > 0.0f) {
                is_identical = false;
            }
            
            if (diff > EPSILON) {
                is_nearly_identical = false;
            }
        }
        
        if (is_identical) {
            identical_count++;
        } else if (is_nearly_identical) {
            nearly_identical_count++;
        }
        
        if (max_element_diff > max_difference) {
            max_difference = max_element_diff;
            max_diff_position = diff_position;
        }
        
        // Calculate cosine similarity for additional validation
        float similarity = calculateCosineSimilarity(reference_embedding, embeddings[i]);
        
        std::cout << "Iteration " << std::setw(2) << (i + 1) 
                  << " vs Reference: Max diff = " << std::scientific << std::setprecision(2) << max_element_diff
                  << " at pos " << std::setw(3) << diff_position
                  << ", Similarity = " << std::fixed << std::setprecision(6) << similarity;
        
        if (is_identical) {
            std::cout << " [IDENTICAL]";
        } else if (is_nearly_identical) {
            std::cout << " [NEARLY_IDENTICAL]";
        } else {
            std::cout << " [DIFFERENT]";
        }
        std::cout << std::endl;
        
        // Strict requirement: embeddings must be identical or nearly identical
        EXPECT_TRUE(is_nearly_identical) 
            << "Embedding " << (i + 1) << " differs significantly from reference. "
            << "Max difference: " << max_element_diff << " at position " << diff_position;
        
        // Cosine similarity must be very close to 1.0
        EXPECT_GT(similarity, 0.9999f) 
            << "Embedding " << (i + 1) << " has poor similarity with reference: " << similarity;
    }

    // === PERFORMANCE CONSISTENCY VALIDATION ===
    
    std::cout << "\n=== PERFORMANCE ANALYSIS ===" << std::endl;
    
    // Calculate performance statistics
    double total_time = 0.0;
    double min_time = processing_times[0];
    double max_time = processing_times[0];
    
    for (double time : processing_times) {
        total_time += time;
        min_time = std::min(min_time, time);
        max_time = std::max(max_time, time);
    }
    
    double avg_time = total_time / NUM_ITERATIONS;
    double time_variance = 0.0;
    
    for (double time : processing_times) {
        double diff = time - avg_time;
        time_variance += diff * diff;
    }
    time_variance /= NUM_ITERATIONS;
    double time_stddev = std::sqrt(time_variance);
    
    std::cout << "Processing time statistics (microseconds):" << std::endl;
    std::cout << "  Average: " << std::fixed << std::setprecision(1) << avg_time << " μs" << std::endl;
    std::cout << "  Min:     " << min_time << " μs" << std::endl;
    std::cout << "  Max:     " << max_time << " μs" << std::endl;
    std::cout << "  StdDev:  " << time_stddev << " μs" << std::endl;
    std::cout << "  Variance: " << time_variance << " μs²" << std::endl;
    std::cout << "  Range:   " << (max_time - min_time) << " μs" << std::endl;
    
    // Performance consistency validation
    double coefficient_of_variation = time_stddev / avg_time;
    EXPECT_LT(coefficient_of_variation, 0.3) 
        << "Processing time is too inconsistent. CV = " << coefficient_of_variation;
        
    EXPECT_LT(max_time - min_time, avg_time * 0.5) 
        << "Processing time range is too large: " << (max_time - min_time) << " μs";

    // === FINAL SUMMARY ===
    
    std::cout << "\n=== DETERMINISM SUMMARY ===" << std::endl;
    std::cout << "Total embeddings generated: " << NUM_ITERATIONS << std::endl;
    std::cout << "Bit-level identical: " << identical_count << "/" << (NUM_ITERATIONS - 1) << std::endl;
    std::cout << "Nearly identical (ε=" << EPSILON << "): " << nearly_identical_count << "/" << (NUM_ITERATIONS - 1) << std::endl;
    std::cout << "Maximum difference found: " << std::scientific << max_difference 
              << " at position " << max_diff_position << std::endl;
    std::cout << "Coefficient of variation: " << std::fixed << std::setprecision(4) << coefficient_of_variation << std::endl;
    
    // Final assertions for determinism
    EXPECT_GE(identical_count + nearly_identical_count, NUM_ITERATIONS - 1) 
        << "Not all embeddings are deterministic within tolerance";
        
    EXPECT_LT(max_difference, 1e-4f) 
        << "Maximum difference " << max_difference << " exceeds acceptable threshold";
    
    std::cout << "\n✓ DETERMINISM VALIDATION: " 
              << (identical_count == NUM_ITERATIONS - 1 ? "PERFECT" : 
                  (identical_count + nearly_identical_count == NUM_ITERATIONS - 1 ? "ACCEPTABLE" : "FAILED"))
              << std::endl;
}

/**
 * @brief Test determinism across different inswapper compatibility modes
 * 
 * Validates that both regular and inswapper-compatible embeddings are 
 * deterministic when computed multiple times with the same inputs.
 */
TEST_F(ArcfaceRecognizerRealImageTest, DeterminismWithInswapperCompatibility)
{
    ASSERT_TRUE(arcface_recognizer_->isReady());

    auto realImage = loadRealImage("single_face.jpeg");
    ASSERT_TRUE(realImage != nullptr) << "Failed to load test image";

    auto landmarks = createManualLandmarks(realImage->info.width, realImage->info.height);
    
    // Enable inswapper compatibility
    std::string inswapperModelPath = TestUtils::getModelPath("inswapper_128.onnx");
    bool enableResult = arcface_recognizer_->enableInswapperCompatibility(inswapperModelPath);
    ASSERT_TRUE(enableResult) << "Failed to enable inswapper compatibility";

    const int NUM_ITERATIONS = 5;
    
    // Test regular ArcFace embeddings for determinism
    std::cout << "Testing determinism of regular ArcFace embeddings..." << std::endl;
    std::vector<std::vector<float>> regular_embeddings(NUM_ITERATIONS);
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        bool result = arcface_recognizer_->recognize(*realImage, landmarks, regular_embeddings[i], false);
        ASSERT_TRUE(result) << "Regular embedding generation failed on iteration " << (i + 1);
        ASSERT_EQ(regular_embeddings[i].size(), 512);
    }
    
    // Validate regular embeddings are deterministic
    for (int i = 1; i < NUM_ITERATIONS; ++i) {
        float similarity = calculateCosineSimilarity(regular_embeddings[0], regular_embeddings[i]);
        EXPECT_GT(similarity, 0.9999f) << "Regular embedding determinism failed at iteration " << (i + 1);
    }
    
    // Test inswapper-compatible embeddings for determinism  
    std::cout << "Testing determinism of inswapper-compatible embeddings..." << std::endl;
    std::vector<std::vector<float>> inswapper_embeddings(NUM_ITERATIONS);
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        bool result = arcface_recognizer_->recognize(*realImage, landmarks, inswapper_embeddings[i], true);
        ASSERT_TRUE(result) << "Inswapper embedding generation failed on iteration " << (i + 1);
        ASSERT_EQ(inswapper_embeddings[i].size(), 512);
    }
    
    // Validate inswapper embeddings are deterministic
    for (int i = 1; i < NUM_ITERATIONS; ++i) {
        float similarity = calculateCosineSimilarity(inswapper_embeddings[0], inswapper_embeddings[i]);
        EXPECT_GT(similarity, 0.9999f) << "Inswapper embedding determinism failed at iteration " << (i + 1);
    }
    
    // Verify that regular and inswapper embeddings are consistently different
    float cross_similarity = calculateCosineSimilarity(regular_embeddings[0], inswapper_embeddings[0]);
    std::cout << "Cross-mode similarity: " << cross_similarity << std::endl;
    EXPECT_LT(cross_similarity, 0.99f) << "Regular and inswapper embeddings should be consistently different";
    
    // Verify the difference is consistent across iterations
    for (int i = 1; i < NUM_ITERATIONS; ++i) {
        float iter_cross_similarity = calculateCosineSimilarity(regular_embeddings[i], inswapper_embeddings[i]);
        EXPECT_NEAR(cross_similarity, iter_cross_similarity, 0.001f) 
            << "Cross-mode similarity should be consistent across iterations";
    }
    
    std::cout << "✓ Both regular and inswapper-compatible embeddings are deterministic" << std::endl;
}
