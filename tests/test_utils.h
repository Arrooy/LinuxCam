/**
 * TEST UTILITIES
 *
 * Common utility functions for integration tests
 * Provides environment variable handling, test configuration helpers, path resolution, and dataset loading
 */

#pragma once

#include <string>

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

} // namespace TestUtils
