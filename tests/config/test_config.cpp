#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "../../src/config.hpp"

using namespace linuxface;

class ConfigTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create test config files for different test scenarios
        createValidConfigFile();
    }

    void TearDown() override
    {
        // Clean up test files
        std::remove("test_valid_config.yaml");
    }

  private:
    void createValidConfigFile()
    {
        std::ofstream file("test_valid_config.yaml");
        file << R"(
enable_gpu: true

input_cameras:
  - name: "Test Camera 1"
    path: "/dev/video0"
    width: 640
    height: 480
    buffer_count: 2

output_cameras:
  - name: "Output Camera 1"
    path: "/dev/video10"
    width: 1920
    height: 1080
    subsampling: "420"

external_data:
  media_folder_path: "/tmp/media"
  models_folder_path: "/tmp/models"
  WFLW_folder_path: "/tmp/WFLW"
  preload_content: true

window:
  title: "Test Window"
  width: 1280
  height: 720
)";
        file.close();
    }
};

TEST_F(ConfigTest, ValidConfigLoading)
{
    Config& config = Config::getInstance("test_valid_config.yaml");
    EXPECT_TRUE(config.loadConfiguration());

    // Test GPU setting
    EXPECT_TRUE(config.isGPUEnabled());

    // Test window configuration
    EXPECT_EQ(config.getWindowTitle(), "Test Window");
    int width, height;
    config.getWindowSize(width, height);
    EXPECT_EQ(width, 1280);
    EXPECT_EQ(height, 720);

    // Test external data paths
    EXPECT_EQ(config.getMediaFolderPath(), "/tmp/media/");
    EXPECT_EQ(config.getModelFolderPath(), "/tmp/models/");
    EXPECT_EQ(config.getWFLWFolderPath(), "/tmp/WFLW/");
    EXPECT_TRUE(config.preloadExternalContent());

    // Test cameras
    auto cameras = config.getWebcams();
    EXPECT_EQ(cameras.size(), 2);

    // Test input camera
    EXPECT_EQ(cameras[0].name, "Test Camera 1");
    EXPECT_EQ(cameras[0].device_path, "/dev/video0");
    EXPECT_EQ(cameras[0].width, 640u);
    EXPECT_EQ(cameras[0].height, 480u);
    EXPECT_EQ(cameras[0].buffer_count, 2u);
    EXPECT_TRUE(cameras[0].is_input);

    // Test output camera (buffer_count not implemented in output cameras)
    EXPECT_EQ(cameras[1].name, "Output Camera 1");
    EXPECT_EQ(cameras[1].device_path, "/dev/video10");
    EXPECT_EQ(cameras[1].width, 1920u);
    EXPECT_EQ(cameras[1].height, 1080u);
    EXPECT_EQ(cameras[1].buffer_count, 0u); // Default value since not set for output cameras
    EXPECT_FALSE(cameras[1].is_input);
    EXPECT_EQ(cameras[1].subsampling, TJSAMP_420);
}

TEST_F(ConfigTest, InvalidConfigFile)
{
    Config& config = Config::getInstance("test_invalid_config.yaml");
    // Should fail to load due to invalid YAML
    EXPECT_FALSE(config.loadConfiguration());
}

TEST_F(ConfigTest, MissingConfigFile)
{
    Config& config = Config::getInstance("nonexistent_config.yaml");
    // Should fail to load due to missing file
    EXPECT_FALSE(config.loadConfiguration());
}

TEST_F(ConfigTest, MissingRequiredFields)
{
    Config& config = Config::getInstance("test_missing_field_config.yaml");
    // Should fail to load due to missing required fields
    EXPECT_FALSE(config.loadConfiguration());
}

TEST_F(ConfigTest, GetTemplateMethod)
{
    Config& config = Config::getInstance("test_valid_config.yaml");
    config.loadConfiguration();

    // Test template get method with existing key
    bool gpuEnabled = config.get<bool>("enable_gpu", false);
    EXPECT_TRUE(gpuEnabled);

    // Test template get method with non-existing key (should return default)
    int nonExistentValue = config.get<int>("non_existent_key", 42);
    EXPECT_EQ(nonExistentValue, 42);

    // Test template get method with string
    std::string defaultString = config.get<std::string>("non_existent_string", "default");
    EXPECT_EQ(defaultString, "default");
}

TEST_F(ConfigTest, SingletonBehavior)
{
    Config& config1 = Config::getInstance("test_valid_config.yaml");
    Config& config2 = Config::getInstance("test_valid_config.yaml");

    // Should be the same instance
    EXPECT_EQ(&config1, &config2);
}

// Test the subsampling parsing method functionality
TEST_F(ConfigTest, SubsamplingParsing)
{
    // Create a config with different subsampling values
    std::ofstream file("test_subsampling_config.yaml");
    file << R"(
enable_gpu: true

input_cameras:
  - name: "Test Camera 1"
    path: "/dev/video0"
    width: 640
    height: 480
    buffer_count: 2

output_cameras:
  - name: "Output 420"
    path: "/dev/video10"
    width: 1920
    height: 1080
    buffer_count: 4
    subsampling: "420"
  - name: "Output 422"
    path: "/dev/video11" 
    width: 1920
    height: 1080
    buffer_count: 4
    subsampling: "422"
  - name: "Output 444"
    path: "/dev/video12"
    width: 1920
    height: 1080
    buffer_count: 4
    subsampling: "444"

external_data:
  media_folder_path: "/tmp/media"
  models_folder_path: "/tmp/models"
  WFLW_folder_path: "/tmp/WFLW"
  preload_content: false

window:
  title: "Test Window"
  width: 1280
  height: 720
)";
    file.close();

    Config& config = Config::getInstance("test_subsampling_config.yaml");
    EXPECT_TRUE(config.loadConfiguration());

    auto cameras = config.getWebcams();
    EXPECT_EQ(cameras.size(), 4); // 1 input + 3 outputs

    // Check subsampling values (implementation specific, but should handle different formats)
    EXPECT_EQ(cameras[1].subsampling, TJSAMP_420);
    EXPECT_EQ(cameras[2].subsampling, TJSAMP_422);
    EXPECT_EQ(cameras[3].subsampling, TJSAMP_444);

    // Clean up
    std::remove("test_subsampling_config.yaml");
}

TEST_F(ConfigTest, DisabledGPU)
{
    // Create config with GPU disabled
    std::ofstream file("test_gpu_disabled_config.yaml");
    file << R"(
enable_gpu: false

input_cameras:
  - name: "Test Camera"
    path: "/dev/video0"
    width: 640
    height: 480
    buffer_count: 2

output_cameras:
  - name: "Output Camera"
    path: "/dev/video10"
    width: 1920
    height: 1080
    buffer_count: 4
    subsampling: "420"

external_data:
  media_folder_path: "/tmp/media"
  models_folder_path: "/tmp/models"
  WFLW_folder_path: "/tmp/WFLW"
  preload_content: false

window:
  title: "Test Window"
  width: 1280
  height: 720
)";
    file.close();

    Config& config = Config::getInstance("test_gpu_disabled_config.yaml");
    EXPECT_TRUE(config.loadConfiguration());
    EXPECT_FALSE(config.isGPUEnabled());

    // Clean up
    std::remove("test_gpu_disabled_config.yaml");
}

TEST_F(ConfigTest, PreloadContentDisabled)
{
    Config& config = Config::getInstance("test_valid_config.yaml");
    config.loadConfiguration();

    // Modify our test file to have preload_content: false
    std::ofstream file("test_preload_false_config.yaml");
    file << R"(
enable_gpu: true

input_cameras:
  - name: "Test Camera"
    path: "/dev/video0"
    width: 640
    height: 480
    buffer_count: 2

output_cameras:
  - name: "Output Camera"
    path: "/dev/video10"
    width: 1920
    height: 1080
    buffer_count: 4
    subsampling: "420"

external_data:
  media_folder_path: "/tmp/media"
  models_folder_path: "/tmp/models"
  WFLW_folder_path: "/tmp/WFLW"
  preload_content: false

window:
  title: "Test Window"
  width: 1280
  height: 720
)";
    file.close();

    // Reload the config with the new file
    EXPECT_TRUE(config.reloadFromFile("test_preload_false_config.yaml"));
    EXPECT_TRUE(config.loadConfiguration());
    EXPECT_FALSE(config.preloadExternalContent());

    // Clean up
    std::remove("test_preload_false_config.yaml");
}
