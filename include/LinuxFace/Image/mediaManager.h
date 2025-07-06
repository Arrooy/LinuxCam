#ifndef MEDIAMANAGER_H
#define MEDIAMANAGER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>

#include "LinuxFace/Image/gif.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/imageRenderGL.h"

namespace linuxface
{
class MediaManager
{
  public:
    explicit MediaManager(std::shared_ptr<ImageRenderGL> imageRenderGl);
    std::vector<std::string> getImageNames();
    std::vector<std::string> getGifNames();

    std::shared_ptr<Image> getImage(const std::string& imageName);
    std::shared_ptr<Gif> getGif(const std::string& gifName);

    // Reload image from disk
    bool reloadImage(const std::string& imageName);

    void shutdown();

  private:
    size_t loadMediaFromFolder(const std::string& folderPath);
    std::unordered_map<std::string, std::shared_ptr<Image>> images;
    std::unordered_map<std::string, std::shared_ptr<Gif>> gifs;

    std::shared_ptr<ImageRenderGL> imageRenderGl_;

    std::recursive_mutex loadMutex_;
    std::mutex imageMutex_;
    std::mutex gifMutex_;
    std::thread loadThread_;
    std::atomic<bool> stopLoading_{false};
};
} // namespace linuxface
#endif // MEDIAMANAGER_H
