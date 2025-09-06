#ifndef VIDEOLOADER_H
#define VIDEOLOADER_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <memory>
#include <string>

#include "LinuxFace/Image/image.h"

namespace linuxface
{

struct VideoMetadata
{
    int width = 0;
    int height = 0;
    int frameCount = 0;
    double fps = 0.0;
    bool isValid = false;
    std::string filename;
};

class VideoLoader
{
public:
    VideoLoader();
    ~VideoLoader();

    // Load video from file
    bool loadFromFile(const std::string& filePath);

    // Get next frame as Image
    bool getNextFrame(std::unique_ptr<Image>& outImage);

    // Seek to specific frame
    bool seekToFrame(int frameIndex);

    // Get current frame index
    int getCurrentFrameIndex() const;

    // Check if video is loaded
    bool isValid() const { return metadata_.isValid; }

    // Get metadata
    const VideoMetadata& getMetadata() const { return metadata_; }

    // Reset to beginning
    void reset();

private:
    AVFormatContext* formatContext_ = nullptr;
    AVCodecContext* codecContext_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVFrame* frameRGB_ = nullptr;
    SwsContext* swsContext_ = nullptr;
    int videoStreamIndex_ = -1;
    VideoMetadata metadata_;
    int currentFrameIndex_ = 0;
};

} // namespace linuxface

#endif // VIDEOLOADER_H
