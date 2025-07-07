#include "LinuxFace/Image/mediaManager.h"

#include <filesystem>

#include "LinuxFace/imageLoader.h"
#include "config.hpp"

using namespace linuxface;

MediaManager::MediaManager(std::shared_ptr<ImageRenderGL> imageRenderGl)
    : images(), gifs(), imageRenderGl_(imageRenderGl)
{
    std::string folderPath = Config::getInstance().getMediaFolderPath();
    loadThread_ = std::thread(&MediaManager::loadMediaFromFolder, this, folderPath);
}

std::vector<std::string> MediaManager::getImageNames()
{
    std::lock_guard<std::mutex> lock(imageMutex_);
    return common::getKeysFromMap(images);
}

std::vector<std::string> MediaManager::getGifNames()
{
    std::lock_guard<std::mutex> lock(gifMutex_);
    return common::getKeysFromMap(gifs);
}


std::shared_ptr<Image> MediaManager::getImage(const std::string& imageName)

{
    std::lock_guard<std::mutex> lock(imageMutex_);
    auto it = images.find(imageName);
    if (it != images.end())
    {
        return it->second;
    }

    return nullptr;
}

std::shared_ptr<Gif> MediaManager::getGif(const std::string& gifName)
{
    std::lock_guard<std::mutex> lock(gifMutex_);
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
    std::lock_guard<std::recursive_mutex> lock(loadMutex_);
    size_t mediaCount{0u};

    for (const auto& entry : fs::directory_iterator(folderPath))
    {
        if (stopLoading_)
        {
            common::log_warn("Media loading stopped by user.");
            break;
        }

        if (entry.is_directory())
        {
            // Recursively load media from subdirectories
            mediaCount += loadMediaFromFolder(entry.path().string());
            continue;
        }

        if (entry.is_regular_file())
        {
            const auto& filename = entry.path().filename().string();
            const auto& extension = entry.path().extension();
            const auto& full_path = entry.path().string();
            bool processingOk{false};
            if (extension == ".jpg" || extension == ".jpeg")
            {
                std::shared_ptr<Image> image = ImageLoader::loadImageFromFile(full_path);
                if (image)
                {
                    std::lock_guard<std::mutex> lock(imageMutex_);
                    images[filename] = image;
                    processingOk = true;
                }
            }
            else if (extension == ".gif")
            {
                std::shared_ptr<Gif> gif = std::make_shared<Gif>(full_path);
                if (gif->isOpen() && gif->decodeAllFrames())
                {
                    std::lock_guard<std::mutex> lock(gifMutex_);
                    gifs[filename] = gif;
                    processingOk = true;
                }
                else
                {
                    common::log_error("Failed to decode GIF: %s", full_path.c_str());
                }
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
    common::log_info("Loaded %zu media items from folder: %s", mediaCount, folderPath.c_str());
    return mediaCount;
}

bool MediaManager::reloadImage(const std::string& imageName)
{
    std::lock_guard<std::mutex> lock(imageMutex_);
    auto it = images.find(imageName);
    if (it != images.end())
    {
        auto& image = it->second;
        ImageLoader loader(ImageLoader::LoadStrategy::LAZY);
        if (!loader.loadFromFile(image->info.filename))
        {
            common::log_error("Failed to reload image: %s", imageName.c_str());
            return false;
        }

        std::unique_ptr<Image> reloadedImage;
        if (!loader.getImage(reloadedImage))
        {
            common::log_error("Failed to decode reloaded image: %s", imageName.c_str());
            return false;
        }

        // Replace the shared_ptr with the newly loaded image
        image = std::shared_ptr<Image>(reloadedImage.release());
        image->info.textureId = 0; // Reset texture ID to force re-upload

        return true;
    }

    common::log_error("Image not found: %s", imageName.c_str());
    return false;
}

void MediaManager::shutdown()
{
    // Stop the loading thread if it's running
    stopLoading_ = true;
    if (loadThread_.joinable())
    {
        loadThread_.join();
    }
    images.clear();
    gifs.clear();
}
