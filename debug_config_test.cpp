#include <iostream>
#include <fstream>
#include <filesystem>
#include "src/config.hpp"

using namespace linuxface;

int main() {
    // Create test config with empty path
    std::string testPath = "debug_empty_config.yaml";
    std::ofstream configFile(testPath);
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
    configFile << "  media_folder_path: \"\"\n";
    configFile << "  models_folder_path: \"test\"\n";
    configFile << "  WFLW_folder_path: \"/\"\n";
    configFile << "  preload_content: false\n";
    configFile << "window:\n";
    configFile << "  width: 800\n";
    configFile << "  height: 600\n";
    configFile << "  title: \"Test\"\n";
    configFile.close();
    
    Config& config = Config::getInstance();
    std::cout << "Loading config from: " << testPath << std::endl;
    bool reloadResult = config.reloadFromFile(testPath.c_str());
    std::cout << "Reload result: " << (reloadResult ? "true" : "false") << std::endl;
    
    bool loadResult = config.loadConfiguration();
    std::cout << "Load result: " << (loadResult ? "true" : "false") << std::endl;
    
    std::cout << "Media folder path: '" << config.getMediaFolderPath() << "'" << std::endl;
    std::cout << "Model folder path: '" << config.getModelFolderPath() << "'" << std::endl;
    
    std::filesystem::remove(testPath);
    return 0;
}
