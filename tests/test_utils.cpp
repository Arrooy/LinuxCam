/**
 * TEST UTILITIES IMPLEMENTATION
 *
 * Common utility functions for integration tests
 * Provides environment variable handling, test configuration helpers, and path resolution
 */

#include "test_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

// Include the generated test configuration
#include "test_config.h"

namespace TestUtils
{

// ========== Environment Variable Utilities ==========

int getMaxSamples(int max_available)
{
    int samples = getEnvVarInt("WFLW_MAX_SAMPLES", 5);
    return std::max(1, std::min(samples, max_available));
}

int getMaxFacesPerImage()
{
    int faces = getEnvVarInt("WFLW_MAX_FACES_PER_IMAGE", 5);
    return std::max(1, faces);
}

int getEnvVarInt(const char* var_name, int default_value)
{
    const char* env_value = std::getenv(var_name);
    if (env_value == nullptr)
    {
        return default_value;
    }

    try
    {
        return std::stoi(env_value);
    }
    catch (const std::exception& e)
    {
        std::cout << "Warning: Invalid " << var_name << " value '" << env_value << "', using default (" << default_value
                  << ")" << std::endl;
        return default_value;
    }
}

bool getEnvVarBool(const char* var_name)
{
    const char* env_value = std::getenv(var_name);
    if (env_value == nullptr)
    {
        return false;
    }

    std::string value(env_value);
    // Convert to lowercase for case-insensitive comparison
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    
    return value == "1" || value == "true" || value == "yes" || value == "on" || value == "enabled";
}

// ========== Path Resolution Utilities ==========

std::string getTestDataDir()
{
    return ::getTestDataDir();  // Delegate to the generated function
}

std::string getTestImagePath(const std::string& filename)
{
    return ::getTestImagePath(filename);  // Delegate to the generated function
}

std::string getTestResultsDir(const std::string& test_suite_name)
{
    return ::getSourceDir() + "/tests/" + test_suite_name + "/results";
}

std::string getTestResultPath(const std::string& test_suite_name, const std::string& filename)
{
    return getTestResultsDir(test_suite_name) + "/" + filename;
}

std::string getConfigPath()
{
    return ::getConfigFile();  // Delegate to the generated function
}

std::string getModelsDir()
{
    return ::getModelsDirectory();  // Delegate to the generated function
}

std::string getModelPath(const std::string& model_name)
{
    return getModelsDir() + "/" + model_name;
}

} // namespace TestUtils
