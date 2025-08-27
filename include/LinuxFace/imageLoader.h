#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/codec.h"

namespace linuxface
{

class ImageFormatDetector
{
  public:
    static ImageFormat detectFormat(const std::vector<unsigned char>& data);
    static ImageFormat detectFormatFromPath(const std::string& path);

  private:
    static bool isJPEG(const std::vector<unsigned char>& data);
    static bool isPNG(const std::vector<unsigned char>& data);
    static bool isBMP(const std::vector<unsigned char>& data);
    static bool isPPM(const std::vector<unsigned char>& data);
};

class ImageLoader
{
  public:
    enum class LoadStrategy
    {
        IMMEDIATE,    // Load and decode immediately
        LAZY,         // Load raw data, decode on demand
        METADATA_ONLY // Only load metadata
    };

    explicit ImageLoader(LoadStrategy strategy = LoadStrategy::LAZY);

    // Load image from file
    bool loadFromFile(const std::string& filePath);

    // Get decoded RGB888 image
    bool getImage(std::unique_ptr<Image>& outImage);

    // Static factory method for convenient loading
    static std::unique_ptr<Image> loadImageFromFile(const std::string& filePath)
    {
        ImageLoader loader(LoadStrategy::LAZY);
        if (!loader.loadFromFile(filePath))
        {
            return nullptr;
        }

        std::unique_ptr<Image> image;
        if (!loader.getImage(image))
        {
            return nullptr;
        }

        return image;
    }

    // Get metadata without decoding
    const ImageMetadata& getMetadata() const { return metadata_; }

    // Check if image was loaded successfully
    bool isValid() const { return metadata_.is_valid; }

  private:
    bool loadFileData(const std::string& filePath);
    bool extractMetadata();
  bool createDecoder();

    LoadStrategy strategy_;
    ImageMetadata metadata_;
    std::vector<unsigned char> raw_data_;
    std::unique_ptr<Decoder> decoder_;
    std::unique_ptr<Image> decoded_image_;
    bool is_decoded_{false};
};

} // namespace linuxface
#endif // IMAGELOADER_H
