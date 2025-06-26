#include "LinuxFace/Image/mediaManager.h"

#include <filesystem>

#include "LinuxFace/imageLoader.h"
#include "config.hpp"

using namespace linuxface;

MediaManager::MediaManager(std::shared_ptr<ImageRenderGL> imageRenderGl)
    : images(), gifs(), imageRenderGl_(imageRenderGl)
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

bool MediaManager::addToBackground(const std::string& mediaName)
{
    auto image = getImage(mediaName);
    if (image)
    {
        image->info.layer = 1;
        imageRenderGl_->uploadImage(*image);
        return true;
    }

    auto gif = getGif(mediaName);
    if (gif)
    {
        if (!gif->decodeAllFrames())
        {
            common::log_error("Failed to decode GIF frames for: %s", gif->getFilename().c_str());
            return false;
        }

        for (const auto& frame : gif->frames())
        {
            imageRenderGl_->uploadImage(*frame);
        }
        return true;
    }

    return false;
}

bool MediaManager::reloadImage(const std::string& imageName)
{
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

        std::unique_ptr<Image> newImage;
        if (!loader.getImage(newImage))
        {
            common::log_error("Failed to decode reloaded image: %s", imageName.c_str());
            return false;
        }

        // Update the existing image with the new data
        image->copyFrom(*newImage);

        // Update the texture in the renderer
        if (!imageRenderGl_->uploadImage(*image))
        {
            common::log_error("Failed to upload reloaded image to renderer: %s", imageName.c_str());
            return false;
        }
        return true;
    }

    common::log_error("Image not found: %s", imageName.c_str());
    return false;
}
