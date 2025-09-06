#include "LinuxFace/videoLoader.h"

#include <iostream>

#include "LinuxFace/common.h"

using namespace linuxface;

VideoLoader::VideoLoader()
{
    // Initialize FFmpeg libraries
    avformat_network_init(); // can we remove this?
}

VideoLoader::~VideoLoader()
{
    // Clean up FFmpeg resources
    if (swsContext_)
    {
        sws_freeContext(swsContext_);
    }
    if (frameRGB_)
    {
        av_frame_free(&frameRGB_);
    }
    if (frame_)
    {
        av_frame_free(&frame_);
    }
    if (codecContext_)
    {
        avcodec_free_context(&codecContext_);
    }
    if (formatContext_)
    {
        avformat_close_input(&formatContext_);
    }
}

bool VideoLoader::loadFromFile(const std::string& filePath)
{
    common::logInfo("Loading video from file: %s", filePath.c_str());

    // Reset previous state
    if (swsContext_)
    {
        sws_freeContext(swsContext_);
        swsContext_ = nullptr;
    }
    if (frameRGB_)
    {
        av_frame_free(&frameRGB_);
        frameRGB_ = nullptr;
    }
    if (frame_)
    {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (codecContext_)
    {
        avcodec_free_context(&codecContext_);
        codecContext_ = nullptr;
    }
    if (formatContext_)
    {
        avformat_close_input(&formatContext_);
        formatContext_ = nullptr;
    }

    metadata_ = VideoMetadata();
    currentFrameIndex_ = 0;
    videoStreamIndex_ = -1;

    // Open video file
    if (avformat_open_input(&formatContext_, filePath.c_str(), nullptr, nullptr) != 0)
    {
        common::logError("Failed to open video file: %s", filePath.c_str());
        return false;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(formatContext_, nullptr) < 0)
    {
        common::logError("Failed to find stream information");
        return false;
    }

    // Find the first video stream
    for (unsigned int i = 0; i < formatContext_->nb_streams; i++)
    {
        if (formatContext_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStreamIndex_ = i;
            break;
        }
    }

    if (videoStreamIndex_ == -1)
    {
        common::logError("No video stream found");
        return false;
    }

    AVCodecParameters* codecParameters = formatContext_->streams[videoStreamIndex_]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);

    if (!codec)
    {
        common::logError("Unsupported codec");
        return false;
    }

    // Allocate codec context
    codecContext_ = avcodec_alloc_context3(codec);
    if (!codecContext_)
    {
        common::logError("Failed to allocate codec context");
        return false;
    }

    // Copy codec parameters to context
    if (avcodec_parameters_to_context(codecContext_, codecParameters) < 0)
    {
        common::logError("Failed to copy codec parameters");
        return false;
    }

    // Open codec
    if (avcodec_open2(codecContext_, codec, nullptr) < 0)
    {
        common::logError("Failed to open codec");
        return false;
    }

    // Allocate frames
    frame_ = av_frame_alloc();
    frameRGB_ = av_frame_alloc();
    if (!frame_ || !frameRGB_)
    {
        common::logError("Failed to allocate frames");
        return false;
    }

    // Set up scaling context for RGB conversion
    swsContext_ =
        sws_getContext(codecContext_->width, codecContext_->height, codecContext_->pix_fmt, codecContext_->width,
                       codecContext_->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsContext_)
    {
        common::logError("Failed to create scaling context");
        return false;
    }

    // Allocate buffer for RGB frame
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codecContext_->width, codecContext_->height, 1);
    uint8_t* buffer = (uint8_t*) av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(frameRGB_->data, frameRGB_->linesize, buffer, AV_PIX_FMT_RGB24, codecContext_->width,
                         codecContext_->height, 1);

    // Extract metadata
    AVStream* videoStream = formatContext_->streams[videoStreamIndex_];
    metadata_.width = codecContext_->width;
    metadata_.height = codecContext_->height;
    metadata_.frameCount = videoStream->nb_frames > 0
                               ? videoStream->nb_frames
                               : (videoStream->duration * videoStream->time_base.num) / videoStream->time_base.den;
    metadata_.fps = av_q2d(videoStream->r_frame_rate);
    metadata_.filename = filePath;
    metadata_.isValid = true;

    common::logInfo("Video loaded: %s, %dx%d, %d frames, %.2f fps", filePath.c_str(), metadata_.width, metadata_.height,
                    metadata_.frameCount, metadata_.fps);

    return true;
}

bool VideoLoader::getNextFrame(std::unique_ptr<Image>& outImage)
{
    if (!formatContext_ || !codecContext_ || !metadata_.isValid)
    {
        common::logError("Video not loaded or invalid");
        return false;
    }

    AVPacket packet;
    bool frameDecoded = false;

    while (!frameDecoded && av_read_frame(formatContext_, &packet) >= 0)
    {
        if (packet.stream_index == videoStreamIndex_)
        {
            // Send packet to decoder
            int response = avcodec_send_packet(codecContext_, &packet);
            if (response < 0)
            {
                common::logError("Error sending packet to decoder");
                av_packet_unref(&packet);
                continue;
            }

            // Receive frame from decoder
            response = avcodec_receive_frame(codecContext_, frame_);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
            {
                av_packet_unref(&packet);
                continue;
            }
            else if (response < 0)
            {
                common::logError("Error receiving frame from decoder");
                av_packet_unref(&packet);
                continue;
            }

            // Convert frame to RGB
            sws_scale(swsContext_, frame_->data, frame_->linesize, 0, codecContext_->height, frameRGB_->data,
                      frameRGB_->linesize);

            // Create Image from RGB frame data
            size_t dataSize = codecContext_->width * codecContext_->height * 3; // RGB24 = 3 bytes per pixel
            outImage = std::make_unique<Image>(frameRGB_->data[0], dataSize, false);
            outImage->info.width = codecContext_->width;
            outImage->info.height = codecContext_->height;
            outImage->info.pixelSizeBytes = 3; // RGB
            outImage->info.format = ImageFormat::RGB;

            frameDecoded = true;
            currentFrameIndex_++;
        }
        av_packet_unref(&packet);
    }

    if (!frameDecoded)
    {
        common::logInfo("End of video reached");
        outImage = nullptr;  // Set to nullptr when end of video is reached
        return false;
    }

    return true;
}

bool VideoLoader::seekToFrame(int frameIndex)
{
    if (!formatContext_ || !metadata_.isValid || frameIndex < 0 || frameIndex >= metadata_.frameCount)
    {
        return false;
    }

    AVStream* videoStream = formatContext_->streams[videoStreamIndex_];
    int64_t timestamp = (frameIndex * videoStream->time_base.den) / (videoStream->time_base.num * metadata_.fps);

    if (av_seek_frame(formatContext_, videoStreamIndex_, timestamp, AVSEEK_FLAG_BACKWARD) < 0)
    {
        return false;
    }

    // Flush codec buffers
    avcodec_flush_buffers(codecContext_);
    currentFrameIndex_ = frameIndex;
    return true;
}

int VideoLoader::getCurrentFrameIndex() const
{
    return currentFrameIndex_;
}

void VideoLoader::reset()
{
    if (formatContext_ && metadata_.isValid)
    {
        av_seek_frame(formatContext_, videoStreamIndex_, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codecContext_);
        currentFrameIndex_ = 0;
    }
}
