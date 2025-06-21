#ifndef MEDIAMANAGER_H
#define MEDIAMANAGER_H
#include <memory>
#include <string>
#include <unordered_map>

#include "LinuxFace/Image/gif.h"
#include "LinuxFace/Image/image.h"

namespace linuxface
{
class MediaManager
{
  public:
    MediaManager();
    std::vector<std::string> getImageNames();
    std::vector<std::string> getGifNames();

    std::shared_ptr<Image> getImage(const std::string& imageName);
    std::shared_ptr<Gif> getGif(const std::string& gifName);
  private:
    size_t loadMediaFromFolder(const std::string& folderPath);
    std::unordered_map<std::string, std::shared_ptr<Image>> images;
    std::unordered_map<std::string, std::shared_ptr<Gif>> gifs;
};
} // namespace linuxface
#endif // MEDIAMANAGER_H
