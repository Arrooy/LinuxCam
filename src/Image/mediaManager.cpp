#include "LinuxFace/Image/mediaManager.h"

#include <filesystem>

#include "LinuxFace/imageLoader.h"
#include "config.hpp"

using linuxface::Config;
using linuxface::Gif;
using linuxface::Image;
using linuxface::MediaManager;

MediaManager::MediaManager(std::shared_ptr<ImageRenderGL> imageRenderGl) : imageRenderGl_(std::move(imageRenderGl))
{
    const std::string folderPath = Config::getInstance().getMediaFolderPath();
    loadThread_ = std::thread(&MediaManager::loadMediaFromFolder, this, folderPath);
}

std::vector<std::string> MediaManager::getImageNames()
{
    const std::lock_guard<std::mutex> lock(this->imageMutex_);
    return linuxface::common::getKeysFromMap(this->images);
}

std::vector<std::string> MediaManager::getGifNames()
{
    const std::lock_guard<std::mutex> lock(this->gifMutex_);
    return linuxface::common::getKeysFromMap(this->gifs);
}

std::shared_ptr<Image> MediaManager::getImage(const std::string& imageName)

{
    const std::lock_guard<std::mutex> lock(this->imageMutex_);
    auto it = this->images.find(imageName);
    if (it != this->images.end())
        return it->second;
    return nullptr;
}

std::shared_ptr<Gif> MediaManager::getGif(const std::string& gifName)
{
    const std::lock_guard<std::mutex> lock(this->gifMutex_);
    auto it = this->gifs.find(gifName);
    if (it != this->gifs.end())
        return it->second;
    return nullptr;
}

size_t MediaManager::loadMediaFromFolder(const std::string& folderPath)
{
    const std::lock_guard<std::recursive_mutex> lock(this->loadMutex_);
    size_t mediaCount{0u};

    namespace fs = std::filesystem;
    for (const auto& entry : fs::directory_iterator(folderPath))
    {
    if (this->stopLoading_)
        {
            linuxface::common::logWarn("Media loading stopped by user.");
            break;
        }

        if (entry.is_directory())
        {
            // Recursively load media from subdirectories
            mediaCount += this->loadMediaFromFolder(entry.path().string());
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
                if (image != nullptr)
                {
                    const std::lock_guard<std::mutex> lock(imageMutex_);
                    images[filename] = image;
                    processingOk = true;
                }
            }
            else if (extension == ".gif")
            {
                const std::shared_ptr<Gif> gif = std::make_shared<Gif>(fullPath);
                if (gif->isOpen() && gif->decodeAllFrames())
                {
                    const std::lock_guard<std::mutex> lock(gifMutex_);
                    gifs[filename] = gif;
                    processingOk = true;
                }
                else
                {
                    linuxface::common::logError("Failed to decode GIF: %s", fullPath.c_str());
                }
            }
            else
            {
                linuxface::common::logError("Unsupported file format %s in path %s", extension.c_str(),
                                             fullPath.c_str());
            }
            if (!processingOk)
            {
                linuxface::common::logError("Failed to load image %s", filename.c_str());
            }
            else
            {
                mediaCount++;
            }
        }
    }
    linuxface::common::logInfo("Loaded %zu media items from folder: %s", mediaCount, folderPath.c_str());
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
            linuxface::common::logError("Failed to reload image: %s", imageName.c_str());
            return false;
        }

        std::unique_ptr<Image> reloadedImage;
        if (!loader.getImage(reloadedImage))
        {
            linuxface::common::logError("Failed to decode reloaded image: %s", imageName.c_str());
            return false;
        }

        // Replace the shared_ptr with the newly loaded image
        image = std::shared_ptr<Image>(reloadedImage.release());
        image->info.textureId = 0; // Reset texture ID to force re-upload

        return true;
    }

    linuxface::common::logError("Image not found: %s", imageName.c_str());
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
