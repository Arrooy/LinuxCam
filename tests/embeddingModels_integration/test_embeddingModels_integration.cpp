#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <sstream>
#include <string>
#include <vector>

#include "../common/test_utils.h"
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
 * Image quality metrics for comparing original and swapped faces
 */
struct QualityMetrics
{
    double mse;                // Mean Squared Error
    double psnr;               // Peak Signal-to-Noise Ratio
    double ssim;               // Structural Similarity Index
    double lpips;              // Learned Perceptual Image Patch Similarity (approximated)
    float identity_similarity; // Identity preservation using embeddings

    // Helper function to convert metrics to string for logging
    std::string toString() const
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << "MSE: " << mse << ", PSNR: " << psnr << "dB, SSIM: " << ssim;
        oss << ", LPIPS: " << lpips << ", Identity: " << identity_similarity;
        return oss.str();
    }
};

/**
 * Calculate Mean Squared Error between two images
 */
double calculateMSE(const Image& img1, const Image& img2)
{
    if (img1.info.width != img2.info.width || img1.info.height != img2.info.height)
    {
        return -1.0; // Invalid comparison
    }

    double mse = 0.0;
    size_t totalPixels = img1.info.width * img1.info.height;

    for (size_t y = 0; y < img1.info.height; ++y)
    {
        for (size_t x = 0; x < img1.info.width; ++x)
        {
            const auto& p1 = img1(x, y);
            const auto& p2 = img2(x, y);

            double dr = static_cast<double>(p1.r) - static_cast<double>(p2.r);
            double dg = static_cast<double>(p1.g) - static_cast<double>(p2.g);
            double db = static_cast<double>(p1.b) - static_cast<double>(p2.b);

            mse += (dr * dr + dg * dg + db * db) / 3.0;
        }
    }

    return mse / totalPixels;
}

/**
 * Calculate Peak Signal-to-Noise Ratio
 */
double calculatePSNR(double mse)
{
    if (mse <= 0.0)
    {
        return 100.0; // Perfect match
    }
    return 20.0 * std::log10(255.0 / std::sqrt(mse));
}

/**
 * Calculate Structural Similarity Index (SSIM) between two images
 */
double calculateSSIM(const Image& img1, const Image& img2)
{
    if (img1.info.width != img2.info.width || img1.info.height != img2.info.height)
    {
        return -1.0; // Invalid comparison
    }

    const double C1 = 6.5025;  // (0.01 * 255)^2
    const double C2 = 58.5225; // (0.03 * 255)^2

    // Calculate means
    double mu1 = 0.0, mu2 = 0.0;
    size_t totalPixels = img1.info.width * img1.info.height;

    for (size_t y = 0; y < img1.info.height; ++y)
    {
        for (size_t x = 0; x < img1.info.width; ++x)
        {
            const auto& p1 = img1(x, y);
            const auto& p2 = img2(x, y);

            // Convert to grayscale for SSIM calculation
            double gray1 = 0.299 * p1.r + 0.587 * p1.g + 0.114 * p1.b;
            double gray2 = 0.299 * p2.r + 0.587 * p2.g + 0.114 * p2.b;

            mu1 += gray1;
            mu2 += gray2;
        }
    }

    mu1 /= totalPixels;
    mu2 /= totalPixels;

    // Calculate variances and covariance
    double sigma1_sq = 0.0, sigma2_sq = 0.0, sigma12 = 0.0;

    for (size_t y = 0; y < img1.info.height; ++y)
    {
        for (size_t x = 0; x < img1.info.width; ++x)
        {
            const auto& p1 = img1(x, y);
            const auto& p2 = img2(x, y);

            double gray1 = 0.299 * p1.r + 0.587 * p1.g + 0.114 * p1.b;
            double gray2 = 0.299 * p2.r + 0.587 * p2.g + 0.114 * p2.b;

            double diff1 = gray1 - mu1;
            double diff2 = gray2 - mu2;

            sigma1_sq += diff1 * diff1;
            sigma2_sq += diff2 * diff2;
            sigma12 += diff1 * diff2;
        }
    }

    sigma1_sq /= (totalPixels - 1);
    sigma2_sq /= (totalPixels - 1);
    sigma12 /= (totalPixels - 1);

    // Calculate SSIM
    double numerator = (2 * mu1 * mu2 + C1) * (2 * sigma12 + C2);
    double denominator = (mu1 * mu1 + mu2 * mu2 + C1) * (sigma1_sq + sigma2_sq + C2);

    return numerator / denominator;
}

/**
 * Approximate LPIPS using simple perceptual features
 * This is a simplified version - real LPIPS would use deep neural networks
 */
double calculateApproximateLPIPS(const Image& img1, const Image& img2)
{
    if (img1.info.width != img2.info.width || img1.info.height != img2.info.height)
    {
        return -1.0; // Invalid comparison
    }

    // Simple gradient-based perceptual difference
    double totalDiff = 0.0;
    size_t validPixels = 0;

    for (size_t y = 1; y < img1.info.height - 1; ++y)
    {
        for (size_t x = 1; x < img1.info.width - 1; ++x)
        {
            // Calculate gradients in both images
            const auto& p1_center = img1(x, y);
            const auto& p1_right = img1(x + 1, y);
            const auto& p1_down = img1(x, y + 1);

            const auto& p2_center = img2(x, y);
            const auto& p2_right = img2(x + 1, y);
            const auto& p2_down = img2(x, y + 1);

            // Convert to grayscale and calculate gradients
            double g1_center = 0.299 * p1_center.r + 0.587 * p1_center.g + 0.114 * p1_center.b;
            double g1_right = 0.299 * p1_right.r + 0.587 * p1_right.g + 0.114 * p1_right.b;
            double g1_down = 0.299 * p1_down.r + 0.587 * p1_down.g + 0.114 * p1_down.b;

            double g2_center = 0.299 * p2_center.r + 0.587 * p2_center.g + 0.114 * p2_center.b;
            double g2_right = 0.299 * p2_right.r + 0.587 * p2_right.g + 0.114 * p2_right.b;
            double g2_down = 0.299 * p2_down.r + 0.587 * p2_down.g + 0.114 * p2_down.b;

            double grad1_x = g1_right - g1_center;
            double grad1_y = g1_down - g1_center;
            double grad2_x = g2_right - g2_center;
            double grad2_y = g2_down - g2_center;

            // Calculate gradient magnitude difference
            double mag1 = std::sqrt(grad1_x * grad1_x + grad1_y * grad1_y);
            double mag2 = std::sqrt(grad2_x * grad2_x + grad2_y * grad2_y);

            totalDiff += std::abs(mag1 - mag2);
            validPixels++;
        }
    }

    return (validPixels > 0) ? (totalDiff / validPixels) / 255.0 : 0.0;
}

/**
 * Calculate cosine similarity between two embedding vectors
 */
float calculateCosineSimilarity(const std::vector<float>& embed1, const std::vector<float>& embed2)
{
    if (embed1.size() != embed2.size() || embed1.empty())
    {
        return -1.0f; // Invalid
    }

    float dot_product = 0.0f;
    float norm1 = 0.0f;
    float norm2 = 0.0f;

    for (size_t i = 0; i < embed1.size(); ++i)
    {
        dot_product += embed1[i] * embed2[i];
        norm1 += embed1[i] * embed1[i];
        norm2 += embed2[i] * embed2[i];
    }

    if (norm1 <= 0.0f || norm2 <= 0.0f)
    {
        return -1.0f; // Invalid
    }

    return dot_product / (std::sqrt(norm1) * std::sqrt(norm2));
}

/**
 * Comprehensive quality evaluation function
 */
QualityMetrics
evaluateImageQuality(const Image& original, const Image& swapped, const std::vector<float>& original_embedding,
                     const std::vector<float>& swapped_embedding)
{
    QualityMetrics metrics;

    // Calculate MSE and PSNR
    metrics.mse = calculateMSE(original, swapped);
    metrics.psnr = calculatePSNR(metrics.mse);

    // Calculate SSIM
    metrics.ssim = calculateSSIM(original, swapped);

    // Calculate approximate LPIPS
    metrics.lpips = calculateApproximateLPIPS(original, swapped);

    // Calculate identity similarity
    metrics.identity_similarity = calculateCosineSimilarity(original_embedding, swapped_embedding);

    return metrics;
}

/**
 * @brief Integration test suite for all embedding models with SwapPipeline
 *
 * This test suite validates all embedding models that can generate embeddings
 * compatible with the face swap pipeline. It tests each model by running
 * the SwapPipeline and saving results for visual inspection.
 *
 * Models tested:
 * - focal-arcface* (all variants)
 * - arcface* (all variants)
 * - cavaface* (all variants)
 * - CurricularFace
 * - glint* (all variants)
 * - ms1mv3_arcface*
 */
class EmbeddingModelsIntegrationTest : public ::testing::Test
{
  protected:
    /**
     * Structure to hold performance statistics for a model across multiple iterations
     */
    struct ModelIterationStats
    {
        std::string model_name;
        std::vector<long long> execution_times;
        bool all_iterations_successful;
        long long total_time;
        double avg_time;
        long long min_time;
        long long max_time;
    };

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

        // Initialize shared components
        std::string models_folder = TestUtils::getModelsDir() + "/";
        inswapper_ = std::make_shared<InSwapper>(TestUtils::getModelPath("inswapper_128.onnx"));
        scrfd_ = std::make_shared<SCRFDetector>(models_folder + "scrfd_500m_bnkps_shape640x640.onnx");

        ASSERT_TRUE(inswapper_ && inswapper_->isReady()) << "InSwapper failed to initialize";
        ASSERT_TRUE(scrfd_ && scrfd_->isReady()) << "SCRFD detector failed to initialize";

        // Load test images
        source_image_ = loadSourceImage();
        target_image_ = loadTargetImage();
        ASSERT_TRUE(source_image_) << "Failed to load source image";
        ASSERT_TRUE(target_image_) << "Failed to load target image";

        // Ensure results directory exists
        std::filesystem::create_directories(TestUtils::getTestResultsDir("embeddingModels_integration"));
    }

    std::unique_ptr<linuxface::Image> loadSourceImage()
    {
        std::string imagePath = "/home/arroyo/Documents/Projectes/LinuxCam/albert.jpeg";
        // TestUtils::getTestImagePath("single_face.jpeg");
        auto image = ImageLoader::loadImageFromFile(imagePath);
        if (!image)
        {
            std::cerr << "Failed to load source image from: " << imagePath << std::endl;
        }
        return image;
    }

    std::unique_ptr<linuxface::Image> loadTargetImage()
    {
        std::string imagePath = "/home/arroyo/Documents/Projectes/LinuxCam/albert.jpeg";
        // std::string imagePath = TestUtils::getTestImagePath("single_face.jpeg");
        auto image = ImageLoader::loadImageFromFile(imagePath);
        if (!image)
        {
            std::cerr << "Failed to load target image from: " << imagePath << std::endl;
        }
        return image;
    }

    /**
     * Test a specific embedding model with the SwapPipeline
     */
    bool testEmbeddingModel(const std::string& model_filename)
    {
        std::cout << "Testing embedding model: " << model_filename << std::endl;

        // Create ArcfaceRecognizer with the specific model
        std::string model_path = TestUtils::getModelPath(model_filename);
        auto arcface = std::make_shared<ArcfaceRecognizer>(model_path);

        if (!arcface || !arcface->isReady())
        {
            std::cerr << "Failed to initialize ArcfaceRecognizer with model: " << model_filename << std::endl;
            return false;
        }

        // Create SwapPipeline with this specific embedding model
        auto swap_pipeline = std::make_unique<SwapPipeline>(inswapper_, arcface, scrfd_);

        // Create copies of images for processing (SwapPipeline modifies input)
        auto source_copy = source_image_->deepCopy();
        auto target_copy = target_image_->deepCopy();

        // Run the swap pipeline
        auto start_time = std::chrono::high_resolution_clock::now();
        bool swap_success = swap_pipeline->run(source_copy, target_copy);
        auto end_time = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << "Swap pipeline execution time for " << model_filename << ": " << duration.count() << " ms"
                  << std::endl;

        if (swap_success)
        {
            // Save result with model name in filename
            std::string result_filename = getResultFilename(model_filename);
            std::string result_path = TestUtils::getTestResultPath("embeddingModels_integration", result_filename);

            if (source_copy->saveToDisk(result_path))
            {
                std::cout << "Saved result for " << model_filename << " to: " << result_path << std::endl;
            }
            else
            {
                std::cerr << "Failed to save result for " << model_filename << " to: " << result_path << std::endl;
            }
        }
        else
        {
            std::cerr << "Swap pipeline failed for model: " << model_filename << std::endl;
        }

        return swap_success;
    }

    /**
     * Generate result filename based on model name
     */
    std::string getResultFilename(const std::string& model_filename)
    {
        // Remove .onnx extension and replace special characters
        std::string base_name = model_filename;
        if (base_name.length() >= 5 && base_name.substr(base_name.length() - 5) == ".onnx")
        {
            base_name = base_name.substr(0, base_name.length() - 5);
        }

        // Replace problematic characters for filenames
        std::replace(base_name.begin(), base_name.end(), '-', '_');
        std::replace(base_name.begin(), base_name.end(), '.', '_');

        return base_name + "_swap_result.ppm";
    }

    /**
     * Create a grid visualization for the parametric iteration test
     * Each row represents a model, each column represents an iteration
     */
    void createParametricIterationGrid(const std::vector<ModelIterationStats>& all_model_stats, int iterations)
    {
        std::cout << "Creating parametric iteration grid visualization..." << std::endl;

        // Helper function to crop face with consistent padding
        auto cropFaceWithPadding = [this](const std::unique_ptr<linuxface::Image>& image, const std::string& name) -> std::unique_ptr<linuxface::Image> {
            std::vector<Face> faces = scrfd_->detect(image);
            if (faces.empty()) {
                std::cerr << "Warning: No face detected in image: " << name << std::endl;
                return nullptr;
            }

            const auto& face = faces[0];
            auto face_bbox = face.getBoundingBox().rect;

            // Add 30% padding to the bounding box
            float padding_factor = 0.3f;
            float width_padding = face_bbox.width() * padding_factor;
            float height_padding = face_bbox.height() * padding_factor;

            math_utils::Rect<float> padded_bbox(
                std::max(0.0f, face_bbox.x() - width_padding), 
                std::max(0.0f, face_bbox.y() - height_padding),
                std::min(static_cast<float>(image->info.width), face_bbox.x() + face_bbox.width() + width_padding),
                std::min(static_cast<float>(image->info.height), face_bbox.y() + face_bbox.height() + height_padding)
            );

            return image->crop(padded_bbox);
        };

        // Collect only successful models for grid
        std::vector<ModelIterationStats> successful_models;
        for (const auto& stats : all_model_stats)
        {
            if (stats.all_iterations_successful)
            {
                successful_models.push_back(stats);
            }
        }

        if (successful_models.empty())
        {
            std::cerr << "No successful models found for grid creation" << std::endl;
            return;
        }

        // Prepare grid cells (rows = models, columns = iterations + 1 for original)
        std::vector<TestUtils::GridCell> grid_cells;
        
        // Add original image as the first column for each row
        std::cout << "Processing original source image..." << std::endl;
        auto original_cropped = cropFaceWithPadding(source_image_, "original source");
        
        for (size_t model_idx = 0; model_idx < successful_models.size(); ++model_idx)
        {
            const auto& model_stats = successful_models[model_idx];
            
            // Add original image for this row (first column)
            TestUtils::GridCell original_cell;
            if (original_cropped)
            {
                original_cell.image = original_cropped->deepCopy();
                original_cell.label = (model_idx == 0) ? "Original" : "";  // Only label the first one
                original_cell.highlight = true;
                original_cell.highlight_color = Pixel(0, 255, 0);  // Green border for original
            }
            grid_cells.push_back(std::move(original_cell));

            // Add iteration results for this model
            for (int iter = 1; iter <= iterations; ++iter)
            {
                TestUtils::GridCell iter_cell;
                
                // Generate the filename for this model and iteration
                std::string model_safe_name = model_stats.model_name;
                std::replace(model_safe_name.begin(), model_safe_name.end(), '.', '_');
                std::replace(model_safe_name.begin(), model_safe_name.end(), '-', '_');

                std::string filename = "SelfSwapParametric_" + model_safe_name + 
                                     "_iteration" + std::to_string(iter) + "_result.ppm";
                std::string file_path = TestUtils::getTestResultPath("embeddingModels_integration", filename);

                // Load and process the iteration result
                auto iter_image = ImageLoader::loadImageFromFile(file_path);
                if (iter_image)
                {
                    auto iter_cropped = cropFaceWithPadding(iter_image, "iteration " + std::to_string(iter));
                    if (iter_cropped)
                    {
                        iter_cell.image = std::move(iter_cropped);
                        iter_cell.label = (model_idx == 0) ? ("Iter " + std::to_string(iter)) : "";  // Only label the first row
                    }
                }
                else
                {
                    std::cerr << "Failed to load iteration result: " << file_path << std::endl;
                }
                
                grid_cells.push_back(std::move(iter_cell));
            }
        }

        // Add row labels (model names) by creating a separate column
        std::cout << "Adding row labels..." << std::endl;
        std::vector<TestUtils::GridCell> labeled_grid_cells;
        
        for (size_t model_idx = 0; model_idx < successful_models.size(); ++model_idx)
        {
            const auto& model_stats = successful_models[model_idx];
            
            // Create a label cell for the model name (at the beginning of each row)
            TestUtils::GridCell label_cell;
            // Create a small image for the label
            auto label_image = std::make_unique<linuxface::Image>(Pixel(220, 220, 220), 120, 120);
            label_image->info.format = ImageFormat::RGB;
            label_image->info.pixelSizeBytes = 3;
            
            // Add model name as label
            std::string model_display_name = model_stats.model_name;
            if (model_display_name.length() >= 5 && model_display_name.substr(model_display_name.length() - 5) == ".onnx")
            {
                model_display_name = model_display_name.substr(0, model_display_name.length() - 5);
            }
            std::replace(model_display_name.begin(), model_display_name.end(), '_', ' ');
            std::replace(model_display_name.begin(), model_display_name.end(), '-', ' ');
            
            // Truncate if too long
            if (model_display_name.length() > 15)
            {
                model_display_name = model_display_name.substr(0, 12) + "...";
            }
            
            label_cell.image = std::move(label_image);
            label_cell.label = model_display_name;
            label_cell.highlight = true;
            label_cell.highlight_color = Pixel(0, 0, 255);  // Blue border for model labels
            labeled_grid_cells.push_back(std::move(label_cell));
            
            // Add the actual content cells for this row
            for (int col = 0; col <= iterations; ++col)  // +1 for original
            {
                size_t source_idx = model_idx * (iterations + 1) + col;
                if (source_idx < grid_cells.size())
                {
                    labeled_grid_cells.push_back(std::move(grid_cells[source_idx]));
                }
            }
        }

        // Create the grid visualization
        std::string title = "Self-Swap Multiple Iterations: " + std::to_string(successful_models.size()) + 
                           " Models × " + std::to_string(iterations) + " Iterations";
        
        auto grid_image = TestUtils::createGridVisualization(
            labeled_grid_cells,
            successful_models.size(),  // rows
            iterations + 2,           // cols (label + original + iterations)
            8,                        // spacing
            Pixel(245, 245, 245),     // background
            title
        );

        if (grid_image)
        {
            std::string grid_path = TestUtils::getTestResultPath("embeddingModels_integration", "parametric_iterations_grid.ppm");
            bool saved = grid_image->saveToDisk(grid_path);

            if (saved)
            {
                std::cout << "✓ Parametric iteration grid saved to: " << grid_path << std::endl;
                std::cout << "Grid dimensions: " << grid_image->info.width << "x" << grid_image->info.height << std::endl;
                std::cout << "Grid layout: " << successful_models.size() << " models × " << (iterations + 2) << " columns (label + original + " << iterations << " iterations)" << std::endl;
            }
            else
            {
                std::cerr << "Failed to save parametric iteration grid to: " << grid_path << std::endl;
            }
        }
        else
        {
            std::cerr << "Failed to create parametric iteration grid" << std::endl;
        }
    }

    // Test fixtures
    std::shared_ptr<InSwapper> inswapper_;
    std::shared_ptr<SCRFDetector> scrfd_;
    std::unique_ptr<linuxface::Image> source_image_;
    std::unique_ptr<linuxface::Image> target_image_;
};

/**
 * Test that verifies all embedding models can be discovered
 */
TEST_F(EmbeddingModelsIntegrationTest, DiscoverEmbeddingModels)
{
    std::vector<std::string> models = TestUtils::getEmbeddingModelFiles();

    EXPECT_GT(models.size(), 0) << "No embedding models found in models directory";

    std::cout << "Discovered " << models.size() << " embedding models:" << std::endl;
    for (const auto& model : models)
    {
        std::cout << "  - " << model << std::endl;
    }
}

/**
 * Test that verifies all embedding models can be initialized
 */
TEST_F(EmbeddingModelsIntegrationTest, InitializeAllEmbeddingModels)
{
    std::vector<std::string> models = TestUtils::getEmbeddingModelFiles();
    ASSERT_GT(models.size(), 0) << "No embedding models found";

    int successful_inits = 0;
    std::vector<std::string> failed_models;

    for (const auto& model_filename : models)
    {
        std::string model_path = TestUtils::getModelPath(model_filename);
        auto arcface = std::make_shared<ArcfaceRecognizer>(model_path);

        if (arcface && arcface->isReady())
        {
            successful_inits++;
            std::cout << "✓ Successfully initialized: " << model_filename << std::endl;
        }
        else
        {
            failed_models.push_back(model_filename);
            std::cout << "✗ Failed to initialize: " << model_filename << std::endl;
        }
    }

    std::cout << "Successfully initialized " << successful_inits << "/" << models.size() << " models" << std::endl;

    // Print failed models for debugging
    if (!failed_models.empty())
    {
        std::cout << "Failed models:" << std::endl;
        for (const auto& model : failed_models)
        {
            std::cout << "  - " << model << std::endl;
        }
    }

    // Expect at least 50% success rate for initialization
    EXPECT_GE(successful_inits, static_cast<int>(models.size() * 0.5))
        << "Less than 50% of models initialized successfully";
}

/**
 * Main test that runs SwapPipeline with all embedding models
 */
TEST_F(EmbeddingModelsIntegrationTest, SwapPipelineWithAllEmbeddingModels)
{
    std::vector<std::string> models = TestUtils::getEmbeddingModelFiles();
    ASSERT_GT(models.size(), 0) << "No embedding models found";

    int successful_swaps = 0;
    std::vector<std::string> failed_models;

    for (const auto& model_filename : models)
    {
        bool success = testEmbeddingModel(model_filename);

        if (success)
        {
            successful_swaps++;
            std::cout << "✓ Swap successful with: " << model_filename << std::endl;
        }
        else
        {
            failed_models.push_back(model_filename);
            std::cout << "✗ Swap failed with: " << model_filename << std::endl;
        }
    }

    std::cout << "\n=== SUMMARY ===" << std::endl;
    std::cout << "Successful swaps: " << successful_swaps << "/" << models.size() << std::endl;
    std::cout << "Success rate: " << (successful_swaps * 100.0 / models.size()) << "%" << std::endl;

    if (!failed_models.empty())
    {
        std::cout << "\nFailed models:" << std::endl;
        for (const auto& model : failed_models)
        {
            std::cout << "  - " << model << std::endl;
        }
    }

    // Test passes if all swaps are successful
    EXPECT_EQ(successful_swaps, static_cast<int>(models.size()))
        << "Not all embedding models produced successful swaps";
}

/**
 * Performance comparison test across all embedding models
 */
TEST_F(EmbeddingModelsIntegrationTest, PerformanceComparison)
{
    std::vector<std::string> models = TestUtils::getEmbeddingModelFiles();
    ASSERT_GT(models.size(), 0) << "No embedding models found";

    struct ModelPerformance
    {
        std::string model_name;
        long duration_ms;
        bool success;
    };

    std::vector<ModelPerformance> performance_results;

    for (const auto& model_filename : models)
    {
        std::cout << "Performance testing: " << model_filename << std::endl;

        std::string model_path = TestUtils::getModelPath(model_filename);
        auto arcface = std::make_shared<ArcfaceRecognizer>(model_path);

        if (!arcface || !arcface->isReady())
        {
            performance_results.push_back({model_filename, -1, false});
            continue;
        }

        auto swap_pipeline = std::make_unique<SwapPipeline>(inswapper_, arcface, scrfd_);
        auto source_copy = source_image_->deepCopy();
        auto target_copy = target_image_->deepCopy();

        auto start_time = std::chrono::high_resolution_clock::now();
        bool success = swap_pipeline->run(source_copy, target_copy);
        auto end_time = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        performance_results.push_back({model_filename, duration.count(), success});
    }

    // Sort by performance (successful models first, then by duration)
    std::sort(performance_results.begin(), performance_results.end(),
              [](const ModelPerformance& a, const ModelPerformance& b)
              {
                  if (a.success != b.success)
                  {
                      return a.success > b.success;
                  }
                  if (!a.success)
                  {
                      return false;
                  }
                  return a.duration_ms < b.duration_ms;
              });

    std::cout << "\n=== PERFORMANCE RESULTS ===" << std::endl;
    std::cout << "Rank | Model                                    | Time (ms) | Status" << std::endl;
    std::cout << "-----|------------------------------------------|-----------|--------" << std::endl;

    for (size_t i = 0; i < performance_results.size(); ++i)
    {
        const auto& result = performance_results[i];
        std::cout << std::setw(4) << (i + 1) << " | " << std::setw(40) << std::left << result.model_name << " | "
                  << std::setw(9) << std::right << (result.success ? std::to_string(result.duration_ms) : "FAILED")
                  << " | " << (result.success ? "✓" : "✗") << std::endl;
    }

    // At least one model should be successful for the test to pass
    bool any_success = std::any_of(performance_results.begin(), performance_results.end(),
                                   [](const ModelPerformance& p) { return p.success; });

    EXPECT_TRUE(any_success) << "No embedding models produced successful swaps";
}

/**
 * Quality validation test - check that results are different from source
 */
TEST_F(EmbeddingModelsIntegrationTest, QualityValidation)
{
    std::vector<std::string> models = TestUtils::getEmbeddingModelFiles();
    ASSERT_GT(models.size(), 0) << "No embedding models found";

    // Take first successfully initializing model for quality check
    for (const auto& model_filename : models)
    {
        std::string model_path = TestUtils::getModelPath(model_filename);
        auto arcface = std::make_shared<ArcfaceRecognizer>(model_path);

        if (!arcface || !arcface->isReady())
        {
            continue;
        }

        auto swap_pipeline = std::make_unique<SwapPipeline>(inswapper_, arcface, scrfd_);
        auto source_copy = source_image_->deepCopy();
        auto target_copy = target_image_->deepCopy();
        auto original_source = source_image_->deepCopy();

        bool success = swap_pipeline->run(source_copy, target_copy);
        ASSERT_TRUE(success) << "Swap failed for quality validation with model: " << model_filename;

        // Basic quality check: ensure the image was modified
        // Compare a few sample pixels to verify change occurred
        bool pixels_changed = false;
        int sample_points = 10;
        int width = source_copy->info.width;
        int height = source_copy->info.height;

        for (int i = 0; i < sample_points && !pixels_changed; ++i)
        {
            int x = (width / sample_points) * i + width / (sample_points * 2);
            int y = height / 2;

            auto original_pixel = (*original_source)(x, y);
            auto swapped_pixel = (*source_copy)(x, y);

            // Check if any color channel changed significantly
            if (std::abs(original_pixel.r - swapped_pixel.r) > 5 || std::abs(original_pixel.g - swapped_pixel.g) > 5
                || std::abs(original_pixel.b - swapped_pixel.b) > 5)
            {
                pixels_changed = true;
            }
        }

        EXPECT_TRUE(pixels_changed) << "Face swap did not appear to modify the image with model: " << model_filename;

        std::cout << "Quality validation passed for: " << model_filename << std::endl;
        break; // Only test the first working model for quality
    }
}

/**
 * Test that creates a grid visualization of all face swap results
 */
TEST_F(EmbeddingModelsIntegrationTest, CreateResultsGrid)
{
    std::vector<std::string> models = TestUtils::getEmbeddingModelFiles();
    ASSERT_GT(models.size(), 0) << "No embedding models found";

    std::cout << "Creating results grid from " << models.size() << " models plus original and target images..." << std::endl;

    // Helper function to crop face with consistent padding
    auto cropFaceWithPadding = [this](const std::unique_ptr<linuxface::Image>& image, const std::string& name) -> std::unique_ptr<linuxface::Image> {
        std::vector<Face> faces = scrfd_->detect(image);
        if (faces.empty()) {
            std::cerr << "No face detected in image: " << name << std::endl;
            return nullptr;
        }

        const auto& face = faces[0];
        auto face_bbox = face.getBoundingBox().rect;

        // Add 30% padding to the bounding box
        float padding_factor = 0.3f;
        float width_padding = face_bbox.width() * padding_factor;
        float height_padding = face_bbox.height() * padding_factor;

        math_utils::Rect<float> padded_bbox(
            std::max(0.0f, face_bbox.x() - width_padding), 
            std::max(0.0f, face_bbox.y() - height_padding),
            std::min(static_cast<float>(image->info.width), face_bbox.x() + face_bbox.width() + width_padding),
            std::min(static_cast<float>(image->info.height), face_bbox.y() + face_bbox.height() + height_padding)
        );

        return image->crop(padded_bbox);
    };

    // Load all result images and detect faces for cropping
    struct ModelResult
    {
        std::string model_name;
        std::unique_ptr<linuxface::Image> image;
        std::unique_ptr<linuxface::Image> cropped_face;
        math_utils::Rect<float> face_bbox;
    };

    std::vector<ModelResult> results;

    // Add original source image first
    {
        ModelResult source_result;
        source_result.model_name = "Original Source";
        source_result.image = source_image_->deepCopy();
        source_result.cropped_face = cropFaceWithPadding(source_result.image, "original source");
        if (source_result.cropped_face) {
            results.push_back(std::move(source_result));
            std::cout << "Added original source image to grid" << std::endl;
        }
    }

    // Add target image second
    {
        ModelResult target_result;
        target_result.model_name = "Target Image";
        target_result.image = target_image_->deepCopy();
        target_result.cropped_face = cropFaceWithPadding(target_result.image, "target image");
        if (target_result.cropped_face) {
            results.push_back(std::move(target_result));
            std::cout << "Added target image to grid" << std::endl;
        }
    }

    // Process all model results
    for (const auto& model_filename : models)
    {
        ModelResult result;
        result.model_name = model_filename;

        // Load the result image
        std::string result_filename = getResultFilename(model_filename);
        std::string result_path = TestUtils::getTestResultPath("embeddingModels_integration", result_filename);

        result.image = ImageLoader::loadImageFromFile(result_path);
        if (!result.image)
        {
            std::cerr << "Failed to load result image: " << result_path << std::endl;
            continue;
        }

        // Crop the face with padding using helper function
        result.cropped_face = cropFaceWithPadding(result.image, model_filename);
        if (!result.cropped_face)
        {
            std::cerr << "Failed to crop face for model: " << model_filename << std::endl;
            continue;
        }

        results.push_back(std::move(result));
        std::cout << "Processed result for: " << model_filename << std::endl;
    }

    if (results.size() < 2)
    {
        FAIL() << "Need at least 2 results (original + target) for grid creation, got: " << results.size();
    }

    // Calculate grid dimensions (aim for roughly square grid)
    int grid_cols = static_cast<int>(std::ceil(std::sqrt(results.size())));
    int grid_rows = static_cast<int>(std::ceil(static_cast<double>(results.size()) / grid_cols));

    std::cout << "Creating " << grid_cols << "x" << grid_rows << " grid for " << results.size() << " results"
              << std::endl;

    // Find the maximum dimensions for uniformity
    int max_width = 0;
    int max_height = 0;
    for (const auto& result : results)
    {
        max_width = std::max(max_width, static_cast<int>(result.cropped_face->info.width));
        max_height = std::max(max_height, static_cast<int>(result.cropped_face->info.height));
    }

    // Add space for text labels at the bottom
    const int text_height = 30;  // Space for model name text
    const int cell_spacing = 10; // Space between grid cells
    const int cell_width = max_width;
    const int cell_height = max_height + text_height;

    // Calculate total grid image size
    const int grid_width = grid_cols * cell_width + (grid_cols + 1) * cell_spacing;
    const int grid_height = grid_rows * cell_height + (grid_rows + 1) * cell_spacing;

    // Create the grid image with a light gray background
    auto grid_image = std::make_unique<Image>(Pixel(240, 240, 240), grid_width, grid_height);
    grid_image->info.format = ImageFormat::RGB;
    grid_image->info.pixelSizeBytes = 3;

    // Place each result in the grid
    for (size_t i = 0; i < results.size(); ++i)
    {
        int row = static_cast<int>(i) / grid_cols;
        int col = static_cast<int>(i) % grid_cols;

        int cell_x = cell_spacing + col * (cell_width + cell_spacing);
        int cell_y = cell_spacing + row * (cell_height + cell_spacing);

        const auto& result = results[i];

        // Debug output for original and target images
        if (result.model_name == "Original Source" || result.model_name == "Target Image") {
            std::cout << "Placing " << result.model_name << " at position (" << row << "," << col << ") - grid coordinates (" << cell_x << "," << cell_y << ")" << std::endl;
        }

        // Center the cropped face within the cell
        int face_x = cell_x + (cell_width - static_cast<int>(result.cropped_face->info.width)) / 2;
        int face_y = cell_y + (cell_height - text_height - static_cast<int>(result.cropped_face->info.height)) / 2;

        // Add colored border for original and target images before pasting
        if (result.model_name == "Original Source" || result.model_name == "Target Image") {
            Pixel border_color = (result.model_name == "Original Source") ? Pixel(0, 255, 0) : Pixel(255, 0, 0); // Green for source, Red for target
            int border_thickness = 3;
            
            // Add border to the cropped face image itself
            result.cropped_face->drawBorder(border_color, border_thickness);
        }

        // Paste the cropped face (now with border if it's original/target)
        grid_image->pasteAt(*result.cropped_face, face_x, face_y, false);

        // Create model name text (remove .onnx extension and make readable)
        std::string display_name = result.model_name;
        if (display_name.length() >= 5 && display_name.substr(display_name.length() - 5) == ".onnx")
        {
            display_name = display_name.substr(0, display_name.length() - 5);
        }

        // Replace underscores and hyphens with spaces for better readability
        std::replace(display_name.begin(), display_name.end(), '_', ' ');
        std::replace(display_name.begin(), display_name.end(), '-', ' ');

        // Truncate if too long
        if (display_name.length() > 20)
        {
            display_name = display_name.substr(0, 17) + "...";
        }

        // Calculate text position (centered at bottom of cell)
        int text_x = cell_x + cell_width / 2;
        int text_y = cell_y + cell_height - text_height + 10;

        // Use different styling for original images to make them more prominent
        Pixel text_color = Pixel(0, 0, 0);     // Black text
        Pixel bg_color = Pixel(255, 255, 255); // White background
        int text_scale = 2;


        // Draw text with background for better visibility
        drawTextWithBackground(*grid_image, text_x, text_y, display_name, text_color, bg_color, text_scale, true, 2);
    }

    // Add a title to the grid
    std::string title = "Face Swap Results - " + std::to_string(models.size()) + " Models + Original & Target";
    drawTextWithBackground(*grid_image, grid_width / 2, 20, title, Pixel(0, 0, 0), // Black text
                           Pixel(255, 255, 255),                                   // White background
                           3,                                                      // Larger scale for title
                           true,                                                   // Center
                           4);                                                     // More padding

    // Save the grid image
    std::string grid_path = TestUtils::getTestResultPath("embeddingModels_integration", "results_grid.ppm");
    bool saved = grid_image->saveToDisk(grid_path);

    EXPECT_TRUE(saved) << "Failed to save grid image to: " << grid_path;

    if (saved)
    {
        std::cout << "✓ Results grid saved to: " << grid_path << std::endl;
        std::cout << "Grid dimensions: " << grid_width << "x" << grid_height << std::endl;
        std::cout << "Cell size: " << cell_width << "x" << cell_height << std::endl;
        std::cout << "Total items included: " << results.size() << " (" << models.size() << " models + original & target)" << std::endl;
    }
}

/**
 * Comprehensive quality evaluation test using multiple metrics
 * NOTE: Processing time measures only the face swap execution for consistency
 * with PerformanceComparison test, excluding model initialization and quality analysis overhead
 */
TEST_F(EmbeddingModelsIntegrationTest, ComprehensiveQualityEvaluation)
{
    std::vector<std::string> models = TestUtils::getEmbeddingModelFiles();
    ASSERT_GT(models.size(), 0) << "No embedding models found";

    std::cout << "Running comprehensive quality evaluation on " << models.size() << " models..." << std::endl;

    // Load source and target images from test common folder
    auto source_image = ImageLoader::loadImageFromFile(TestUtils::getTestImagePath("single_face.jpeg"));
    auto target_image = ImageLoader::loadImageFromFile(TestUtils::getTestImagePath("single_face_2.jpg"));
    ASSERT_TRUE(source_image != nullptr) << "Failed to load source image from test common folder";
    ASSERT_TRUE(target_image != nullptr) << "Failed to load target image from test common folder";

    // Store quality metrics for each model
    struct ModelQualityResult
    {
        std::string model_name;
        QualityMetrics metrics;
        bool success;
        double processing_time_ms;
    };

    std::vector<ModelQualityResult> quality_results;

    for (const auto& model_filename : models)
    {
        ModelQualityResult result;
        result.model_name = model_filename;
        result.success = false;
        result.processing_time_ms = 0.0; // Initialize timing

        std::cout << "Evaluating quality for: " << model_filename << "..." << std::endl;

        try
        {
            // Initialize recognizer with this embedding model
            std::string model_path = TestUtils::getModelPath(model_filename);
            auto recognizer = std::make_unique<ArcfaceRecognizer>(model_path);

            if (!recognizer->isReady())
            {
                std::cerr << "Failed to initialize recognizer for: " << model_filename << std::endl;
                continue;
            }

            // Initialize swap pipeline with shared_ptr
            auto shared_recognizer = std::shared_ptr<ArcfaceRecognizer>(recognizer.release());
            SwapPipeline pipeline(inswapper_, shared_recognizer, scrfd_);

            // Generate original embeddings
            std::vector<Face> source_faces = scrfd_->detect(source_image);
            std::vector<Face> target_faces = scrfd_->detect(target_image);

            if (source_faces.empty() || target_faces.empty())
            {
                std::cerr << "No faces detected for: " << model_filename << std::endl;
                continue;
            }

            // Get original face embeddings for identity comparison
            auto source_landmarks = source_faces[0].getFivePointLandmarksArcFaceOrder2D();
            auto target_landmarks = target_faces[0].getFivePointLandmarksArcFaceOrder2D();

            std::vector<float> source_embedding, target_embedding, swapped_embedding;

            bool source_result = shared_recognizer->recognize(*source_image, source_landmarks, source_embedding);
            bool target_result = shared_recognizer->recognize(*target_image, target_landmarks, target_embedding);

            if (!source_result || !target_result)
            {
                std::cerr << "Failed to generate embeddings for: " << model_filename << std::endl;
                continue;
            }

            // Perform face swap using pipeline.run method - MEASURE ONLY THIS PART
            auto source_copy = source_image->deepCopy();
            auto target_copy = target_image->deepCopy();

            auto start_time = std::chrono::high_resolution_clock::now();
            bool swap_success = pipeline.run(source_copy, target_copy);
            auto end_time = std::chrono::high_resolution_clock::now();
            result.processing_time_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

            if (!swap_success)
            {
                std::cerr << "Face swap failed for: " << model_filename << std::endl;
                continue;
            }

            // Generate embedding for swapped face to check identity preservation
            std::vector<Face> swapped_faces = scrfd_->detect(source_copy);
            if (!swapped_faces.empty())
            {
                auto swapped_landmarks = swapped_faces[0].getFivePointLandmarksArcFaceOrder2D();
                bool swapped_result = shared_recognizer->recognize(*source_copy, swapped_landmarks, swapped_embedding);

                if (swapped_result)
                {
                    // Crop both target and swapped images to face regions for fair comparison
                    auto target_face_bbox = target_faces[0].getBoundingBox().rect;
                    auto swapped_face_bbox = swapped_faces[0].getBoundingBox().rect;

                    // Add 20% padding to bounding boxes
                    float padding = 0.2f;

                    // Expand target bbox with padding
                    float target_width_pad = target_face_bbox.width() * padding;
                    float target_height_pad = target_face_bbox.height() * padding;
                    math_utils::Rect<float> target_padded_bbox(
                        std::max(0.0f, target_face_bbox.x() - target_width_pad),
                        std::max(0.0f, target_face_bbox.y() - target_height_pad),
                        std::min(static_cast<float>(target_image->info.width),
                                 target_face_bbox.x() + target_face_bbox.width() + target_width_pad),
                        std::min(static_cast<float>(target_image->info.height),
                                 target_face_bbox.y() + target_face_bbox.height() + target_height_pad));

                    // Expand swapped bbox with padding
                    float swapped_width_pad = swapped_face_bbox.width() * padding;
                    float swapped_height_pad = swapped_face_bbox.height() * padding;
                    math_utils::Rect<float> swapped_padded_bbox(
                        std::max(0.0f, swapped_face_bbox.x() - swapped_width_pad),
                        std::max(0.0f, swapped_face_bbox.y() - swapped_height_pad),
                        std::min(static_cast<float>(source_copy->info.width),
                                 swapped_face_bbox.x() + swapped_face_bbox.width() + swapped_width_pad),
                        std::min(static_cast<float>(source_copy->info.height),
                                 swapped_face_bbox.y() + swapped_face_bbox.height() + swapped_height_pad));

                    // Crop face regions
                    auto target_face_crop = target_image->crop(target_padded_bbox);
                    auto swapped_face_crop = source_copy->crop(swapped_padded_bbox);

                    if (target_face_crop && swapped_face_crop)
                    {
                        // Resize both crops to same dimensions (use smaller of the two)
                        int min_width = std::min(target_face_crop->info.width, swapped_face_crop->info.width);
                        int min_height = std::min(target_face_crop->info.height, swapped_face_crop->info.height);

                        // Make dimensions even numbers for better comparison
                        min_width = (min_width / 2) * 2;
                        min_height = (min_height / 2) * 2;

                        auto target_resized = target_face_crop->scale(min_width, min_height);
                        auto swapped_resized = swapped_face_crop->scale(min_width, min_height);

                        if (target_resized && swapped_resized)
                        {
                            // Now evaluate quality metrics on properly sized face crops
                            result.metrics = evaluateImageQuality(*target_resized, *swapped_resized, target_embedding,
                                                                  swapped_embedding);
                            result.success = true;
                        }
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Exception during quality evaluation for " << model_filename << ": " << e.what() << std::endl;
        }

        quality_results.push_back(result);

        if (result.success)
        {
            std::cout << "✓ " << model_filename << " - " << result.metrics.toString()
                      << " (Time: " << result.processing_time_ms << "ms)" << std::endl;
        }
        else
        {
            std::cout << "✗ " << model_filename << " - Quality evaluation failed"
                      << " (Time: " << result.processing_time_ms << "ms)" << std::endl;
        }
    }

    // Generate comprehensive quality report
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "COMPREHENSIVE QUALITY EVALUATION REPORT" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Summary statistics
    size_t successful_evaluations = 0;
    double avg_mse = 0.0, avg_psnr = 0.0, avg_ssim = 0.0, avg_lpips = 0.0, avg_identity = 0.0;
    double best_ssim = -1.0, best_psnr = -1.0, best_identity = -1.0;
    double worst_lpips = 1.0;
    std::string best_ssim_model, best_psnr_model, best_identity_model, best_lpips_model;

    for (const auto& result : quality_results)
    {
        if (result.success)
        {
            successful_evaluations++;
            avg_mse += result.metrics.mse;
            avg_psnr += result.metrics.psnr;
            avg_ssim += result.metrics.ssim;
            avg_lpips += result.metrics.lpips;
            avg_identity += result.metrics.identity_similarity;

            // Track best performers
            if (result.metrics.ssim > best_ssim)
            {
                best_ssim = result.metrics.ssim;
                best_ssim_model = result.model_name;
            }
            if (result.metrics.psnr > best_psnr)
            {
                best_psnr = result.metrics.psnr;
                best_psnr_model = result.model_name;
            }
            if (result.metrics.identity_similarity > best_identity)
            {
                best_identity = result.metrics.identity_similarity;
                best_identity_model = result.model_name;
            }
            if (result.metrics.lpips < worst_lpips)
            {
                worst_lpips = result.metrics.lpips;
                best_lpips_model = result.model_name;
            }
        }
    }

    if (successful_evaluations > 0)
    {
        avg_mse /= successful_evaluations;
        avg_psnr /= successful_evaluations;
        avg_ssim /= successful_evaluations;
        avg_lpips /= successful_evaluations;
        avg_identity /= successful_evaluations;

        std::cout << "\nSUMMARY STATISTICS:" << std::endl;
        std::cout << "Successful evaluations: " << successful_evaluations << "/" << quality_results.size() << std::endl;
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "Average MSE: " << avg_mse << std::endl;
        std::cout << "Average PSNR: " << avg_psnr << " dB" << std::endl;
        std::cout << "Average SSIM: " << avg_ssim << std::endl;
        std::cout << "Average LPIPS: " << avg_lpips << std::endl;
        std::cout << "Average Identity Similarity: " << avg_identity << std::endl;

        std::cout << "\nBEST PERFORMERS:" << std::endl;
        std::cout << "Best SSIM: " << best_ssim_model << " (" << best_ssim << ")" << std::endl;
        std::cout << "Best PSNR: " << best_psnr_model << " (" << best_psnr << " dB)" << std::endl;
        std::cout << "Best Identity: " << best_identity_model << " (" << best_identity << ")" << std::endl;
        std::cout << "Best LPIPS: " << best_lpips_model << " (" << worst_lpips << ")" << std::endl;
    }

    // Detailed results table
    std::cout << "\nDETAILED RESULTS:" << std::endl;
    std::cout << std::left << std::setw(35) << "Model" << std::setw(10) << "MSE" << std::setw(10) << "PSNR"
              << std::setw(10) << "SSIM" << std::setw(10) << "LPIPS" << std::setw(10) << "Identity" << std::setw(10)
              << "Time(ms)" << std::endl;
    std::cout << std::string(95, '-') << std::endl;

    for (const auto& result : quality_results)
    {
        std::cout << std::left << std::setw(35) << result.model_name.substr(0, 34);
        if (result.success)
        {
            std::cout << std::fixed << std::setprecision(2) << std::setw(10) << result.metrics.mse << std::setw(10)
                      << result.metrics.psnr << std::setw(10) << result.metrics.ssim << std::setw(10)
                      << result.metrics.lpips << std::setw(10) << result.metrics.identity_similarity;
        }
        else
        {
            std::cout << std::setw(50) << "FAILED";
        }
        std::cout << std::setw(10) << result.processing_time_ms << std::endl;
    }

    // Save detailed results to file
    std::string results_path = TestUtils::getTestResultPath("embeddingModels_integration", "quality_evaluation.csv");
    std::ofstream csv_file(results_path);
    if (csv_file.is_open())
    {
        csv_file << "Model,MSE,PSNR,SSIM,LPIPS,Identity_Similarity,Processing_Time_MS,Success\n";
        for (const auto& result : quality_results)
        {
            csv_file << result.model_name << ",";
            if (result.success)
            {
                csv_file << result.metrics.mse << "," << result.metrics.psnr << "," << result.metrics.ssim << ","
                         << result.metrics.lpips << "," << result.metrics.identity_similarity << ",";
            }
            else
            {
                csv_file << ",,,,,,";
            }
            csv_file << result.processing_time_ms << "," << (result.success ? "TRUE" : "FALSE") << "\n";
        }
        csv_file.close();
        std::cout << "\n✓ Detailed results saved to: " << results_path << std::endl;
    }

    std::cout << std::string(80, '=') << std::endl;

    // Basic assertions for test validation
    EXPECT_GT(successful_evaluations, 0) << "At least one model should have successful quality evaluation";
    if (successful_evaluations > 0)
    {
        EXPECT_GT(avg_ssim, 0.3) << "Average SSIM should be reasonable for face swaps (>0.3)";
        EXPECT_GT(avg_identity, -0.2) << "Average identity similarity should be reasonable (>-0.2)";
        EXPECT_LT(avg_lpips, 0.1) << "Average LPIPS should indicate good perceptual similarity (<0.1)";
        EXPECT_GT(avg_psnr, 10.0) << "Average PSNR should be reasonable for face swaps (>10dB)";
        EXPECT_LT(avg_mse, 6000.0) << "Average MSE should be reasonable for face swaps (<6000)";
    }
}

/**
 * @brief Parametric test for multiple consecutive self-swaps using all embedding models
 *
 * This test performs multiple iterations of self-swapping on the same image using each
 * available embedding model. Each iteration should theoretically produce the same result,
 * but in practice we might observe cumulative artifacts or quality degradation due to:
 * - Floating point precision errors
 * - Image compression/decompression artifacts
 * - Transformation approximations
 * - Model-specific characteristics
 *
 * This test helps identify stability and quality preservation issues across different
 * embedding models, providing insights into which models maintain consistency over
 * multiple iterations.
 */
TEST_F(EmbeddingModelsIntegrationTest, SelfSwapMultipleIterationsParametric)
{
    std::vector<std::string> models = TestUtils::getEmbeddingModelFiles();
    ASSERT_GT(models.size(), 0) << "No embedding models found";

    const int iterations = 5;
    int successful_models = 0;
    std::vector<std::string> failed_models;

    std::vector<ModelIterationStats> all_model_stats;

    std::cout << "Testing " << models.size() << " embedding models with " << iterations 
              << " self-swap iterations each..." << std::endl;

    for (const auto& model_filename : models)
    {
        std::cout << "\n" << std::string(60, '-') << std::endl;
        std::cout << "Testing model: " << model_filename << std::endl;
        std::cout << std::string(60, '-') << std::endl;

        ModelIterationStats model_stats;
        model_stats.model_name = model_filename;
        model_stats.all_iterations_successful = true;

        try
        {
            // Initialize recognizer with this embedding model
            std::string model_path = TestUtils::getModelPath(model_filename);
            auto recognizer = std::make_shared<ArcfaceRecognizer>(model_path);

            if (!recognizer || !recognizer->isReady())
            {
                std::cerr << "Failed to initialize recognizer for: " << model_filename << std::endl;
                failed_models.push_back(model_filename);
                model_stats.all_iterations_successful = false;
                all_model_stats.push_back(model_stats);
                continue;
            }

            // Create SwapPipeline with this specific embedding model
            auto swap_pipeline = std::make_unique<SwapPipeline>(inswapper_, recognizer, scrfd_);

            // Start with source image for self-swap iterations
            auto working_image = source_image_->deepCopy();
            ASSERT_TRUE(working_image != nullptr) << "Source image copying failed for " << model_filename;

            // Perform multiple self-swap iterations
            for (int i = 0; i < iterations; ++i)
            {
                std::cout << "  Iteration " << (i + 1) << "/" << iterations << "... ";

                // Create a copy to use as target (self-swap)
                auto target_image = working_image->deepCopy();

                auto start_time = std::chrono::high_resolution_clock::now();
                bool result = swap_pipeline->run(working_image, target_image);
                auto end_time = std::chrono::high_resolution_clock::now();

                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                model_stats.execution_times.push_back(duration.count());

                if (!result)
                {
                    std::cout << "FAILED" << std::endl;
                    model_stats.all_iterations_successful = false;
                    break;
                }

                std::cout << "SUCCESS (" << duration.count() << " ms)" << std::endl;

                // Save intermediate results for quality analysis
                std::string model_safe_name = model_filename;
                std::replace(model_safe_name.begin(), model_safe_name.end(), '.', '_');
                std::replace(model_safe_name.begin(), model_safe_name.end(), '-', '_');

                std::string filename = "SelfSwapParametric_" + model_safe_name + 
                                     "_iteration" + std::to_string(i + 1) + "_result.ppm";
                std::string output_path = TestUtils::getTestResultPath("embeddingModels_integration", filename);

                // Save result with model-specific subdirectory structure
                std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());
                bool save_result = working_image->saveToDisk(output_path);

                if (save_result)
                {
                    std::cout << "    Saved to: " << filename << std::endl;
                }
                else
                {
                    std::cerr << "    Failed to save result to: " << output_path << std::endl;
                }
            }

            // Calculate statistics for this model
            if (model_stats.all_iterations_successful && !model_stats.execution_times.empty())
            {
                successful_models++;
                
                model_stats.total_time = 0;
                model_stats.min_time = model_stats.execution_times[0];
                model_stats.max_time = model_stats.execution_times[0];

                for (long long time : model_stats.execution_times)
                {
                    model_stats.total_time += time;
                    model_stats.min_time = std::min(model_stats.min_time, time);
                    model_stats.max_time = std::max(model_stats.max_time, time);
                }

                model_stats.avg_time = static_cast<double>(model_stats.total_time) / model_stats.execution_times.size();

                std::cout << "✓ Model completed successfully:" << std::endl;
                std::cout << "    Total time: " << model_stats.total_time << " ms" << std::endl;
                std::cout << "    Average time: " << model_stats.avg_time << " ms" << std::endl;
                std::cout << "    Min time: " << model_stats.min_time << " ms" << std::endl;
                std::cout << "    Max time: " << model_stats.max_time << " ms" << std::endl;
            }
            else
            {
                failed_models.push_back(model_filename);
                std::cout << "✗ Model failed during iterations" << std::endl;
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Exception during self-swap iterations for " << model_filename << ": " << e.what() << std::endl;
            failed_models.push_back(model_filename);
            model_stats.all_iterations_successful = false;
        }

        all_model_stats.push_back(model_stats);
    }

    // Generate comprehensive report
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "PARAMETRIC SELF-SWAP MULTIPLE ITERATIONS REPORT" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << "Total models tested: " << models.size() << std::endl;
    std::cout << "Successful models: " << successful_models << std::endl;
    std::cout << "Failed models: " << failed_models.size() << std::endl;
    std::cout << "Success rate: " << (successful_models * 100.0 / models.size()) << "%" << std::endl;

    if (!failed_models.empty())
    {
        std::cout << "\nFailed models:" << std::endl;
        for (const auto& model : failed_models)
        {
            std::cout << "  - " << model << std::endl;
        }
    }

    // Performance comparison across successful models
    if (successful_models > 0)
    {
        std::cout << "\nPERFORMANCE COMPARISON:" << std::endl;
        std::cout << std::left << std::setw(40) << "Model" << std::setw(12) << "Avg (ms)" 
                  << std::setw(12) << "Min (ms)" << std::setw(12) << "Max (ms)" 
                  << std::setw(12) << "Total (ms)" << std::endl;
        std::cout << std::string(88, '-') << std::endl;

        // Sort by average time for performance ranking
        std::vector<ModelIterationStats> successful_stats;
        for (const auto& stats : all_model_stats)
        {
            if (stats.all_iterations_successful)
            {
                successful_stats.push_back(stats);
            }
        }

        std::sort(successful_stats.begin(), successful_stats.end(),
                  [](const ModelIterationStats& a, const ModelIterationStats& b)
                  {
                      return a.avg_time < b.avg_time;
                  });

        for (const auto& stats : successful_stats)
        {
            std::string model_display = stats.model_name;
            if (model_display.length() > 37)
            {
                model_display = model_display.substr(0, 34) + "...";
            }

            std::cout << std::left << std::setw(40) << model_display 
                      << std::fixed << std::setprecision(1)
                      << std::setw(12) << stats.avg_time
                      << std::setw(12) << stats.min_time 
                      << std::setw(12) << stats.max_time
                      << std::setw(12) << stats.total_time << std::endl;
        }

        // Calculate overall statistics
        double overall_avg = 0.0;
        long long overall_total = 0;
        int total_iterations = 0;

        for (const auto& stats : successful_stats)
        {
            overall_total += stats.total_time;
            total_iterations += stats.execution_times.size();
        }

        if (total_iterations > 0)
        {
            overall_avg = static_cast<double>(overall_total) / total_iterations;
            std::cout << "\nOVERALL STATISTICS:" << std::endl;
            std::cout << "Total iterations across all models: " << total_iterations << std::endl;
            std::cout << "Overall average time per iteration: " << overall_avg << " ms" << std::endl;
            std::cout << "Total processing time: " << overall_total << " ms" << std::endl;
        }
    }

    // Save detailed results to CSV
    std::string csv_path = TestUtils::getTestResultPath("embeddingModels_integration", "self_swap_iterations_parametric.csv");
    std::ofstream csv_file(csv_path);
    if (csv_file.is_open())
    {
        csv_file << "Model,Success,Iteration,Execution_Time_MS\n";
        for (const auto& stats : all_model_stats)
        {
            if (stats.all_iterations_successful)
            {
                for (size_t i = 0; i < stats.execution_times.size(); ++i)
                {
                    csv_file << stats.model_name << ",TRUE," << (i + 1) << "," << stats.execution_times[i] << "\n";
                }
            }
            else
            {
                csv_file << stats.model_name << ",FALSE,0,0\n";
            }
        }
        csv_file.close();
        std::cout << "\n✓ Detailed results saved to: " << csv_path << std::endl;
    }

    std::cout << std::string(80, '=') << std::endl;

    // Create iteration grid visualization
    std::cout << "\nCreating iteration grid visualization..." << std::endl;
    createParametricIterationGrid(all_model_stats, iterations);

    // Test assertions
    EXPECT_GT(successful_models, 0) << "At least one model should complete all iterations successfully";
    
    // Expect at least 70% success rate across models
    double success_rate = static_cast<double>(successful_models) / models.size();
    EXPECT_GE(success_rate, 0.7) << "At least 70% of models should complete iterations successfully";

    if (successful_models > 0)
    {
        // Calculate average performance across all successful iterations
        double total_avg_time = 0.0;
        for (const auto& stats : all_model_stats)
        {
            if (stats.all_iterations_successful)
            {
                total_avg_time += stats.avg_time;
            }
        }
        double overall_model_avg = total_avg_time / successful_models;

        // Performance should be reasonable (less than 5 seconds per iteration on average)
        EXPECT_LT(overall_model_avg, 5000.0) << "Average iteration time should be reasonable (<5s)";
    }
}
