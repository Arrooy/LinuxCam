#ifndef CODECFACTORY_H
#define CODECFACTORY_H

#include <any>
#include <memory>
#include <type_traits>
#include <unordered_map>

#include "FunnyFace/image.h"

namespace funnyface
{
    
enum class ImageFormat
{
    UNKNOWN,
    JPEG,
    SGBRG8, // Bayer format
    YUV420, // YUV 4:2:0 format
    RAW,
};

inline std::string fromImageFormatToString(const ImageFormat& format)
{
    switch (format)
    {
        case ImageFormat::JPEG:
            return "JPEG";
        case ImageFormat::SGBRG8:
            return "SGBRG8";
        case ImageFormat::YUV420:
            return "YUV420";
        case ImageFormat::RAW:
            return "RAW";
        default:
            return "UNKNOWN";
    }
}

class ConfigBuilder
{
  public:
    ConfigBuilder& quality(int q) { return set("quality", q); }
    ConfigBuilder& width(unsigned int w) { return set("width", static_cast<int>(w)); }
    ConfigBuilder& height(unsigned int h) { return set("height", static_cast<int>(h)); }
    ConfigBuilder& chrominance_subsampling(TJSAMP c) { return set("chrominance_subsampling", c); }
    ConfigBuilder& pixelFormat(TJPF p) { return set("pixelFormat", p); }
    ConfigBuilder& imageFormat(ImageFormat i) { return set("imageFormat", i); }

    // Generic property setter
    template <typename T>
    ConfigBuilder& set(const std::string& key, T value)
    {
        props_[key] = value;
        return *this;
    }

    // Get property with default
    template <typename T>
    bool get(const std::string& key, T& result) const
    {
        auto it = props_.find(key);
        if (it != props_.end())
        {
            try
            {
                result = std::any_cast<T>(it->second);
                return true;
            }
            catch (const std::bad_any_cast&)
            {
                common::log_error("Failed to cast property value to type T");
                return false;
            }
        }
        return false;
    }

    bool has(const std::string& key) const { return props_.find(key) != props_.end(); }

  private:
    std::unordered_map<std::string, std::any> props_;
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

    /**
     * Encode the image from srcImage to outImage.
     * @param srcImage The image to encode.
     * @param outImage The encoded image.
     * @return true if the encoding is successful, false otherwise.
     */
    virtual bool encode(const Image& srcImage, Image& outImage, unsigned long& compressedSize) = 0;

    /**
     * @return The size (bytes) of any image encoded by this class.
     */
    virtual unsigned long encodeSizeInBytes() = 0;
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

    /**
     * Decode the image from srcImage to outImage.
     * @param srcImage The image to decode.
     * @param outImage The decoded image.
     * @return true if the decoding is successful, false otherwise.
     */
    virtual bool decode(const Image& srcImage, Image& outImage) = 0;

    /**
     * Decode the image header from srcImage, updates the same image header.
     * @param srcImage The image to decode.
     * @param raw_needed_size The size of the raw image data needed to decode the image.
     * @return true if the header decoding is successful, false otherwise.
     */
    virtual bool decodeHeader(Image& srcImage, unsigned long& raw_needed_size) = 0;
};

class CodecFactory
{
  public:
    template <typename T>
    static std::unique_ptr<T> create(const ConfigBuilder& config);

  private:
    template <typename T>
    static std::unique_ptr<T> createDecoder(ImageFormat format, const ConfigBuilder& config);

    template <typename T>
    static std::unique_ptr<T> createEncoder(ImageFormat format, const ConfigBuilder& config);
};

} // namespace funnyface
#endif // CODECFACTORY_H
