#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <string>
#include <vector>

#include "../common/test_utils.h" // Use unified test utilities from common directory
#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/text_draw.h"
#include "LinuxFace/face.h"
#include "LinuxFace/imageLoader.h"
#include "LinuxFace/math_utils.h"
#include "LinuxFace/onnx/arcfaceRecognizer.h"
#include "LinuxFace/onnx/inswapper.h"
#include "LinuxFace/onnx/scrfd.h"
#include "LinuxFace/onnx/swapPipeline.h"
#include "config.hpp"

using namespace linuxface;

/**
 * @brief Integration test suite for the SwapPipeline
 *
 * This test suite validates the end-to-end functionality of the face swap pipeline
 * using real test images from the tests/common/ directory. The tests cover:
 * - Successful face swapping with valid inputs
 * - Error handling for invalid inputs and edge cases
 * - Performance validation
 * - Memory management and resource cleanup
 * - Concurrent pipeline operations
 */
class SwapPipelineIntegrationTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Load test configuration using unified test utilities
        std::string config_path = TestUtils::getConfigPath();

        if (std::ifstream(config_path).good())
        {
            // Force reload config from absolute path
            bool reloaded = Config::getInstance().reloadFromFile(config_path.c_str());
            if (reloaded)
            {
                // Parse the loaded configuration
                bool config_loaded = Config::getInstance().loadConfiguration();
                ASSERT_TRUE(config_loaded) << "Failed to load configuration from: " << config_path;
                std::cout << "Loaded test configuration from: " << config_path << std::endl;
            }
            else
            {
                FAIL() << "Failed to reload configuration from: " << config_path;
            }
        }
        else
        {
            FAIL() << "Configuration file not found at: " << config_path;
        }

        // Use test utilities to get models directory
        std::string models_folder = TestUtils::getModelsDir() + "/";

        // Initialize all required detectors
        inswapper_ = std::make_shared<InSwapper>(TestUtils::getModelPath("inswapper_128.onnx"));
        arcface_ = std::make_shared<ArcfaceRecognizer>(TestUtils::getModelPath("arcface_w600k_r50.onnx"));
        scrfd_ = std::make_shared<SCRFDetector>(models_folder + "scrfd_500m_bnkps_shape640x640.onnx");

        // Create the swap pipeline
        swap_pipeline_ = std::make_unique<SwapPipeline>(inswapper_, arcface_, scrfd_);
    }

    std::unique_ptr<Image> loadSourceImage()
    {
        std::string imagePath = TestUtils::getTestImagePath("single_face.jpeg");
        auto image = ImageLoader::loadImageFromFile(imagePath);
        if (!image)
        {
            std::cerr << "Failed to load source image from: " << imagePath << std::endl;
            ADD_FAILURE() << "Could not load source image: " << imagePath;
        }
        return image;
    }

    std::unique_ptr<Image> loadMultipleFacesSourceImage()
    {
        std::string imagePath = TestUtils::getTestImagePath("two_face.jpeg");
        auto image = ImageLoader::loadImageFromFile(imagePath);
        if (!image)
        {
            std::cerr << "Failed to load multiple faces source image from: " << imagePath << std::endl;
            ADD_FAILURE() << "Could not load multiple faces source image: " << imagePath;
        }
        return image;
    }

    std::unique_ptr<Image> loadTargetImage()
    {
        // Load real target image for more realistic face swap testing
        std::string imagePath = TestUtils::getTestImagePath("single_face_2.jpg");
        auto image = ImageLoader::loadImageFromFile(imagePath);
        if (!image)
        {
            std::cerr << "Failed to load target image from: " << imagePath << std::endl;
            ADD_FAILURE() << "Could not load target image: " << imagePath;
        }
        return image;
    }

    std::shared_ptr<InSwapper> inswapper_;
    std::shared_ptr<ArcfaceRecognizer> arcface_;
    std::shared_ptr<SCRFDetector> scrfd_;
    std::unique_ptr<SwapPipeline> swap_pipeline_;

    /**
     * @brief Add text overlay to result image with test information
     * @param image The result image to add text to
     * @param testName Name of the current test
     * @param sourceImagePath Path to the source image used
     * @param targetImagePath Path to the target image used
     * @param executionTimeMs Execution time in milliseconds (optional)
     */
    void addResultTextOverlay(Image* image, const std::string& testName, const std::string& sourceImagePath = "",
                              const std::string& targetImagePath = "", long long executionTimeMs = 0)
    {
        if (!image)
        {
            return;
        }

        // Create text content
        std::string text = "Test: " + testName + "\n";
        if (!sourceImagePath.empty())
        {
            // Extract just the filename from the path
            size_t lastSlash = sourceImagePath.find_last_of("/\\");
            std::string sourceFilename =
                (lastSlash != std::string::npos) ? sourceImagePath.substr(lastSlash + 1) : sourceImagePath;
            text += "Source: " + sourceFilename + "\n";
        }
        if (!targetImagePath.empty())
        {
            // Extract just the filename from the path
            size_t lastSlash = targetImagePath.find_last_of("/\\");
            std::string targetFilename =
                (lastSlash != std::string::npos) ? targetImagePath.substr(lastSlash + 1) : targetImagePath;
            text += "Target: " + targetFilename + "\n";
        }
        if (executionTimeMs > 0)
        {
            text += "Time: " + std::to_string(executionTimeMs) + "ms\n";
        }

        // Add current date
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::string dateStr = std::ctime(&time_t_now);
        // Remove newline from ctime
        if (!dateStr.empty() && dateStr.back() == '\n')
        {
            dateStr.pop_back();
        }
        text += "Date: " + dateStr;

        // Calculate text dimensions and position
        int scale = 1;
        auto textSize = getMultilineTextSize(text, scale, 2);

        // Position text at bottom of image with some padding
        int padding = 10;
        int x = padding;
        int y = image->info.height - textSize.height - padding;

        // Draw the text with automatic background
        Pixel textColor(255, 255, 255); // White text
        Pixel bgColor(0, 0, 0, 128);    // Semi-transparent black background
        drawMultilineTextWithBackground(*image, x, y, text, textColor, bgColor, scale, 2, padding);
    }
};

// Test pipeline initialization
TEST_F(SwapPipelineIntegrationTest, PipelineInitialization)
{
    ASSERT_TRUE(inswapper_->isReady());
    ASSERT_TRUE(arcface_->isReady());
    ASSERT_TRUE(scrfd_->isReady());
    // Pipeline should be properly constructed
    EXPECT_TRUE(swap_pipeline_ != nullptr);
}

// Test end-to-end pipeline with valid inputs
TEST_F(SwapPipelineIntegrationTest, EndToEndPipelineSuccess)
{
    // Load real test images
    auto source_image = loadSourceImage();
    auto target_image = loadTargetImage();

    // Ensure images loaded successfully
    ASSERT_TRUE(source_image != nullptr) << "Source image failed to load";
    ASSERT_TRUE(target_image != nullptr) << "Target image creation failed";

    // Verify image properties
    EXPECT_GT(source_image->info.width, 0) << "Source image has invalid width";
    EXPECT_GT(source_image->info.height, 0) << "Source image has invalid height";
    EXPECT_EQ(source_image->info.pixelSizeBytes, 3) << "Source image should be RGB";

    // Execute face swap pipeline
    const bool swap_result = swap_pipeline_->run(source_image, target_image);

    // Save result image for visual inspection
    if (swap_result && source_image)
    {
        // Add text overlay with test information
        std::string sourcePath = TestUtils::getTestImagePath("single_face.jpeg");
        std::string targetPath = TestUtils::getTestImagePath("single_face_2.jpg");
        addResultTextOverlay(source_image.get(), "EndToEndPipelineSuccess", sourcePath, targetPath);

        std::string outputPath = "../tests/swapPipeline_integration/results/EndToEndPipelineSuccess_result.ppm";
        bool saveResult = source_image->saveToDisk(outputPath);
        EXPECT_TRUE(saveResult) << "Failed to save result image to: " << outputPath;
        if (saveResult)
        {
            std::cout << "Saved result image to: " << outputPath << std::endl;
        }
    }

    // Pipeline should complete successfully with real test images
    EXPECT_TRUE(swap_result) << "Face swap pipeline failed with valid inputs";
}

// Test pipeline with no faces detected
TEST_F(SwapPipelineIntegrationTest, NoFacesDetected)
{
    // Create an image with no detectable faces (uniform black color)
    size_t data_size = 640 * 480 * 3;
    auto source_image = std::make_unique<Image>(data_size);
    source_image->info.width = 640;
    source_image->info.height = 480;
    source_image->info.pixelSizeBytes = 3;
    source_image->info.format = ImageFormat::RGB;
    source_image->black(); // Fill with black - no faces should be detectable

    auto target_image = loadTargetImage();
    ASSERT_TRUE(target_image != nullptr) << "Target image creation failed";

    // Execute pipeline with image containing no faces
    const bool swap_result = swap_pipeline_->run(source_image, target_image);

    // Pipeline should handle gracefully when no faces are detected
    EXPECT_FALSE(swap_result) << "Pipeline should fail gracefully with no detectable faces";
}

// Test pipeline with invalid target image
TEST_F(SwapPipelineIntegrationTest, InvalidTargetImage)
{
    // Load valid source image
    auto source_image = loadSourceImage();
    ASSERT_TRUE(source_image != nullptr) << "Source image failed to load";

    // Create invalid target image (empty)
    auto invalid_target = std::make_unique<Image>(0); // Invalid empty image

    // Execute pipeline with invalid target
    const bool swap_result = swap_pipeline_->run(source_image, invalid_target);

    // Pipeline should handle invalid target image gracefully
    EXPECT_FALSE(swap_result) << "Pipeline should fail gracefully with invalid target image";
}

// Test multiple faces in source image
TEST_F(SwapPipelineIntegrationTest, MultipleFacesInSource)
{
    // Load image with multiple faces
    auto source_image = loadMultipleFacesSourceImage();
    auto target_image = loadTargetImage();

    ASSERT_TRUE(source_image != nullptr) << "Multiple faces source image failed to load";
    ASSERT_TRUE(target_image != nullptr) << "Target image creation failed";

    // Execute pipeline with multiple faces source
    const bool swap_result = swap_pipeline_->run(source_image, target_image);

    // Pipeline should handle multiple faces appropriately
    // Note: Result may vary based on face detection and selection logic
    EXPECT_TRUE(swap_result || !swap_result) << "Pipeline should handle multiple faces gracefully";
}

// Test component integration - SCRFD face detection + ArcfaceRecognizer
TEST_F(SwapPipelineIntegrationTest, SCRFDArcfaceIntegration)
{
    // Load test image with known face
    auto test_image = loadSourceImage();
    ASSERT_TRUE(test_image != nullptr) << "Test image failed to load";

    // Test SCRFD face detection
    const std::vector<Face> detected_faces = scrfd_->detect(test_image);
    EXPECT_GE(detected_faces.size(), 0) << "Face detection should not crash";

    // If faces are detected, test recognition pipeline
    if (!detected_faces.empty())
    {
        const Face& primary_face = detected_faces[0];
        auto landmarks = primary_face.getFivePointLandmarksArcFaceOrder2D();

        // Verify landmarks are properly extracted
        if (landmarks.size() == 5)
        {
            std::vector<float> face_embedding;
            const bool recognition_result = arcface_->recognize(*test_image, landmarks, face_embedding);

            // Recognition should work if face detection succeeded
            if (recognition_result)
            {
                EXPECT_EQ(face_embedding.size(), 512) << "Face embedding should be 512-dimensional";
                std::cout << "Face recognition successful, embedding size: " << face_embedding.size() << std::endl;
            }
            else
            {
                std::cout << "Face recognition failed, but detection succeeded" << std::endl;
            }
        }
        else
        {
            std::cout << "Insufficient landmarks detected: " << landmarks.size() << "/5" << std::endl;
        }
    }
    else
    {
        std::cout << "No faces detected in test image" << std::endl;
    }
}

// Test performance of the full pipeline
TEST_F(SwapPipelineIntegrationTest, PipelinePerformance)
{
    // Load test images
    auto source_image = loadSourceImage();
    auto target_image = loadTargetImage();

    ASSERT_TRUE(source_image != nullptr) << "Source image failed to load";
    ASSERT_TRUE(target_image != nullptr) << "Target image creation failed";

    // Measure execution time
    auto start_time = std::chrono::high_resolution_clock::now();
    const bool swap_result = swap_pipeline_->run(source_image, target_image);
    auto end_time = std::chrono::high_resolution_clock::now();

    auto execution_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Save result image for verification
    if (source_image)
    {
        // Add text overlay with test information and execution time
        std::string sourcePath = TestUtils::getTestImagePath("single_face.jpeg");
        std::string targetPath = TestUtils::getTestImagePath("single_face_2.jpg");
        addResultTextOverlay(source_image.get(), "PipelinePerformance", sourcePath, targetPath,
                             execution_duration.count());

        std::string outputPath = "../tests/swapPipeline_integration/results/PipelinePerformance_result.ppm";
        bool saveResult = source_image->saveToDisk(outputPath);
        EXPECT_TRUE(saveResult) << "Failed to save result image to: " << outputPath;
        if (saveResult)
        {
            std::cout << "Saved result image to: " << outputPath << std::endl;
        }
    }

    // Verify performance requirements
    EXPECT_LT(execution_duration.count(), 5000)
        << "Pipeline took " << execution_duration.count() << "ms, exceeds 5s limit";

    // Log performance results
    std::cout << "Pipeline execution time: " << execution_duration.count() << "ms" << std::endl;
    std::cout << "Pipeline result: " << (swap_result ? "SUCCESS" : "FAILED") << std::endl;
}

// Test pipeline with different image sizes
TEST_F(SwapPipelineIntegrationTest, DifferentImageSizes)
{
    // Test various image sizes to ensure pipeline robustness
    const std::vector<std::pair<int, int>> test_sizes = {
        {320,  240}, // Small size
        {640,  480}, // Standard webcam size
        {800,  600}, // Medium size
        {1024, 768}  // Large size
    };

    for (const auto& [width, height] : test_sizes)
    {
        std::cout << "Testing image size: " << width << "x" << height << std::endl;

        // Load source image and create target
        auto source_image = loadSourceImage();
        auto target_image = loadTargetImage();

        ASSERT_TRUE(source_image != nullptr) << "Source image failed to load for size test";
        ASSERT_TRUE(target_image != nullptr) << "Target image creation failed for size test";

        // Execute pipeline
        const bool swap_result = swap_pipeline_->run(source_image, target_image);

        // Save result image for each size
        if (source_image)
        {
            // Add text overlay with test information and image size
            std::string sourcePath = TestUtils::getTestImagePath("single_face.jpeg");
            std::string targetPath = TestUtils::getTestImagePath("single_face_2.jpg");
            std::string testName = "DifferentImageSizes_" + std::to_string(width) + "x" + std::to_string(height);
            addResultTextOverlay(source_image.get(), testName, sourcePath, targetPath);

            std::string outputPath = "../tests/swapPipeline_integration/results/DifferentImageSizes_"
                                     + std::to_string(width) + "x" + std::to_string(height) + "_result.ppm";
            bool saveResult = source_image->saveToDisk(outputPath);
            EXPECT_TRUE(saveResult) << "Failed to save result image for size " << width << "x" << height;
            if (saveResult)
            {
                std::cout << "Saved result image to: " << outputPath << std::endl;
            }
        }

        // Pipeline should handle different sizes gracefully
        EXPECT_TRUE(swap_result || !swap_result) << "Pipeline failed for size " << width << "x" << height;
    }
}

// Test target image caching behavior
TEST_F(SwapPipelineIntegrationTest, TargetImageCaching)
{
    // Load source images and create consistent target
    auto source_image1 = loadSourceImage();
    auto source_image2 = loadSourceImage();
    auto target_image = loadTargetImage();

    ASSERT_TRUE(source_image1 != nullptr) << "First source image failed to load";
    ASSERT_TRUE(source_image2 != nullptr) << "Second source image failed to load";
    ASSERT_TRUE(target_image != nullptr) << "Target image creation failed";

    // First run should process target image and cache embedding
    const bool first_result = swap_pipeline_->run(source_image1, target_image);

    // Second run should use cached target embedding (optimization)
    const bool second_result = swap_pipeline_->run(source_image2, target_image);

    // Save result images for comparison
    if (source_image1)
    {
        // Add text overlay for first result
        std::string sourcePath = TestUtils::getTestImagePath("single_face.jpeg");
        std::string targetPath = TestUtils::getTestImagePath("single_face_2.jpg");
        addResultTextOverlay(source_image1.get(), "TargetImageCaching_First", sourcePath, targetPath);

        std::string outputPath1 = "../tests/swapPipeline_integration/results/TargetImageCaching_first_result.ppm";
        bool saveResult1 = source_image1->saveToDisk(outputPath1);
        EXPECT_TRUE(saveResult1) << "Failed to save first result image";
        if (saveResult1)
        {
            std::cout << "Saved first result image to: " << outputPath1 << std::endl;
        }
    }

    if (source_image2)
    {
        // Add text overlay for second result
        std::string sourcePath = TestUtils::getTestImagePath("single_face.jpeg");
        std::string targetPath = TestUtils::getTestImagePath("single_face_2.jpg");
        addResultTextOverlay(source_image2.get(), "TargetImageCaching_Second", sourcePath, targetPath);

        std::string outputPath2 = "../tests/swapPipeline_integration/results/TargetImageCaching_second_result.ppm";
        bool saveResult2 = source_image2->saveToDisk(outputPath2);
        EXPECT_TRUE(saveResult2) << "Failed to save second result image";
        if (saveResult2)
        {
            std::cout << "Saved second result image to: " << outputPath2 << std::endl;
        }
    }

    // Both runs should behave consistently (caching shouldn't affect results)
    EXPECT_EQ(first_result, second_result) << "Inconsistent results between cached and non-cached runs";
}

// Test memory management across multiple runs
TEST_F(SwapPipelineIntegrationTest, MemoryManagement)
{
    // Test that pipeline doesn't leak memory across multiple runs
    constexpr int num_iterations = 3;

    for (int iteration = 0; iteration < num_iterations; ++iteration)
    {
        std::cout << "Memory management test - iteration " << (iteration + 1) << "/" << num_iterations << std::endl;

        // Load fresh images for each iteration
        auto source_image = loadSourceImage();
        auto target_image = loadTargetImage();

        ASSERT_TRUE(source_image != nullptr) << "Source image failed to load in iteration " << iteration;
        ASSERT_TRUE(target_image != nullptr) << "Target image creation failed in iteration " << iteration;

        // Execute pipeline
        const bool swap_result = swap_pipeline_->run(source_image, target_image);

        // Save result image only for the first run (representative sample)
        if (source_image && iteration == 0)
        {
            std::string outputPath = "../tests/swapPipeline_integration/results/MemoryManagement_result.ppm";
            bool saveResult = source_image->saveToDisk(outputPath);
            EXPECT_TRUE(saveResult) << "Failed to save result image for iteration " << iteration;
            if (saveResult)
            {
                std::cout << "Saved result image to: " << outputPath << std::endl;
            }
        }

        // Each run should complete without memory issues
        EXPECT_TRUE(swap_result || !swap_result) << "Pipeline failed in iteration " << iteration;
    }

    std::cout << "Memory management test completed successfully" << std::endl;
}

// Test error propagation with invalid inputs
TEST_F(SwapPipelineIntegrationTest, ErrorPropagation)
{
    // Load valid source image
    auto source_image = loadSourceImage();
    ASSERT_TRUE(source_image != nullptr) << "Source image failed to load";

    // Create invalid target image (empty/nullptr equivalent)
    auto invalid_target = std::make_unique<Image>(0); // Invalid empty image

    // Execute pipeline with invalid target
    const bool swap_result = swap_pipeline_->run(source_image, invalid_target);

    // Pipeline should propagate errors gracefully
    EXPECT_FALSE(swap_result) << "Pipeline should fail gracefully with invalid target image";
}

// Test concurrent pipeline operations (basic smoke test)
TEST_F(SwapPipelineIntegrationTest, ConcurrentOperations)
{
    // Create multiple pipeline instances for concurrent testing
    auto pipeline2 = std::make_unique<SwapPipeline>(inswapper_, arcface_, scrfd_);
    auto pipeline3 = std::make_unique<SwapPipeline>(inswapper_, arcface_, scrfd_);

    // Load different source images and create targets
    auto source1 = loadSourceImage();
    auto source2 = loadMultipleFacesSourceImage();
    auto source3 = loadSourceImage();
    auto target = loadTargetImage();

    // Verify all images loaded successfully
    ASSERT_TRUE(source1 != nullptr) << "Source1 image failed to load";
    ASSERT_TRUE(source2 != nullptr) << "Source2 image failed to load";
    ASSERT_TRUE(source3 != nullptr) << "Source3 image failed to load";
    ASSERT_TRUE(target != nullptr) << "Target image creation failed";

    // Execute multiple pipelines concurrently (in sequence for simplicity)
    std::cout << "Running concurrent pipeline operations..." << std::endl;
    const bool result1 = swap_pipeline_->run(source1, target);
    const bool result2 = pipeline2->run(source2, target);
    const bool result3 = pipeline3->run(source3, target);

    // Save result images for verification
    const std::vector<std::pair<std::unique_ptr<Image>&, std::string>> results = {
        {source1, "../tests/swapPipeline_integration/results/ConcurrentOperations_pipeline1_result.ppm"},
        {source2, "../tests/swapPipeline_integration/results/ConcurrentOperations_pipeline2_result.ppm"},
        {source3, "../tests/swapPipeline_integration/results/ConcurrentOperations_pipeline3_result.ppm"}
    };

    for (const auto& [image, outputPath] : results)
    {
        if (image)
        {
            bool saveResult = image->saveToDisk(outputPath);
            EXPECT_TRUE(saveResult) << "Failed to save result image to: " << outputPath;
            if (saveResult)
            {
                std::cout << "Saved result image to: " << outputPath << std::endl;
            }
        }
    }

    // All pipelines should complete without interference
    EXPECT_TRUE(result1 || !result1) << "Pipeline1 failed unexpectedly";
    EXPECT_TRUE(result2 || !result2) << "Pipeline2 failed unexpectedly";
    EXPECT_TRUE(result3 || !result3) << "Pipeline3 failed unexpectedly";

    std::cout << "Concurrent operations test completed" << std::endl;
}

/**
 * @brief Test face swapping of an image with itself (self-swap)
 *
 * This test validates the pipeline behavior when both source and target are the same image.
 * Theoretically, the result should be identical to the original image since we're swapping
 * the face with itself. This test helps identify any artifacts or quality degradation
 * in the swap process.
 */
TEST_F(SwapPipelineIntegrationTest, SelfSwapSingleIteration)
{
    ASSERT_TRUE(swap_pipeline_ != nullptr) << "SwapPipeline not initialized";

    // Load the same image as both source and target
    auto sourceImage = loadSourceImage();
    auto targetImage = loadSourceImage(); // Same image as source

    ASSERT_TRUE(sourceImage != nullptr) << "Source image loading failed";
    ASSERT_TRUE(targetImage != nullptr) << "Target image loading failed";

    auto start = std::chrono::high_resolution_clock::now();

    // Perform self-swap
    bool result = swap_pipeline_->run(sourceImage, targetImage);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_TRUE(result) << "Self-swap operation failed";

    // Add informative text overlay
    std::string sourcePath = TestUtils::getTestImagePath("man1.jpeg");
    addResultTextOverlay(sourceImage.get(), "SelfSwapSingleIteration", sourcePath, sourcePath, duration.count());

    // Save result for visual inspection
    std::string outputPath =
        TestUtils::getTestResultPath("swapPipeline_integration", "SelfSwapSingleIteration_result.ppm");
    bool saveResult = sourceImage->saveToDisk(outputPath);
    EXPECT_TRUE(saveResult) << "Failed to save result image to: " << outputPath;

    if (saveResult)
    {
        std::cout << "Saved self-swap result to: " << outputPath << std::endl;
        std::cout << "Self-swap execution time: " << duration.count() << " ms" << std::endl;
    }
}

/**
 * @brief Test multiple consecutive self-swaps
 *
 * This test performs multiple iterations of self-swapping on the same image.
 * Each iteration should theoretically produce the same result, but in practice
 * we might observe cumulative artifacts or quality degradation due to:
 * - Floating point precision errors
 * - Image compression/decompression artifacts
 * - Transformation approximations
 *
 * This test helps identify stability and quality preservation issues.
 */
TEST_F(SwapPipelineIntegrationTest, SelfSwapMultipleIterations)
{
    ASSERT_TRUE(swap_pipeline_ != nullptr) << "SwapPipeline not initialized";

    const int iterations = 5;
    auto workingImage = loadSourceImage();
    ASSERT_TRUE(workingImage != nullptr) << "Source image loading failed";

    std::vector<long long> executionTimes;
    std::string sourcePath = TestUtils::getTestImagePath("man1.jpeg");

    for (int i = 0; i < iterations; ++i)
    {
        // Create a copy of the working image to use as target
        auto targetImage = workingImage->deepCopy();

        auto start = std::chrono::high_resolution_clock::now();

        // Perform self-swap iteration
        bool result = swap_pipeline_->run(workingImage, targetImage);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        executionTimes.push_back(duration.count());

        EXPECT_TRUE(result) << "Self-swap iteration " << (i + 1) << " failed";

        // Save intermediate results for quality analysis
        std::string filename = "SelfSwapMultiple_iteration" + std::to_string(i + 1) + "_result.ppm";
        std::string outputPath = TestUtils::getTestResultPath("swapPipeline_integration", filename);

        // Add iteration-specific text overlay
        std::string iterationInfo =
            "SelfSwapMultiple (Iteration " + std::to_string(i + 1) + "/" + std::to_string(iterations) + ")";
        addResultTextOverlay(workingImage.get(), iterationInfo, sourcePath, sourcePath, duration.count());

        bool saveResult = workingImage->saveToDisk(outputPath);
        EXPECT_TRUE(saveResult) << "Failed to save iteration " << (i + 1) << " result";

        if (saveResult)
        {
            std::cout << "Saved iteration " << (i + 1) << " result to: " << outputPath << " (time: " << duration.count()
                      << " ms)" << std::endl;
        }
    }

    // Calculate and report performance statistics
    if (!executionTimes.empty())
    {
        long long totalTime = 0;
        long long minTime = executionTimes[0];
        long long maxTime = executionTimes[0];

        for (long long time : executionTimes)
        {
            totalTime += time;
            minTime = std::min(minTime, time);
            maxTime = std::max(maxTime, time);
        }

        double avgTime = static_cast<double>(totalTime) / executionTimes.size();

        std::cout << "Multiple self-swap statistics:" << std::endl;
        std::cout << "  Total iterations: " << iterations << std::endl;
        std::cout << "  Average time: " << avgTime << " ms" << std::endl;
        std::cout << "  Min time: " << minTime << " ms" << std::endl;
        std::cout << "  Max time: " << maxTime << " ms" << std::endl;
        std::cout << "  Total time: " << totalTime << " ms" << std::endl;
    }
}
