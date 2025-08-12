#include "wflw_loader.h"

#include "LinuxFace/common.h"
#include "config.hpp"

namespace linuxface::test
{

WFLWLoader::WFLWLoader(const std::string& data_file_path, const int max_samples)
{
    std::ifstream file(data_file_path);
    if (!file.is_open())
    {
        common::log_error("Could not open data file: %s", data_file_path.c_str());
        return;
    }

    std::string line;
    int num_examples = 0;
    while (std::getline(file, line))
    {
        WFLWExample example;
        if (parse_line(line, example))
        {
            examples_.emplace_back(std::move(example));
            ++num_examples;
            if (max_samples > 0 && num_examples >= max_samples)
            {
                break;
            }
        }
        else
        {
            common::log_warn("Skipping invalid line: %s", line.c_str());
        }
    }
    file.close();
}

bool WFLWLoader::load_example(int index, WFLWExample& example) const
{
    if (index < 0 || static_cast<size_t>(index) >= examples_.size())
    {
        common::log_error("Invalid example index: %lu", index);
        return false;
    }

    // Copy the basic data
    example.landmarks = examples_[index].landmarks;
    example.bounding_box = examples_[index].bounding_box;
    example.attributes = examples_[index].attributes;
    example.image_name = examples_[index].image_name;

    // Create a deep copy of the image
    if (examples_[index].image)
    {
        example.image = examples_[index].image->deepCopy();
    }
    else
    {
        example.image = nullptr;
    }

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

    // Load the image using ImageLoader
    const auto& base_image_dir = Config::getInstance().getWFLWFolderPath();
    const auto image_path = base_image_dir + "/WFLW_images/" + example.image_name;
    example.image = ImageLoader::loadImageFromFile(image_path);
    if (!example.image)
    {
        common::log_error("Could not load image: %s", image_path.c_str());
        return false;
    }
    common::log_info("Loaded image: %s", image_path.c_str());
    return true;
}

std::vector<int> WFLWLoader::getExamplesByAttribute(bool normal_pose, bool normal_expression, bool normal_illumination,
                                                    bool no_makeup, bool no_occlusion, bool is_clear) const
{
    std::vector<int> matching_indices;

    for (size_t i = 0; i < examples_.size(); ++i)
    {
        const auto& example = examples_[i];

        if ((normal_pose == example.isNormalPose()) && (normal_expression == example.isNormalExpression())
            && (normal_illumination == example.isNormalIllumination()) && (no_makeup == example.hasNoMakeup())
            && (no_occlusion == example.hasNoOcclusion()) && (is_clear == example.isClear()))
        {
            matching_indices.push_back(static_cast<int>(i));
        }
    }

    return matching_indices;
}

} // namespace linuxface::test
