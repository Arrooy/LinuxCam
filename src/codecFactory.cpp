#include "FunnyFace/codecFactory.h"

#include "FunnyFace/codec.h"

using namespace funnyface;

// Create a fresh decoder instance
template <typename T>
std::unique_ptr<T> CodecFactory::create(const ConfigBuilder& config)
{
    ImageFormat format = ImageFormat::UNKNOWN;
    if (!config.get("imageFormat", format) || format == ImageFormat::UNKNOWN)
    {
        return nullptr; // Missing imageFormat in config or unknown format
    }

    if constexpr (std::is_base_of_v<Decoder, T>)
    {
        return createDecoder<T>(format, config);
    }
    else if constexpr (std::is_base_of_v<Encoder, T>)
    {
        return createEncoder<T>(format, config);
    }
    else
    {
        static_assert(std::is_base_of_v<Decoder, T> || std::is_base_of_v<Encoder, T>,
                      "T must be derived from Decoder or Encoder");
        return nullptr;
    }
}

template <typename T>
std::unique_ptr<T> CodecFactory::createDecoder(ImageFormat format, const ConfigBuilder& config)
{
    switch (format)
    {
        case ImageFormat::RAW:
            return std::unique_ptr<T>(new RAWDecoder(config));
        case ImageFormat::JPEG:
            return std::unique_ptr<T>(new JPEGDecoder(config));
        case ImageFormat::SGBRG8:
            return std::unique_ptr<T>(new BayerGBRGDecoder(config));
        // case ImageFormat::YUV420:
        //     return std::unique_ptr<T>(new YUV420Decoder(config));
        // Add more decoders as needed
        default:
            return nullptr;
    }
}

template <typename T>
std::unique_ptr<T> CodecFactory::createEncoder(ImageFormat format, const ConfigBuilder& config)
{
    switch (format)
    {
        case ImageFormat::RAW:
            return std::unique_ptr<T>(new RAWEncoder(config));
        case ImageFormat::JPEG:
            return std::unique_ptr<T>(new JPEGEncoder(config));
        // case ImageFormat::SGBRG8:
        //     return std::unique_ptr<T>(new BayerGBRGEncoder(config));
        // case ImageFormat::YUV420:
        //     return std::unique_ptr<T>(new YUV420Decoder(config));
        // Add more encoders as needed
            default:
            return nullptr;
    }
}

template std::unique_ptr<Decoder> CodecFactory::create<Decoder>(const ConfigBuilder& config);
template std::unique_ptr<Encoder> CodecFactory::create<Encoder>(const ConfigBuilder& config);
