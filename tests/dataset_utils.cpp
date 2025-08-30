/**
 * DATASET UTILITIES IMPLEMENTATION
 *
 * Centralized utilities for loading and managing test datasets
 */

#include "dataset_utils.h"
#include "test_utils.h"
#include "test_config.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "LinuxFace/imageLoader.h"
#include "config.hpp"

namespace TestUtils::Datasets
{

// ========== WFLW Dataset Utilities ==========

WFLWPaths WFLWPaths::getDefault()
{
    WFLWPaths paths;
    
    // Use test configuration for reliable absolute paths
    // First try the configured build-time path
    std::string source_dir = getSourceDir();
    paths.base_path = source_dir + "/WFLW";
    
    // If that doesn't exist, try config system as fallback
    if (!std::filesystem::exists(paths.base_path)) {
        try {
            std::string config_path = Config::getInstance().getWFLWFolderPath();
            // Remove trailing slash for consistency
            if (!config_path.empty() && config_path.back() == '/') {
                config_path.pop_back();
            }
            if (std::filesystem::exists(config_path)) {
                paths.base_path = config_path;
            }
        } catch (...) {
            // Keep the source_dir based path as fallback
        }
    }
    
    paths.images_path = paths.base_path + "/WFLW_images";
    paths.annotations_path = paths.base_path + "/WFLW_annotations/list_98pt_rect_attr_train_test/list_98pt_rect_attr_test.txt";
    
    return paths;
}

bool WFLWPaths::isValid() const
{
    return std::filesystem::exists(annotations_path) && 
           std::filesystem::exists(images_path);
}

std::unique_ptr<Image> WFLWSample::loadImage() const
{
    return ImageLoader::loadImageFromFile(image_path);
}

bool WFLWSample::isChallengingCondition() const
{
    for (int attr : attributes) {
        if (attr != 0) return true;
    }
    return false;
}

bool SimpleWFLWLoader::loadDataset(int max_samples, bool challenging_only)
{
    paths_ = WFLWPaths::getDefault();
    
    if (!paths_.isValid()) {
        std::cout << "WFLW dataset not found at: " << paths_.annotations_path << std::endl;
        return false;
    }
    
    // Count total available samples first
    int total_available = countValidSamples(paths_.annotations_path);
    if (total_available == 0) {
        std::cout << "No valid samples found in WFLW dataset" << std::endl;
        return false;
    }
    
    // Determine actual max samples to load
    if (max_samples == -1) {
        max_samples = TestUtils::getMaxSamples(total_available);
    } else {
        max_samples = std::min(max_samples, total_available);
    }
    
    // Load samples
    std::ifstream file(paths_.annotations_path);
    if (!file.is_open()) {
        std::cout << "Could not open annotations file: " << paths_.annotations_path << std::endl;
        return false;
    }
    
    std::string line;
    int loaded_count = 0;
    int processed_count = 0;
    
    samples_.clear();
    samples_.reserve(max_samples);
    
    while (std::getline(file, line) && loaded_count < max_samples) {
        WFLWSample sample;
        if (parseLine(line, sample)) {
            processed_count++;
            
            bool should_include = true;
            if (challenging_only && !sample.isChallengingCondition()) {
                should_include = false;
            }
            
            if (should_include) {
                samples_.emplace_back(std::move(sample));
                loaded_count++;
            }
        }
    }
    
    file.close();
    
    std::cout << "Loaded " << loaded_count << " WFLW samples";
    if (challenging_only) {
        std::cout << " (challenging conditions only)";
    }
    std::cout << " from " << total_available << " available" << std::endl;
    
    return loaded_count > 0;
}

const WFLWSample& SimpleWFLWLoader::getSample(int index) const
{
    if (index < 0 || index >= static_cast<int>(samples_.size())) {
        throw std::out_of_range("Sample index out of range");
    }
    return samples_[index];
}

std::vector<int> SimpleWFLWLoader::getSamplesByAttributes(bool normal_pose, 
                                                        bool normal_expression,
                                                        bool normal_illumination,
                                                        bool no_makeup,
                                                        bool no_occlusion,
                                                        bool is_clear) const
{
    std::vector<int> matching_indices;
    
    for (int i = 0; i < static_cast<int>(samples_.size()); ++i) {
        const auto& sample = samples_[i];
        
        if ((normal_pose == sample.isNormalPose()) &&
            (normal_expression == sample.isNormalExpression()) &&
            (normal_illumination == sample.isNormalIllumination()) &&
            (no_makeup == sample.hasNoMakeup()) &&
            (no_occlusion == sample.hasNoOcclusion()) &&
            (is_clear == sample.isClear())) {
            matching_indices.push_back(i);
        }
    }
    
    return matching_indices;
}

bool SimpleWFLWLoader::isWFLWAvailable()
{
    return WFLWPaths::getDefault().isValid();
}

WFLWPaths SimpleWFLWLoader::getWFLWPaths()
{
    return WFLWPaths::getDefault();
}

bool SimpleWFLWLoader::parseLine(const std::string& line, WFLWSample& sample)
{
    std::stringstream ss(line);
    
    // Parse 98 landmarks (x, y pairs)
    sample.landmarks.resize(98);
    for (int i = 0; i < 98; ++i) {
        if (!(ss >> sample.landmarks[i][0] >> sample.landmarks[i][1])) {
            return false;
        }
    }
    
    // Parse bounding box (x_min, y_min, x_max, y_max)
    if (!(ss >> sample.bbox[0] >> sample.bbox[1] >> sample.bbox[2] >> sample.bbox[3])) {
        return false;
    }
    
    // Parse 6 attributes
    for (int i = 0; i < 6; ++i) {
        if (!(ss >> sample.attributes[i])) {
            return false;
        }
    }
    
    // Parse image filename
    if (!(ss >> sample.image_filename)) {
        return false;
    }
    
    // Construct full image path
    sample.image_path = paths_.images_path + "/" + sample.image_filename;
    
    return true;
}

int SimpleWFLWLoader::countValidSamples(const std::string& annotations_path)
{
    std::ifstream file(annotations_path);
    if (!file.is_open()) {
        return 0;
    }
    
    std::string line;
    int count = 0;
    
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string temp;
        int valid_fields = 0;
        
        // Quick validation: check if we can read the expected number of fields
        while (ss >> temp && valid_fields < 203) { // 98*2 + 4 + 6 + 1 = 203 fields
            valid_fields++;
        }
        
        if (valid_fields >= 203) {
            count++;
        }
    }
    
    file.close();
    return count;
}

// ========== Generic Dataset Loading Utilities ==========

std::unique_ptr<Image> ImageSample::loadImage() const
{
    return ImageLoader::loadImageFromFile(path);
}

std::vector<ImageSample> loadImagesFromDirectory(const std::string& directory_path, 
                                               const std::vector<std::string>& extensions,
                                               int max_images)
{
    std::vector<ImageSample> samples;
    
    if (!std::filesystem::exists(directory_path)) {
        std::cout << "Directory not found: " << directory_path << std::endl;
        return samples;
    }
    
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory_path)) {
        if (max_images > 0 && count >= max_images) {
            break;
        }
        
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                ImageSample sample;
                sample.filename = entry.path().filename().string();
                sample.path = entry.path().string();
                samples.emplace_back(std::move(sample));
                count++;
            }
        }
    }
    
    return samples;
}

std::vector<ImageSample> loadTestImages(const std::vector<std::string>& image_names)
{
    std::vector<ImageSample> samples;
    
    for (const auto& name : image_names) {
        ImageSample sample;
        sample.filename = name;
        // Use test configuration for reliable absolute paths
        sample.path = getTestImagePath(name);
        
        if (std::filesystem::exists(sample.path)) {
            samples.emplace_back(std::move(sample));
        } else {
            std::cout << "Test image not found: " << sample.path << std::endl;
        }
    }
    
    return samples;
}

} // namespace TestUtils::Datasets
