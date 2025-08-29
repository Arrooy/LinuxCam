#include "wflw_loader.h"
#include "../test_utils.h"

#include "LinuxFace/common.h"
#include "config.hpp"

namespace linuxface::test
{

WFLWLoader::WFLWLoader(const std::string& data_file_path, const int max_samples)
{
    load_examples_from_file(data_file_path, false, max_samples);
}

WFLWLoader::WFLWLoader(const std::string& data_file_path, bool load_challenging_only, const int max_samples)
{
    load_examples_from_file(data_file_path, load_challenging_only, max_samples);
}

void WFLWLoader::load_examples_from_file(const std::string& data_file_path, bool load_challenging_only, const int max_samples)
{
    std::ifstream file(data_file_path);
    if (!file.is_open())
    {
    common::logError("Could not open data file: %s", data_file_path.c_str());
        return;
    }

    std::string line;
    int num_examples = 0;
    int total_processed = 0;
    
    while (std::getline(file, line))
    {
        WFLWExample example;
        if (parse_line(line, example))
        {
            total_processed++;
            
            bool should_include = true;
            if (load_challenging_only)
            {
                // Check if example has ANY challenging condition (any attribute != 0)
                bool has_challenging_condition = false;
                for (int attr : example.attributes)
                {
                    if (attr != 0)
                    {
                        has_challenging_condition = true;
                        break;
                    }
                }
                should_include = has_challenging_condition;
            }
            
            if (should_include)
            {
                examples_.emplace_back(std::move(example));
                ++num_examples;
                if (max_samples > 0 && num_examples >= max_samples)
                {
                    break;
                }
            }
        }
        else
        {
            common::logWarn("Skipping invalid line: %s", line.c_str());
        }
    }
    file.close();
    
    if (load_challenging_only)
    {
        common::logInfo("Prepared %d challenging condition examples from %d total processed samples (images will be loaded on-demand)", 
                        num_examples, total_processed);
    }
    else
    {
        common::logInfo("Prepared %d examples from %d total processed samples (images will be loaded on-demand)", 
                        num_examples, total_processed);
    }
}

bool WFLWLoader::load_example(int index, WFLWExample& example) const
{
    if (index < 0 || static_cast<size_t>(index) >= examples_.size())
    {
        common::logError("Invalid example index: %d", index);
        return false;
    }

    // Copy the basic data
    example.landmarks = examples_[index].landmarks;
    example.bounding_box = examples_[index].bounding_box;
    example.attributes = examples_[index].attributes;
    example.image_name = examples_[index].image_name;
    example.image_path = examples_[index].image_path;

    // Load the image on-demand if not already loaded
    if (!examples_[index].image)
    {
        // Load the image from the stored path
        examples_[index].image = ImageLoader::loadImageFromFile(examples_[index].image_path);
        if (!examples_[index].image)
        {
            common::log_error("Could not load image: %s", examples_[index].image_path.c_str());
            return false;
        }
        common::log_info("Loaded image on-demand: %s", examples_[index].image_path.c_str());
    }

    // Create a deep copy of the image for the caller
    example.image = examples_[index].image->deepCopy();

    return true;
}

bool WFLWLoader::parse_line(const std::string& line, WFLWExample& example)
{
    auto ss = std::stringstream(line);

    // Parse 98 landmarks (x, y)
    for (int i = 0; i < 98; ++i)
    {
        double x = 0.0;
        double y = 0.0;
        if (!(ss >> x >> y))
        {
            return false;
        }
        example.landmarks.emplace_back(x, y);
    }

    // Parse bounding box (x_min, y_min, x_max, y_max)
    double x_min = 0.0;
    double y_min = 0.0;
    double x_max = 0.0;
    double y_max = 0.0;
    if (!(ss >> x_min >> y_min >> x_max >> y_max))
    {
        return false;
    }
    example.bounding_box = math_utils::Rect<double>(x_min, y_min, x_max, y_max);

    // Parse 6 attributes
    for (auto& attr : example.attributes)
    {
        if (!(ss >> attr))
        {
            return false;
        }
    }

    // Parse image name
    if (!(ss >> example.image_name))
    {
        return false;
    }

    // Construct the full image path for lazy loading (don't load the image yet)
    const auto& base_image_dir = Config::getInstance().getWFLWFolderPath();

    // Normalize the path by ensuring clean directory separators
    std::string normalized_base = base_image_dir;

    // Remove any trailing slashes first
    while (!normalized_base.empty() && normalized_base.back() == '/')
    {
        normalized_base.pop_back();
    }

    // Then normalize any double slashes
    size_t pos = 0;
    while ((pos = normalized_base.find("//", pos)) != std::string::npos)
    {
        normalized_base.replace(pos, 2, "/");
        pos += 1;
    }

    example.image_path = normalized_base + "/WFLW_images/" + example.image_name;
    example.image = nullptr; // Will be loaded on-demand

    return true;
}

std::vector<int> WFLWLoader::getExamplesByAttribute(bool normal_pose, bool normal_expression, bool normal_illumination,
                                                    bool no_makeup, bool no_occlusion, bool is_clear, int max_results) const
{
    std::vector<int> matching_indices;

    // If max_results is not specified, check environment variable
    if (max_results == -1)
    {
        max_results = TestUtils::getEnvVarInt("WFLW_MAX_SAMPLES", -1);
    }

    for (size_t i = 0; i < examples_.size(); ++i)
    {
        const auto& example = examples_[i];

        if ((normal_pose == example.isNormalPose()) && (normal_expression == example.isNormalExpression())
            && (normal_illumination == example.isNormalIllumination()) && (no_makeup == example.hasNoMakeup())
            && (no_occlusion == example.hasNoOcclusion()) && (is_clear == example.isClear()))
        {
            matching_indices.push_back(static_cast<int>(i));
            
            // Stop collecting if we reached the limit (unless unlimited)
            if (max_results > 0 && static_cast<int>(matching_indices.size()) >= max_results)
            {
                break;
            }
        }
    }

    return matching_indices;
}

} // namespace linuxface::test
