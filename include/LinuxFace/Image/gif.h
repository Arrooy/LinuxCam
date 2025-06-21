#ifndef GIF_READER_H
#define GIF_READER_H

#include <memory>
#include <string>
#include <vector>

extern "C"
{
#include "gifdec.h"
}

#include "LinuxFace/Image/image.h"

namespace linuxface
{

class Gif
{
  public:
    explicit Gif(const std::string& filename);
    ~Gif();

    bool isOpen() const;

    // Loads and stores all frames as Image objects
    bool decodeAllFrames();

    // Access individual frames
    std::vector<std::unique_ptr<Image>>& frames();

    bool hasNext() const;
    std::unique_ptr<Image>& next();

    inline void move(long x, long y)
    {
        x_ = x;
        y_ = y;
    }
  private:
    gd_GIF* gif_ = nullptr;
    std::vector<std::unique_ptr<Image>> frameImages_;
    size_t index_{0};

    long x_{0}, y_{0};
};

} // namespace linuxface

#endif // GIF_READER_H
