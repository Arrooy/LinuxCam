/**
 * TEST UTILITIES
 *
 * Common utility functions for integration tests
 * Provides environment variable handling, test configuration helpers, path resolution, and dataset loading
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

// Include LinuxFace components for complete type definitions
#include "LinuxFace/Image/image.h"

// Forward declare dataset utilities namespace
namespace TestUtils::Datasets { class SimpleWFLWLoader; }

namespace TestUtils
{

// ========== Environment Variable Utilities ==========

/**
 * Get maximum number of samples to process from WFLW_MAX_SAMPLES environment variable
 * @param upper_limit Upper limit for samples to process (caps the result)
 * @return Number of samples to process (default: 5, min: 1, max: upper_limit)
 */
int getMaxSamples(int upper_limit);

/**
 * Get maximum number of faces per image from WFLW_MAX_FACES_PER_IMAGE environment variable
 * @return Number of faces per image to process (default: 5, min: 1)
 */
int getMaxFacesPerImage();

/**
 * Get integer value from environment variable with default fallback
 * @param var_name Environment variable name
 * @param default_value Default value if env var not set or invalid
 * @return Environment variable value or default
 */
int getEnvVarInt(const char* var_name, int default_value);

/**
 * Check if environment variable is set to a truthy value
 * @param var_name Environment variable name
 * @return true if env var is set to "1", "true", "yes", "on", "enabled" (case-insensitive)
 */
bool getEnvVarBool(const char* var_name);

// ========== Path Resolution Utilities ==========

/**
 * Get the absolute path to the test data directory
 * This path is configured at build time by CMake
 * @return Absolute path to tests/common directory
 */
std::string getTestDataDir();

/**
 * Get the absolute path to a test image file
 * @param filename Name of the image file (e.g., "man1.jpeg")
 * @return Absolute path to the test image file
 */
std::string getTestImagePath(const std::string& filename);

/**
 * Get the absolute path to the test results directory
 * @param test_suite_name Name of the test suite (e.g., "swapPipeline_integration")
 * @return Absolute path to the test results directory
 */
std::string getTestResultsDir(const std::string& test_suite_name);

/**
 * Get the absolute path to a test result file
 * @param test_suite_name Name of the test suite (e.g., "swapPipeline_integration") 
 * @param filename Name of the result file (e.g., "result.ppm")
 * @return Absolute path to the test result file
 */
std::string getTestResultPath(const std::string& test_suite_name, const std::string& filename);

/**
 * Get the absolute path to the project configuration file
 * @return Absolute path to config.yaml
 */
std::string getConfigPath();

/**
 * Get the absolute path to the models directory
 * @return Absolute path to models directory
 */
std::string getModelsDir();

/**
 * Get the absolute path to a specific ONNX model file
 * @param model_name Name of the model file (e.g., "inswapper_128.onnx")
 * @return Absolute path to the model file
 */
std::string getModelPath(const std::string& model_name);

// ========== Model Discovery Utilities ==========

/**
 * Get all embedding model files from the models directory
 * Filters based on known embedding model patterns:
 * - focal-arcface* (all variants)
 * - arcface* (all variants)
 * - cavaface* (all variants)
 * - CurricularFace
 * - glint* (all variants)
 * - ms1mv3_arcface*
 * @return Vector of embedding model filenames (sorted)
 */
std::vector<std::string> getEmbeddingModelFiles();

// ========== Grid Visualization Utilities ==========

/**
 * Structure representing a single cell in a grid visualization
 */
struct GridCell
{
    std::unique_ptr<linuxface::Image> image;
    std::string label;
    bool highlight = false;  // Whether to add a colored border
    linuxface::Pixel highlight_color;  // Border color if highlighted
    
    // Default constructor
    GridCell();
    
    // Move constructor and assignment
    GridCell(GridCell&&) = default;
    GridCell& operator=(GridCell&&) = default;
    
    // Disable copy to avoid issues with unique_ptr
    GridCell(const GridCell&) = delete;
    GridCell& operator=(const GridCell&) = delete;
};

/**
 * Create a grid visualization from a collection of images
 * @param cells Vector of GridCell objects containing images and labels
 * @param grid_rows Number of rows in the grid (0 for auto-calculate)
 * @param grid_cols Number of columns in the grid (0 for auto-calculate)
 * @param cell_spacing Spacing between grid cells in pixels
 * @param background_color Background color for the grid
 * @param title Optional title to add at the top of the grid
 * @return Unique pointer to the generated grid image
 */
std::unique_ptr<linuxface::Image> createGridVisualization(
    const std::vector<GridCell>& cells,
    int grid_rows = 0,
    int grid_cols = 0,
    int cell_spacing = 10,
    const linuxface::Pixel& background_color = linuxface::Pixel(240, 240, 240),
    const std::string& title = ""
);

} // namespace TestUtils
