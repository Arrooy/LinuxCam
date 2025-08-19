/**
 * TEST UTILITIES IMPLEMENTATION
 *
 * Common utility functions for WFLW integration tests
 */

#include "test_utils.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace TestUtils
{

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

} // namespace TestUtils
