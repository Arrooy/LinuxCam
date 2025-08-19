/**
 * TEST UTILITIES
 *
 * Common utility functions for WFLW integration tests
 * Provides environment variable handling and test configuration helpers
 */

#pragma once

namespace TestUtils
{

/**
 * Get maximum number of samples to process from WFLW_MAX_SAMPLES environment variable
 * @param max_available Maximum samples available in the dataset
 * @return Number of samples to process (default: 5, min: 1, max: max_available)
 */
int getMaxSamples(int max_available);

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

} // namespace TestUtils
