#include "LinuxFace/Image/gif.h"

#include "LinuxFace/common.h"
namespace linuxface
{

Gif::Gif(const std::string& filename) : filename_(filename)
{
    gif_ = gd_open_gif(filename.c_str());
}

Gif::~Gif()
{
    if (gif_)
    {
        gd_close_gif(gif_);
    }
}

bool Gif::isOpen() const
{
    return gif_ != nullptr;
}

bool Gif::decodeAllFrames()
{
    if (!gif_)
    {
        return false;
    }
    // Gif library only works with RGB
    const size_t frameSize = gif_->width * gif_->height * 3;
    auto buffer = std::make_unique<uint8_t[]>(frameSize);
    int i = 0;
    do
    {
        // TODO: FIXME: There are 2 buffer copy here, we could simplify
        auto img = std::make_unique<Image>(frameSize);
        gd_render_frame(gif_, img->data());

        img->info.width = gif_->width;
        img->info.height = gif_->height;
        img->info.x = 0;
        img->info.y = 0;
        img->info.pixelSizeBytes = 3;
        img->info.TJSampleFormat = TJSAMP_444;
        img->info.TJColorSpace = TJCS_RGB;
        img->info.TJPixelFormat = TJPF_RGB;
        img->info.filename = filename_+"_"+std::to_string(i++);
        size_ += frameSize;
        frameImages_.push_back(std::move(img));
    } while (gd_get_frame(gif_));
    common::log_info("Gif loaded using %s", common::format_size(size_));
    return true;
}

std::vector<std::unique_ptr<Image>>& Gif::frames()
{
    return frameImages_;
}


bool Gif::hasNext() const
{
    return !frameImages_.empty();
}

std::unique_ptr<Image>& Gif::next()
{
    index_++;
    if (index_ >= frameImages_.size())
    {
        index_ = 0;
    }
    auto& img = frameImages_[index_];
    if (img)
    {
        img->move(x_, y_);
    }
    else
    {
        common::log_error("Gif::next - No image found at index %zu", index_);
    }
    return img;
}

} // namespace linuxface
