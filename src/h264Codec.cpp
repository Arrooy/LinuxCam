#include "LinuxFace/codec.h"
#include "LinuxFace/common.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace linuxface
{

// H264Encoder Impl
struct H264Encoder::Impl
{
    AVCodecContext* codecContext{nullptr};
    AVFrame* avFrame{nullptr};
    AVPacket* avPacket{nullptr};
    SwsContext* swsContext{nullptr};

    unsigned int width{0};
    unsigned int height{0};
    int bitrate{2000000}; // Default 2Mbps

    bool initialized{false};

    bool initialize();
    void cleanup();
};

bool H264Encoder::Impl::initialize()
{
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
    {
        common::logError("H264Encoder::initialize - H.264 codec not found");
        return false;
    }

    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext)
    {
        common::logError("H264Encoder::initialize - Failed to allocate codec context");
        return false;
    }

    codecContext->bit_rate = bitrate;
    codecContext->width = width;
    codecContext->height = height;
    codecContext->time_base = {1, 30};
    codecContext->framerate = {30, 1};
    codecContext->gop_size = 10;
    codecContext->max_b_frames = 0;
    codecContext->pix_fmt = AV_PIX_FMT_YUV420P;

    av_opt_set(codecContext->priv_data, "preset", "ultrafast", 0);
    av_opt_set(codecContext->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(codecContext, codec, nullptr) < 0)
    {
        common::logError("H264Encoder::initialize - Failed to open codec");
        return false;
    }

    avFrame = av_frame_alloc();
    if (!avFrame)
    {
        common::logError("H264Encoder::initialize - Failed to allocate frame");
        return false;
    }

    avFrame->format = codecContext->pix_fmt;
    avFrame->width = codecContext->width;
    avFrame->height = codecContext->height;

    if (av_frame_get_buffer(avFrame, 0) < 0)
    {
        common::logError("H264Encoder::initialize - Failed to allocate frame buffer");
        return false;
    }

    avPacket = av_packet_alloc();
    if (!avPacket)
    {
        common::logError("H264Encoder::initialize - Failed to allocate packet");
        return false;
    }

    swsContext = sws_getContext(width, height, AV_PIX_FMT_RGB24, width, height, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                                nullptr, nullptr, nullptr);
    if (!swsContext)
    {
        common::logError("H264Encoder::initialize - Failed to create sws context");
        return false;
    }

    initialized = true;
    return true;
}

void H264Encoder::Impl::cleanup()
{
    if (swsContext)
    {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }
    if (avPacket)
    {
        av_packet_free(&avPacket);
    }
    if (avFrame)
    {
        av_frame_free(&avFrame);
    }
    if (codecContext)
    {
        avcodec_free_context(&codecContext);
    }
    initialized = false;
}

H264Encoder::H264Encoder(const ConfigBuilder& config) : impl_(std::make_unique<Impl>())
{
    int w = 0, h = 0;
    if (!config.get("width", w))
    {
        common::logError("H264Encoder - Unable to load parameter width");
        return;
    }
    impl_->width = static_cast<unsigned int>(w);

    if (!config.get("height", h))
    {
        common::logError("H264Encoder - Unable to load parameter height");
        return;
    }
    impl_->height = static_cast<unsigned int>(h);

    // Optional bitrate parameter
    config.get("bitrate", impl_->bitrate);

    if (!impl_->initialize())
    {
        common::logError("H264Encoder - Failed to initialize");
        impl_->cleanup();
    }
}

H264Encoder::~H264Encoder()
{
    if (impl_)
    {
        impl_->cleanup();
    }
}

bool H264Encoder::encode(const Image& srcImage, Image& outImage, unsigned long& compressedSize)
{
    if (!impl_->initialized)
    {
        common::logError("H264Encoder::encode - Encoder not initialized");
        return false;
    }

    if (srcImage.info.width != impl_->width || srcImage.info.height != impl_->height)
    {
        common::logError("H264Encoder::encode - Image size mismatch: expected %ux%u, got %lux%lu", impl_->width,
                         impl_->height, srcImage.info.width, srcImage.info.height);
        return false;
    }

    if (srcImage.info.pixelSizeBytes != 3)
    {
        common::logError("H264Encoder::encode - Expected RGB24 format (3 bytes per pixel)");
        return false;
    }

    if (av_frame_make_writable(impl_->avFrame) < 0)
    {
        common::logError("H264Encoder::encode - Failed to make frame writable");
        return false;
    }

    const uint8_t* srcData[1] = {srcImage.data()};
    int srcLinesize[1] = {static_cast<int>(impl_->width * 3)};

    sws_scale(impl_->swsContext, srcData, srcLinesize, 0, impl_->height, impl_->avFrame->data,
              impl_->avFrame->linesize);

    static int64_t pts = 0;
    impl_->avFrame->pts = pts++;

    int ret = avcodec_send_frame(impl_->codecContext, impl_->avFrame);
    if (ret < 0)
    {
        common::logError("H264Encoder::encode - Error sending frame to encoder");
        return false;
    }

    ret = avcodec_receive_packet(impl_->codecContext, impl_->avPacket);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        return false;
    }
    else if (ret < 0)
    {
        common::logError("H264Encoder::encode - Error receiving packet from encoder");
        return false;
    }

    if (outImage.size() < static_cast<size_t>(impl_->avPacket->size))
    {
        outImage.resize(impl_->avPacket->size);
    }

    memcpy(outImage.data(), impl_->avPacket->data, impl_->avPacket->size);
    compressedSize = impl_->avPacket->size;

    outImage.info = srcImage.info;
    outImage.info.format = ImageFormat::H264;

    av_packet_unref(impl_->avPacket);

    return true;
}

unsigned long H264Encoder::encodeSizeInBytes()
{
    return impl_->width * impl_->height * 3 / 2;
}

// H264Decoder Impl
struct H264Decoder::Impl
{
    AVCodecContext* decoderContext{nullptr};
    AVFrame* decodedFrame{nullptr};
    AVPacket* decoderPacket{nullptr};
    SwsContext* decoderSwsContext{nullptr};

    bool initialized{false};

    bool initialize();
    void cleanup();
};

bool H264Decoder::Impl::initialize()
{
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec)
    {
        common::logError("H264Decoder::initialize - H.264 codec not found");
        return false;
    }

    decoderContext = avcodec_alloc_context3(codec);
    if (!decoderContext)
    {
        common::logError("H264Decoder::initialize - Failed to allocate decoder context");
        return false;
    }

    if (avcodec_open2(decoderContext, codec, nullptr) < 0)
    {
        common::logError("H264Decoder::initialize - Failed to open decoder");
        return false;
    }

    decodedFrame = av_frame_alloc();
    if (!decodedFrame)
    {
        common::logError("H264Decoder::initialize - Failed to allocate frame");
        return false;
    }

    decoderPacket = av_packet_alloc();
    if (!decoderPacket)
    {
        common::logError("H264Decoder::initialize - Failed to allocate packet");
        return false;
    }

    initialized = true;
    return true;
}

void H264Decoder::Impl::cleanup()
{
    if (decoderSwsContext)
    {
        sws_freeContext(decoderSwsContext);
        decoderSwsContext = nullptr;
    }
    if (decoderPacket)
    {
        av_packet_free(&decoderPacket);
    }
    if (decodedFrame)
    {
        av_frame_free(&decodedFrame);
    }
    if (decoderContext)
    {
        avcodec_free_context(&decoderContext);
    }
    initialized = false;
}

// H264Decoder Implementation
H264Decoder::H264Decoder(const ConfigBuilder& /*config*/) : impl_(std::make_unique<Impl>())
{
    if (!impl_->initialize())
    {
        common::logError("H264Decoder - Failed to initialize");
        impl_->cleanup();
    }
}

H264Decoder::~H264Decoder()
{
    if (impl_)
    {
        impl_->cleanup();
    }
}

bool H264Decoder::decode(const Image& srcImage, Image& outImage)
{
    if (!impl_->initialized)
    {
        common::logError("H264Decoder::decode - Decoder not initialized");
        return false;
    }

    impl_->decoderPacket->data = const_cast<uint8_t*>(srcImage.data());
    impl_->decoderPacket->size = static_cast<int>(srcImage.size());

    int ret = avcodec_send_packet(impl_->decoderContext, impl_->decoderPacket);
    if (ret < 0)
    {
        if (ret != AVERROR(EAGAIN))
        {
            common::logError("H264Decoder::decode - Error sending packet to decoder");
        }
        return false;
    }

    ret = avcodec_receive_frame(impl_->decoderContext, impl_->decodedFrame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        return false;
    }
    else if (ret < 0)
    {
        common::logError("H264Decoder::decode - Error receiving frame from decoder");
        return false;
    }

    // Create sws context if not exists or if dimensions changed
    if (!impl_->decoderSwsContext || impl_->decodedFrame->width != impl_->decoderContext->width
        || impl_->decodedFrame->height != impl_->decoderContext->height)
    {
        if (impl_->decoderSwsContext)
        {
            sws_freeContext(impl_->decoderSwsContext);
        }

        impl_->decoderSwsContext =
            sws_getContext(impl_->decodedFrame->width, impl_->decodedFrame->height,
                           static_cast<AVPixelFormat>(impl_->decodedFrame->format), impl_->decodedFrame->width,
                           impl_->decodedFrame->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (!impl_->decoderSwsContext)
        {
            common::logError("H264Decoder::decode - Failed to create sws context");
            return false;
        }
    }

    const size_t rgbSize = impl_->decodedFrame->width * impl_->decodedFrame->height * 3;
    if (outImage.size() != rgbSize)
    {
        outImage.resize(rgbSize);
    }

    uint8_t* dstData[1] = {outImage.data()};
    int dstLinesize[1] = {impl_->decodedFrame->width * 3};

    sws_scale(impl_->decoderSwsContext, impl_->decodedFrame->data, impl_->decodedFrame->linesize, 0,
              impl_->decodedFrame->height, dstData, dstLinesize);

    outImage.info.width = impl_->decodedFrame->width;
    outImage.info.height = impl_->decodedFrame->height;
    outImage.info.pixelSizeBytes = 3;
    outImage.info.format = ImageFormat::RGB;
    outImage.info.TJPixelFormat = TJPF_RGB;

    av_packet_unref(impl_->decoderPacket);

    return true;
}

bool H264Decoder::decodeHeader(Image& srcImage, unsigned long& rawNeededSize)
{
    // For H.264, we need to actually decode to determine size
    // This is a limitation of the current design
    rawNeededSize = 0;
    srcImage.info.format = ImageFormat::H264;
    return true;
}

} // namespace linuxface
