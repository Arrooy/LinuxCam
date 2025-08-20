#include "LinuxFace/Image/mediaManager.h"

#include <filesystem>
#include <utility>

#include "LinuxFace/imageLoader.h"
#include "config.hpp"

using namespace linuxface;

MediaManager::MediaManager(std::shared_ptr<ImageRenderGL> imageRenderGl)
    : imageRenderGl_(std::move(std::move(imageRenderGl)))
{
    const std::string folderPath = Config::getInstance().getMediaFolderPath();
    loadThread_ = std::thread(&MediaManager::loadMediaFromFolder, this, folderPath);
}

std::vector<std::string> MediaManager::getImageNames()
{
    const std::lock_guard<std::mutex> lock(imageMutex_);
    return common::getKeysFromMap(images_);
}

std::vector<std::string> MediaManager::getGifNames()
{
    const std::lock_guard<std::mutex> lock(gifMutex_);
    return common::getKeysFromMap(gifs_);
}


std::shared_ptr<Image> MediaManager::getImage(const std::string& imageName)

{
    const std::lock_guard<std::mutex> lock(imageMutex_);
    auto it = images_.find(imageName);
    if (it != images_.end())
    {
        return it->second;
    }

    return nullptr;
}

std::shared_ptr<Gif> MediaManager::getGif(const std::string& gifName)
{
    const std::lock_guard<std::mutex> lock(gifMutex_);
    auto it = gifs_.find(gifName);
    if (it != gifs_.end())
    {
        return it->second;
    }

    return nullptr;
}

size_t MediaManager::loadMediaFromFolder(const std::string& folderPath)
{
    const std::lock_guard<std::recursive_mutex> lock(loadMutex_);
    const size_t mediaCount{0u};

    for (const auto& entry : fs::directory_iterator(folderPath))
    {
        if (stopLoading_)
        {
            common::logWarn("Media loading stopped by user.");
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
            const auto& fullPath = entry.path().string();
            bool processingOk{false};
            if (extension == ".jpg" || extension == ".jpeg")
            {
                const std::shared_ptr<Image> image = ImageLoader::loadImageFromFile(fullPath);
                if (image)
                {
                    const std::lock_guard<std::mutex> lock(imageMutex_);
                    image[filename] = image;
                    processingOk = true;
                }
            }
            else if (extension == ".gif")
            {
                const std::shared_ptr<Gif> gif = std::make_shared<Gif>(fullPath);
                if (gif->isOpen() && gif->decodeAllFrames())
                {
                    const std::lock_guard<std::mutex> lock(gifMutex_);
                    gif[filename] = gif;
                    processingOk = true;
                }
                else
                {
                    common::logError("Failed to decode GIF: %s", fullPath.c_str());
                }
            }
            else
            {
                common::logError("Unsupported file format %s in path %s", extension.c_str(), fullPath.c_str());
            }
            if (!processingOk)
            {
                common::logError("Failed to load image %s", filename.c_str());
            }
            else
            {
                mediaCount++;
            }
        }
    }
    common::logInfo("Loaded %zu media items from folder: %s", mediaCount, folderPath.c_str());
    return mediaCount;
}

bool MediaManager::reloadImage(const std::string& imageName)
{
    const std::lock_guard<std::mutex> lock(imageMutex_);
    auto it = images_.find(imageName);
    if (it != images_.end())
    {
        auto& image = it->second;
        ImageLoader loader(ImageLoader::LoadStrategy::LAZY);
        if (!loader.loadFromFile(image->info.filename))
        {
            common::logError("Failed to reload image: %s", imageName.c_str());
            return false;
        }

        std::unique_ptr<Image> reloadedImage;
        if (!loader.getImage(reloadedImage))
        {
            common::logError("Failed to decode reloaded image: %s", imageName.c_str());
            return false;
        }

        // Replace the shared_ptr with the newly loaded image
        image = std::shared_ptr<Image>(reloadedImage.release());
        image->info.textureId = 0; // Reset texture ID to force re-upload

        return true;
    }

    common::logError("Image not found: %s", imageName.c_str());
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
    images_.clear();
    gifs_.clear();
}
