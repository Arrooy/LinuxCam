#ifndef CONFIG_H
#define CONFIG_H

#include "LinuxFace/cameraManager.h"
#include "LinuxFace/common.h"
#include "yaml-cpp/yaml.h"

namespace linuxface
{

// Configuration structures
struct ExternalData
{
    std::string mediaFolderPath;
    std::string modelFolderPath;
    std::string WFLWFolderPath;
    bool preLoading{false};
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
    explicit Config(const char* filename)
    {
        try
        {
            config_ = YAML::LoadFile(filename);
        }
        catch (const YAML::Exception& e)
        {
            common::logError("Failed to load config file: %s", e.what());
            config_ = YAML::Node(); // Set to null node on failure
        }
    }

    bool validateAndLoadInputCameras()
    {
    if (!this->config_["input_cameras"] || !this->config_["input_cameras"].IsSequence() || this->config_["input_cameras"].size() == 0)
        {
            common::logError("Missing or empty input_camera section in config");
            return false;
        }
    auto inputs = this->config_["input_cameras"];
        for (const auto& input : inputs)
        {
            if (!validateInputCameraFields(input))
            {
                return false;
            }
            this->loadInputCameraData(input);
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
                common::logError("Missing required field '%s' in input_camera section", field.c_str());
                return false;
            }
        }
        return true;
    }

    void loadInputCameraData(const YAML::Node& input)
    {
    WebcamDevice inputCamera;
    inputCamera.is_input = true;
    inputCamera.name = input["name"].as<std::string>();
    inputCamera.device_path = input["path"].as<std::string>();
    inputCamera.width = input["width"].as<unsigned int>();
    inputCamera.height = input["height"].as<unsigned int>();
    inputCamera.buffer_count = input["buffer_count"].as<unsigned int>();
    this->cameras_.push_back(inputCamera);
    }

    bool validateAndLoadOutputCameras()
    {
    if (!this->config_["output_cameras"])
        {
            common::logError("Missing output_camera section in config");
            return false;
        }

    auto outputs = this->config_["output_cameras"];
        for (const auto& output : outputs)
        {
            if (!validateOutputCameraFields(output))
            {
                return false;
            }
            this->loadOutputCameraData(output);
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
                common::logError("Missing required field '%s' in output_camera section", field.c_str());
                return false;
            }
        }
        return true;
    }

    bool loadOutputCameraData(const YAML::Node& output)
    {
    WebcamDevice outputCamera;
        outputCamera.is_input = false;
        outputCamera.name = output["name"].as<std::string>();
        outputCamera.device_path = output["path"].as<std::string>();
        outputCamera.width = output["width"].as<unsigned int>();
        outputCamera.height = output["height"].as<unsigned int>();
        const bool result =
            this->parseSubsamplingValue(output["subsampling"].as<std::string>(), outputCamera.subsampling);
        if (result)
        {
            this->cameras_.push_back(outputCamera);
        }
        return result;
    }

    bool parseSubsamplingValue(const std::string& subsample, TJSAMP& outputSubsampling)
    {
        static const std::map<std::string, TJSAMP> SUBSAMPLING_MAP = {
            {"444",  TJSAMP_444 },
            {"422",  TJSAMP_422 },
            {"420",  TJSAMP_420 },
            {"gray", TJSAMP_GRAY},
            {"440",  TJSAMP_440 },
            {"411",  TJSAMP_411 }
        };

        auto it = SUBSAMPLING_MAP.find(subsample);
        if (it != SUBSAMPLING_MAP.end())
        {
            outputSubsampling = it->second;
            return true;
        }

        common::logError("Invalid subsampling value: %s", subsample.c_str());
        return false;
    }

    bool validateAndLoadExternalImages()
    {
        if (!config_["external_data"])
        {
            common::logError("Missing external_data section in config");
            return false;
        }

        auto images = config_["external_data"];
        if (!images["media_folder_path"])
        {
            common::logError("Missing media_folder_path field in external_data section");
            return false;
        }
        external_data_.mediaFolderPath = normalizePath(images["media_folder_path"].as<std::string>());

        if (!images["models_folder_path"])
        {
            common::logError("Missing models_folder_path field in external_data section");
            return false;
        }

        external_data_.modelFolderPath = normalizePath(images["models_folder_path"].as<std::string>());

        if (!images["WFLW_folder_path"])
        {
            common::logError("Missing WFLW_folder_path field in external_data section");
            return false;
        }

        external_data_.WFLWFolderPath = images["WFLW_folder_path"].as<std::string>() + "/";

        if (!images["preload_content"])
        {
            common::logError("Missing preload_content field in external_data section");
            return false;
        }
        external_data_.preLoading = images["preload_content"].as<bool>();
        return true;
    }

    bool validateAndLoadWindowConfig()
    {
        if (!config_["window"])
        {
            common::logError("Missing window section in config");
            return false;
        }

        auto window = config_["window"];
        if (!window["title"])
        {
            common::logError("Missing title field in window section");
            return false;
        }
        windowTitle_ = config_["window"]["title"].as<std::string>();

        if (!window["width"])
        {
            common::logError("Missing width field in window section");
            return false;
        }
        windowWidth_ = window["width"].as<int>();

        if (!window["height"])
        {
            common::logError("Missing height field in window section");
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

    // Method to reload configuration from a different file (for testing)
    bool reloadFromFile(const char* filename)
    {
        try
        {
            // Clear existing data
            cameras_.clear();
            external_data_ = ExternalData{};
            enableGPU_ = true;
            windowWidth_ = 0;
            windowHeight_ = 0;
            windowTitle_.clear();
            config_ = YAML::LoadFile(filename);
            return true;
        }
        catch (const YAML::Exception& e)
        {
            common::logError("Failed to reload config file: %s", e.what());
            return false;
        }
    }

    bool loadConfiguration()
    {
        // Fail if config is empty (file missing or invalid)
        if (!config_ || config_.IsNull())
        {
            common::logError("Config: YAML file missing or invalid");
            return false;
        }
        if (config_["enable_gpu"])
        {
            enableGPU_ = config_["enable_gpu"].as<bool>();
        }
        return validateAndLoadInputCameras() && validateAndLoadOutputCameras() && validateAndLoadExternalImages()
               && validateAndLoadWindowConfig();
    }

    // Get configuration sections
    std::vector<WebcamDevice> getWebcams() const { return cameras_; }
    std::string getMediaFolderPath() const { return external_data_.mediaFolderPath; }
    std::string getModelFolderPath() const { return external_data_.modelFolderPath; }
    std::string getWFLWFolderPath() const { return external_data_.WFLWFolderPath; }

    bool preloadExternalContent() const { return external_data_.preLoading; }
    bool isGPUEnabled() const { return enableGPU_; }
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
    ExternalData external_data_;

    bool enableGPU_{true};

    unsigned int windowWidth_{};
    unsigned int windowHeight_{};
    std::string windowTitle_;

    // Helper function to ensure path ends with exactly one "/"
    static std::string normalizePath(const std::string& path)
    {
        if (path.empty())
        {
            return "/";
        }

        std::string normalized = path;
        // Remove trailing slashes
        while (!normalized.empty() && normalized.back() == '/')
        {
            normalized.pop_back();
        }
        // Add exactly one slash
        normalized += "/";
        return normalized;
    }
};

} // namespace linuxface
#endif // CONFIG_H
