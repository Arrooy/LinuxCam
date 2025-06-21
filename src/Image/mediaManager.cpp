#include "LinuxFace/Image/mediaManager.h"

#include <filesystem>

#include "LinuxFace/imageLoader.h"
#include "config.hpp"

using namespace linuxface;

MediaManager::MediaManager()
{
    std::string folderPath = Config::getInstance().getMediaFolderPath();
    if (loadMediaFromFolder(folderPath) == 0)
    {
        common::log_error("Failed to load media from folder: %s", folderPath.c_str());
    }
}

std::vector<std::string> MediaManager::getImageNames()
{
    return common::getKeysFromMap(images);
}

std::vector<std::string> MediaManager::getGifNames()
{
    return common::getKeysFromMap(gifs);
}


std::shared_ptr<Image> MediaManager::getImage(const std::string& imageName)
{
    auto it = images.find(imageName);
    if (it != images.end())
    {
        return it->second;
    }

    return nullptr;
}

std::shared_ptr<Gif> MediaManager::getGif(const std::string& gifName)
{
    auto it = gifs.find(gifName);
    if (it != gifs.end())
    {
        return it->second;
    }

    return nullptr;
}

size_t MediaManager::loadMediaFromFolder(const std::string& folderPath)
{
    namespace fs = std::filesystem;
    size_t mediaCount{0u};
    for (const auto& entry : fs::directory_iterator(folderPath))
    {
        if (entry.is_regular_file())
        {
            const auto& filename = entry.path().filename().string();
            const auto& extension = entry.path().extension();
            const auto& full_path = entry.path().string();
            bool processingOk{false};
            if (extension == ".jpg")
            {
                std::shared_ptr<Image> image = ImageLoader::loadImageFromFile(full_path);
                if (image)
                {
                    images[filename] = image;
                    processingOk = true;
                }
            }
            else if (extension == ".gif")
            {
            }
            else
            {
                common::log_error("Unsupported file format %s in path %s", extension.c_str(), full_path.c_str());
            }
            if (!processingOk)
            {
                common::log_error("Failed to load image %s", filename.c_str());
            }
            else
            {
                mediaCount++;
            }
        }
    }
    return mediaCount;
}
