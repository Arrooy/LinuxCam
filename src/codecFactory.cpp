#include "LinuxFace/codecFactory.h"

#include "LinuxFace/codec.h"

using linuxface::CodecFactory;
using linuxface::Decoder;
using linuxface::Encoder;
using linuxface::ImageFormat;

// Create a fresh decoder instance
template <typename T>
std::unique_ptr<T> CodecFactory::create(const linuxface::ConfigBuilder& config)
{
    ImageFormat format = ImageFormat::UNKNOWN;
    if (!config.get("imageFormat", format) || format == ImageFormat::UNKNOWN)
    {
        return nullptr; // Missing imageFormat in config or unknown format
    }

    if constexpr (std::is_base_of_v<linuxface::Decoder, T>)
    {
        return CodecFactory::createDecoder<T>(format, config);
    }
    else if constexpr (std::is_base_of_v<linuxface::Encoder, T>)
    {
        return CodecFactory::createEncoder<T>(format, config);
    }
    else
    {
        static_assert(std::is_base_of_v<Decoder, T> || std::is_base_of_v<Encoder, T>,
                      "T must be derived from Decoder or Encoder");
        return nullptr;
    }
}

template <typename T>
std::unique_ptr<T> CodecFactory::createDecoder(ImageFormat format, const linuxface::ConfigBuilder& config)
{
    switch (format)
    {
        case ImageFormat::RAW:
            return std::unique_ptr<T>(new linuxface::RAWDecoder(config));
        case ImageFormat::JPEG:
            return std::unique_ptr<T>(new linuxface::JPEGDecoder(config));
        case ImageFormat::SGBRG8:
            return std::unique_ptr<T>(new linuxface::BayerGBRGDecoder(config));
        case ImageFormat::DEPTH_Z16:
            return std::unique_ptr<T>(new linuxface::DepthZ16Decoder(config));
        case ImageFormat::UYUV422:
            return std::unique_ptr<T>(new linuxface::UYVY422Decoder(config));
        case ImageFormat::YUYV422:
            return std::unique_ptr<T>(new linuxface::YUYV422Decoder(config));
        // Add more decoders as needed
        default:
            return nullptr;
    }
}

template <typename T>
std::unique_ptr<T> CodecFactory::createEncoder(ImageFormat format, const linuxface::ConfigBuilder& config)
{
    switch (format)
    {
        case ImageFormat::RAW:
            return std::unique_ptr<T>(new linuxface::RAWEncoder(config));
        case ImageFormat::JPEG:
            return std::unique_ptr<T>(new linuxface::JPEGEncoder(config));
        // Add more encoders as needed
        default:
            return nullptr;
    }
}

template std::unique_ptr<linuxface::Decoder>
CodecFactory::create<linuxface::Decoder>(const linuxface::ConfigBuilder& config);
template std::unique_ptr<linuxface::Encoder>
CodecFactory::create<linuxface::Encoder>(const linuxface::ConfigBuilder& config);
