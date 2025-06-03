#ifndef CODEC_H
#define CODEC_H

#include <memory>
#include <type_traits>

#include "image.h"

namespace funnyface
{

// Forward declarations for concrete implementations
class JpegEncoder;
class JpegDecoder;
class DoNothingEncoder;
class DoNothingDecoder;

// JPEG-specific configuration
class JpegEncoderConfig
{
  public:
    int quality = 85;      // 0-100, higher = better quality
    int subsampling = 420; // 420, 422, 444
    bool progressive = false;
};

class JpegDecoderConfig
{
  public:
    bool fast_decode = false;
};

class DoNothingEncoderConfig
{
  public:
    // No configuration needed
};

class DoNothingDecoderConfig
{
  public:
    // No configuration needed
};

class Encoder
{
  public:
    Encoder() = default;
    virtual ~Encoder() = default;

    // Disable copy constructor and assignment operator
    Encoder(const Encoder&) = delete;
    Encoder(Encoder&&) = delete;
    Encoder& operator=(const Encoder&) = delete;
    Encoder& operator=(Encoder&&) = delete;

    virtual bool encode(const Image& srcImage, Image& outImage) = 0;
};

class Decoder
{
  public:
    Decoder() = default;
    virtual ~Decoder() = default;

    // Disable copy constructor and assignment operator
    Decoder(const Decoder&) = delete;
    Decoder(Decoder&&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder& operator=(Decoder&&) = delete;

    virtual bool decode(const Image& srcImage, Image& outImage) = 0;
};

template <typename EncoderConfig, typename DecoderConfig>
class Codec
{
  public:
    // Primary constructor using configs
    Codec(const EncoderConfig& encoderConfig, const DecoderConfig& decoderConfig)
        : encoder_(createEncoder(encoderConfig)), decoder_(createDecoder(decoderConfig))
    {
    }

    // Static factory method
    static std::unique_ptr<Codec> create(const EncoderConfig& encoderConfig, const DecoderConfig& decoderConfig)
    {
        return std::make_unique<Codec>(encoderConfig, decoderConfig);
    }

    bool encode(const Image& srcImage, Image& outImage) const
    {
        return encoder_ && encoder_->encode(srcImage, outImage);
    }

    bool decode(const Image& srcImage, Image& outImage) const
    {
        return decoder_ && decoder_->decode(srcImage, outImage);
    }

    bool hasEncoder() const noexcept { return encoder_ != nullptr; }
    bool hasDecoder() const noexcept { return decoder_ != nullptr; }

    // Convenience method to check if codec is valid
    bool isValid() const noexcept { return hasEncoder() && hasDecoder(); }

    // Add move constructor and assignment for better performance
    Codec(Codec&& other) noexcept = default;
    Codec& operator=(Codec&& other) noexcept = default;

    // Delete copy constructor and assignment
    Codec(const Codec&) = delete;
    Codec& operator=(const Codec&) = delete;

  private:
    static std::unique_ptr<Encoder> createEncoder(const EncoderConfig& config)
    {
        if constexpr (std::is_same_v<EncoderConfig, JpegEncoderConfig>)
        {
            return std::make_unique<JpegEncoder>(config);
        }
        else if constexpr (std::is_same_v<EncoderConfig, DoNothingEncoderConfig>)
        {
            return std::make_unique<DoNothingEncoder>();
        }
        else
        {
            static_assert(!std::is_same_v<EncoderConfig, EncoderConfig>, "Unsupported encoder config type");
        }
        return nullptr; // Fallback for unsupported types
    }

    static std::unique_ptr<Decoder> createDecoder(const DecoderConfig& config)
    {
        if constexpr (std::is_same_v<DecoderConfig, JpegDecoderConfig>)
        {
            return std::make_unique<JpegDecoder>(config);
        }
        else if constexpr (std::is_same_v<DecoderConfig, DoNothingDecoderConfig>)
        {
            return std::make_unique<DoNothingDecoder>();
        }
        else
        {
            static_assert(!std::is_same_v<DecoderConfig, DecoderConfig>, "Unsupported decoder config type");
        }
        return nullptr; // Fallback for unsupported types
    }

    std::unique_ptr<Encoder> encoder_;
    std::unique_ptr<Decoder> decoder_;
};

// Type aliases for common codec combinations
using JpegCodec = Codec<JpegEncoderConfig, JpegDecoderConfig>;

} // namespace funnyface
#endif // CODEC_H
