#include <gtest/gtest.h>
#include "config.hpp"
#include <fstream>
#include <filesystem>

using namespace linuxface;

class ConfigPathNormalizationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary config file for testing
        testConfigPath = "test_config_path_norm.yaml";
        
        std::ofstream configFile(testConfigPath);
        configFile << "input_cameras:\n";
        configFile << "  - name: \"TestInput\"\n";
        configFile << "    path: \"/dev/video0\"\n";
        configFile << "    width: 640\n";
        configFile << "    height: 480\n";
        configFile << "    buffer_count: 2\n";
        configFile << "output_cameras:\n";
        configFile << "  - name: \"TestOutput\"\n";
        configFile << "    path: \"/dev/video8\"\n";
        configFile << "    width: 640\n";
        configFile << "    height: 480\n";
        configFile << "    jpeg_quality: 90\n";
        configFile << "    subsampling: \"444\"\n";
        configFile << "external_data:\n";
        configFile << "  media_folder_path: \"/path/to/media//\"\n";  // Double slash
        configFile << "  models_folder_path: \"/path/to/models\"\n";  // No slash
        configFile << "  WFLW_folder_path: \"/path/to/wflw/\"\n";     // Single slash
        configFile << "  preload_content: false\n";
        configFile << "window:\n";
        configFile << "  width: 800\n";
        configFile << "  height: 600\n";
        configFile << "  title: \"Test\"\n";
        configFile.close();
    }
    
    void TearDown() override {
        std::filesystem::remove(testConfigPath);
    }
    
    std::string testConfigPath;
};

TEST_F(ConfigPathNormalizationTest, PathsEndWithSingleSlash) {
    Config& config = Config::getInstance();
    ASSERT_TRUE(config.reloadFromFile(testConfigPath.c_str()));
    ASSERT_TRUE(config.loadConfiguration());
    
    // Test that all paths end with exactly one slash using public methods
    std::string mediaPath = config.getMediaFolderPath();
    std::string modelsPath = config.getModelFolderPath();
    
    EXPECT_EQ(mediaPath, "/path/to/media/");   // Double slash normalized to single
    EXPECT_EQ(modelsPath, "/path/to/models/"); // Missing slash added
    
    // Verify no double slashes
    EXPECT_EQ(mediaPath.find("//"), std::string::npos);
    EXPECT_EQ(modelsPath.find("//"), std::string::npos);
    
    // Verify ends with exactly one slash
    EXPECT_TRUE(mediaPath.back() == '/');
    EXPECT_TRUE(modelsPath.back() == '/');
}

TEST_F(ConfigPathNormalizationTest, EmptyPathHandling) {
    // Create config with empty path
    std::string emptyConfigPath = "test_config_empty_path.yaml";
    std::ofstream configFile(emptyConfigPath);
    configFile << "input_cameras:\n";
    configFile << "  - name: \"TestInput\"\n";
    configFile << "    path: \"/dev/video0\"\n";
    configFile << "    width: 640\n";
    configFile << "    height: 480\n";
    configFile << "    buffer_count: 2\n";
    configFile << "output_cameras:\n";
    configFile << "  - name: \"TestOutput\"\n";
    configFile << "    path: \"/dev/video8\"\n";
    configFile << "    width: 640\n";
    configFile << "    height: 480\n";
    configFile << "    jpeg_quality: 90\n";
    configFile << "    subsampling: \"444\"\n";
    configFile << "external_data:\n";
    configFile << "  media_folder_path: \"\"\n";  // Empty path
    configFile << "  models_folder_path: \"test\"\n";
    configFile << "  WFLW_folder_path: \"/\"\n";
    configFile << "  preload_content: false\n";
    configFile << "window:\n";
    configFile << "  width: 800\n";
    configFile << "  height: 600\n";
    configFile << "  title: \"Test\"\n";
    configFile.close();
    
    Config& config = Config::getInstance();
    ASSERT_TRUE(config.reloadFromFile(emptyConfigPath.c_str()));
    ASSERT_TRUE(config.loadConfiguration());
    
    // Empty path should become "/"
    EXPECT_EQ(config.getMediaFolderPath(), "/");
    EXPECT_EQ(config.getModelFolderPath(), "test/");
    
    std::filesystem::remove(emptyConfigPath);
}
