#ifndef CONFIG_H
#define CONFIG_H

#include "FunnyFace/cameraManager.h"
#include "FunnyFace/common.h"
#include "yaml-cpp/yaml.h"

namespace funnyface
{

// Configuration structures
struct ExternalImages
{
    std::string folder;
};

struct WebcamDevice
{
    std::string name;
    std::string device_path;
    bool is_input{false};
    unsigned int width = 0u;
    unsigned int height = 0u;
    unsigned int buffer_count = 0u;
    TJSAMP subsampling = TJSAMP_420; // Default subsampling
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

    bool validateAndLoadInputCameras()
    {
        if (!config_["input_cameras"])
        {
            common::log_error("Missing input_camera section in config");
            return false;
        }

        auto inputs = config_["input_cameras"];
        for (const auto& input : inputs)
        {
            if (!validateInputCameraFields(input))
            {
                return false;
            }

            loadInputCameraData(input);
        }
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
        WebcamDevice input_camera_;
        input_camera_.is_input = true;
        input_camera_.name = input["name"].as<std::string>();
        input_camera_.device_path = input["path"].as<std::string>();
        input_camera_.width = input["width"].as<unsigned int>();
        input_camera_.height = input["height"].as<unsigned int>();
        input_camera_.buffer_count = input["buffer_count"].as<unsigned int>();
        cameras_.push_back(input_camera_);
    }

    bool validateAndLoadOutputCameras()
    {
        if (!config_["output_cameras"])
        {
            common::log_error("Missing output_camera section in config");
            return false;
        }

        auto outputs = config_["output_cameras"];
        for (const auto& output : outputs)
        {
            if (!validateOutputCameraFields(output))
            {
                return false;
            }
            loadOutputCameraData(output);
        }
        return true;
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
        WebcamDevice output_camera_;
        output_camera_.is_input = false;
        output_camera_.name = output["name"].as<std::string>();
        output_camera_.device_path = output["path"].as<std::string>();
        output_camera_.width = output["width"].as<unsigned int>();
        output_camera_.height = output["height"].as<unsigned int>();
        bool result = parseSubsamplingValue(output["subsampling"].as<std::string>(), output_camera_.subsampling);
        if (result)
        {
            cameras_.push_back(output_camera_);
        }
        return result;
    }

    bool parseSubsamplingValue(const std::string& subsample, TJSAMP& output_subsampling)
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
            output_subsampling = it->second;
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

    bool validateAndLoadWindowConfig()
    {
        if (!config_["window"])
        {
            common::log_error("Missing window section in config");
            return false;
        }

        auto window = config_["window"];
        if (!window["title"])
        {
            common::log_error("Missing title field in window section");
            return false;
        }
        windowTitle_ = config_["window"]["title"].as<std::string>();

        if (!window["width"])
        {
            common::log_error("Missing width field in window section");
            return false;
        }
        windowWidth_ = window["width"].as<int>();

        if (!window["height"])
        {
            common::log_error("Missing height field in window section");
            return false;
        }
        windowHeight_ = window["height"].as<int>();
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
        return validateAndLoadInputCameras() && validateAndLoadOutputCameras() && validateAndLoadExternalImages() && validateAndLoadWindowConfig();
    }

    // Get configuration sections
    std::vector<WebcamDevice> getWebcams() const { return cameras_; }
    ExternalImages getExternalImages() const { return external_images_; }

    void getWindowSize(int& width, int& height) const
    {
        width = windowWidth_;
        height = windowHeight_;
    }

    std::string getWindowTitle() const { return windowTitle_; }

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
    std::vector<WebcamDevice> cameras_;
    ExternalImages external_images_;

    unsigned int windowWidth_;
    unsigned int windowHeight_;
    std::string windowTitle_;
};

} // namespace funnyface
#endif // CONFIG_H
