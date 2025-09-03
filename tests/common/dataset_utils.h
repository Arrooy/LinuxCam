/**
 * DATASET UTILITIES
 *
 * Centralized utilities for loading and managing test datasets
 * Provides standardized interfaces for WFLW and other facial landmark datasets
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/math_utils.h"

using namespace linuxface; // Use the linuxface namespace

namespace TestUtils::Datasets
{

// ========== WFLW Dataset Utilities ==========

/**
 * Standard WFLW dataset paths configuration
 */
struct WFLWPaths
{
    std::string base_path;
    std::string images_path;
    std::string annotations_path;
    
    static WFLWPaths getDefault();
    bool isValid() const;
};

/**
 * Simple WFLW sample for basic validation tests
 */
struct WFLWSample
{
    std::string image_filename;
    std::string image_path;
    std::vector<std::array<double, 2>> landmarks; // 98 landmarks as [x, y] pairs
    std::array<double, 4> bbox;                   // [x_min, y_min, x_max, y_max]
    std::array<int, 6> attributes;                // pose, expression, illumination, makeup, occlusion, blur
    
    // Load image on-demand
    std::unique_ptr<Image> loadImage() const;
    
    // Attribute helpers
    bool isNormalPose() const { return attributes[0] == 0; }
    bool isNormalExpression() const { return attributes[1] == 0; }
    bool isNormalIllumination() const { return attributes[2] == 0; }
    bool hasNoMakeup() const { return attributes[3] == 0; }
    bool hasNoOcclusion() const { return attributes[4] == 0; }
    bool isClear() const { return attributes[5] == 0; }
    bool isChallengingCondition() const;
};

/**
 * Simplified WFLW dataset loader for basic validation tests
 * Lighter weight alternative to full WFLWLoader for simple test cases
 */
class SimpleWFLWLoader
{
public:
    /**
     * Load WFLW samples with automatic path detection and sample limiting
     * @param max_samples Maximum samples to load (uses getMaxSamples() if -1)
     * @param challenging_only Load only challenging condition samples
     * @return true if dataset found and loaded successfully
     */
    bool loadDataset(int max_samples = -1, bool challenging_only = false);
    
    /**
     * Get total number of loaded samples
     */
    int getSampleCount() const { return static_cast<int>(samples_.size()); }
    
    /**
     * Get sample by index
     */
    const WFLWSample& getSample(int index) const;
    
    /**
     * Get all samples matching specific attributes
     */
    std::vector<int> getSamplesByAttributes(bool normal_pose = true, 
                                          bool normal_expression = true,
                                          bool normal_illumination = true,
                                          bool no_makeup = true,
                                          bool no_occlusion = true,
                                          bool is_clear = true) const;
    
    /**
     * Check if WFLW dataset is available
     */
    static bool isWFLWAvailable();
    
    /**
     * Get WFLW dataset paths
     */
    static WFLWPaths getWFLWPaths();

private:
    std::vector<WFLWSample> samples_;
    WFLWPaths paths_;
    
    bool parseLine(const std::string& line, WFLWSample& sample);
    int countValidSamples(const std::string& annotations_path);
};

// ========== Generic Dataset Loading Utilities ==========

/**
 * Generic image sample for basic tests
 */
struct ImageSample
{
    std::string filename;
    std::string path;
    
    std::unique_ptr<Image> loadImage() const;
};

/**
 * Load images from a directory for basic testing
 */
std::vector<ImageSample> loadImagesFromDirectory(const std::string& directory_path, 
                                               const std::vector<std::string>& extensions = {".jpg", ".jpeg", ".png"},
                                               int max_images = -1);

/**
 * Load specific test images by name
 */
std::vector<ImageSample> loadTestImages(const std::vector<std::string>& image_names);

} // namespace TestUtils::Datasets
