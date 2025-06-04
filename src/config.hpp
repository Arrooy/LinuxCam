#ifndef CONFIG_H
#define CONFIG_H

#include "FunnyFace/camera.h"
#include "FunnyFace/common.h"
#include "yaml-cpp/yaml.h"

namespace funnyface
{

// Configuration structures
struct ExternalImages
{
    std::string folder;
};

// Class that reads configuration from a yaml file and provides access to it.
class Config
{
    Config(const char* filename)
    {
        try
        {
            config_ = YAML::LoadFile(filename);
        }
        catch (const YAML::Exception& e)
        {
            common::log_error("Failed to load config file: %s", e.what());
        }
    }

    bool validateAndLoadInputCamera()
    {
        if (!config_["input_camera"])
        {
            common::log_error("Missing input_camera section in config");
            return false;
        }

        auto input = config_["input_camera"];
        if (!validateInputCameraFields(input))
        {
            return false;
        }

        loadInputCameraData(input);
        return true;
    }

    bool validateInputCameraFields(const YAML::Node& input)
    {
        const std::vector<std::string> requiredFields = {"name", "path", "width", "height", "buffer_count"};

        for (const auto& field : requiredFields)
        {
            if (!input[field])
            {
                common::log_error("Missing required field '%s' in input_camera section", field.c_str());
                return false;
            }
        }
        return true;
    }

    void loadInputCameraData(const YAML::Node& input)
    {
        input_camera_.name = input["name"].as<std::string>();
        input_camera_.device_path = input["path"].as<std::string>();
        input_camera_.width = input["width"].as<unsigned int>();
        input_camera_.height = input["height"].as<unsigned int>();
        input_camera_.buffer_count = input["buffer_count"].as<unsigned int>();
    }

    bool validateAndLoadOutputCamera()
    {
        if (!config_["output_camera"])
        {
            common::log_error("Missing output_camera section in config");
            return false;
        }

        auto output = config_["output_camera"];
        if (!validateOutputCameraFields(output))
        {
            return false;
        }

        return loadOutputCameraData(output);
    }

    bool validateOutputCameraFields(const YAML::Node& output)
    {
        const std::vector<std::string> requiredFields = {"name", "path", "width", "height", "subsampling"};

        for (const auto& field : requiredFields)
        {
            if (!output[field])
            {
                common::log_error("Missing required field '%s' in output_camera section", field.c_str());
                return false;
            }
        }
        return true;
    }

    bool loadOutputCameraData(const YAML::Node& output)
    {
        output_camera_.name = output["name"].as<std::string>();
        output_camera_.device_path = output["path"].as<std::string>();
        output_camera_.width = output["width"].as<unsigned int>();
        output_camera_.height = output["height"].as<unsigned int>();

        return parseSubsamplingValue(output["subsampling"].as<std::string>());
    }

    bool parseSubsamplingValue(const std::string& subsample)
    {
        static const std::map<std::string, TJSAMP> subsamplingMap = {
            {"444",  TJSAMP_444 },
            {"422",  TJSAMP_422 },
            {"420",  TJSAMP_420 },
            {"gray", TJSAMP_GRAY},
            {"440",  TJSAMP_440 },
            {"411",  TJSAMP_411 }
        };

        auto it = subsamplingMap.find(subsample);
        if (it != subsamplingMap.end())
        {
            output_camera_.subsampling = it->second;
            return true;
        }

        common::log_error("Invalid subsampling value: %s", subsample.c_str());
        return false;
    }

    bool validateAndLoadExternalImages()
    {
        if (!config_["external_images"])
        {
            common::log_error("Missing external_images section in config");
            return false;
        }

        auto images = config_["external_images"];
        if (!images["folder"])
        {
            common::log_error("Missing folder field in external_images section");
            return false;
        }

        external_images_.folder = images["folder"].as<std::string>();
        return true;
    }

  public:
    static Config& getInstance(const char* filename = "config.yaml")
    {
        static Config instance(filename);
        return instance;
    }

    bool loadConfiguration()
    {
        return validateAndLoadInputCamera() && validateAndLoadOutputCamera() && validateAndLoadExternalImages();
    }

    // Get configuration sections
    CapturingDevice getInputCamera() const { return input_camera_; }
    CapturingDevice getOutputCamera() const { return output_camera_; }
    ExternalImages getExternalImages() const { return external_images_; }

    // Get a value from the config
    template <typename T>
    T get(const std::string& key, const T& defaultValue = T()) const
    {
        if (config_[key])
        {
            return config_[key].as<T>();
        }
        return defaultValue;
    }

  private:
    YAML::Node config_;
    CapturingDevice input_camera_;
    CapturingDevice output_camera_;
    ExternalImages external_images_;

    // String storage for const char* pointers in CapturingDevice
    std::string input_name_;
    std::string input_path_;
    std::string output_name_;
    std::string output_path_;
};

} // namespace funnyface
#endif // CONFIG_H
