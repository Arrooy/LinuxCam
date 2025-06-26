#include "LinuxFace/imageLoader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "LinuxFace/codec.h"
#include "LinuxFace/common.h"

using namespace linuxface;

// ImageFormatDetector Implementation
ImageFormat ImageFormatDetector::detectFormat(const std::vector<unsigned char>& data)
{
    if (data.empty())
    {
        return ImageFormat::UNKNOWN;
    }

    if (isJPEG(data))
    {
        return ImageFormat::JPEG;
    }

    if (isPNG(data))
    {
        return ImageFormat::PNG;
    }

    if (isBMP(data))
    {
        return ImageFormat::BMP;
    }

    return ImageFormat::UNKNOWN;
}

ImageFormat ImageFormatDetector::detectFormatFromPath(const std::string& path)
{
    if (path.empty())
    {
        return ImageFormat::UNKNOWN;
    }

    // Extract file extension
    std::filesystem::path file_path(path);
    std::string extension = file_path.extension().string();

    // Convert to lowercase for comparison
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (extension == ".jpg" || extension == ".jpeg")
    {
        return ImageFormat::JPEG;
    }
    else if (extension == ".png")
    {
        return ImageFormat::PNG;
    }
    else if (extension == ".bmp")
    {
        return ImageFormat::BMP;
    }

    return ImageFormat::UNKNOWN;
}

bool ImageFormatDetector::isJPEG(const std::vector<unsigned char>& data)
{
    if (data.size() < 4)
    {
        return false;
    }

    // JPEG files start with FF D8 and end with FF D9
    return data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF && (data[3] & 0xF0) == 0xE0;
}

bool ImageFormatDetector::isPNG(const std::vector<unsigned char>& data)
{
    if (data.size() < 8)
    {
        return false;
    }

    // PNG signature: 89 50 4E 47 0D 0A 1A 0A
    const unsigned char png_signature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    return std::equal(png_signature, png_signature + 8, data.begin());
}

bool ImageFormatDetector::isBMP(const std::vector<unsigned char>& data)
{
    if (data.size() < 2)
    {
        return false;
    }

    // BMP files start with "BM"
    return data[0] == 0x42 && data[1] == 0x4D;
}

// ImageLoader Implementation
ImageLoader::ImageLoader(LoadStrategy strategy) : strategy_(strategy), is_decoded_(false)
{
    metadata_.is_valid = false;
}

bool ImageLoader::loadFromFile(const std::string& file_path)
{
    // Reset previous state
    raw_data_.clear();
    decoder_.reset();
    decoded_image_.reset();
    is_decoded_ = false;
    metadata_.is_valid = false;
    metadata_.filename = file_path;

    // Load file data
    if (!loadFileData(file_path))
    {
        common::log_error("Failed to load file data from: %s", file_path.c_str());
        return false;
    }

    // Extract metadata
    if (!extractMetadata())
    {
        common::log_error("Failed to extract metadata from: %s", file_path.c_str());
        return false;
    }

    // Create decoder based on format
    if (!createDecoder())
    {
        common::log_error("Failed to create decoder for format: %d", static_cast<int>(metadata_.format));
        return false;
    }

    // Handle immediate loading strategy
    if (strategy_ == LoadStrategy::IMMEDIATE)
    {
        std::unique_ptr<Image> temp_image;
        if (!getImage(temp_image))
        {
            common::log_error("Failed to decode image immediately");
            return false;
        }
    }

    return true;
}

bool ImageLoader::getImage(std::unique_ptr<Image>& outImage)
{
    if (!metadata_.is_valid)
    {
        common::log_error("Cannot get image: metadata is invalid");
        return false;
    }

    if (strategy_ == LoadStrategy::METADATA_ONLY)
    {
        common::log_error("Cannot get image data with METADATA_ONLY strategy");
        return false;
    }

    // Return cached copy of decoded image if available
    if (is_decoded_ && decoded_image_)
    {
        outImage = decoded_image_->deepCopy();
        return true;
    }

    // Decode the image
    if (!decoder_)
    {
        common::log_error("No decoder available");
        return false;
    }

    // Create source image from raw file data (non-owning reference)
    Image srcImage(const_cast<unsigned char*>(raw_data_.data()), raw_data_.size(), false);
    srcImage.info = metadata_;

    // Create decoded image for caching
    decoded_image_ = std::make_unique<Image>();

    // Step 1: Decode header to get image dimensions and required buffer size
    unsigned long raw_needed_size = 0;
    if (!decoder_->decodeHeader(srcImage, raw_needed_size))
    {
        common::log_error("Failed to decode header");
        return false;
    }

    // Step 2: Resize decoded image to accommodate decoded data
    decoded_image_->resize(raw_needed_size);

    // Step 3: Perform actual decoding directly into cached image
    if (!decoder_->decode(srcImage, *decoded_image_))
    {
        common::log_error("Failed to decode image data");
        decoded_image_.reset(); // Clean up on failure
        return false;
    }

    // Mark as decoded
    is_decoded_ = true;

    common::log_info("ImageLoader::loadFileData: Decoded image data successfully");
    common::log_info("Width height: %d x %d", decoded_image_->info.width, decoded_image_->info.height);
    common::log_info("Size of decoded image: %d bytes", decoded_image_->size());
    // Return a deep copy of the cached decoded image
    outImage = decoded_image_->deepCopy();
    return true;
}

bool ImageLoader::loadFileData(const std::string& file_path)
{
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return false;
    }

    // Get file size
    std::streamsize file_size = file.tellg();
    if (file_size <= 0)
    {
        return false;
    }

    file.seekg(0, std::ios::beg);

    // Read file into vector
    raw_data_.resize(static_cast<size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(raw_data_.data()), file_size))
    {
        raw_data_.clear();
        return false;
    }

    return true;
}

bool ImageLoader::extractMetadata()
{
    if (raw_data_.empty())
    {
        return false;
    }

    // Detect format
    metadata_.format = ImageFormatDetector::detectFormat(raw_data_);
    if (metadata_.format == ImageFormat::UNKNOWN)
    {
        return false;
    }

    // For METADATA_ONLY strategy, we need to extract dimensions
    // This requires creating a temporary decoder
    std::unique_ptr<Decoder> temp_decoder;
    switch (metadata_.format)
    {
        case ImageFormat::JPEG:
            temp_decoder = std::make_unique<JPEGDecoder>();
            metadata_.TJPixelFormat = TJPF_RGB; //TODO: could be RGBA
            break;
        case ImageFormat::PNG:
            // temp_decoder = std::make_unique<PngDecoder>();
            // break;
        case ImageFormat::BMP:
            // temp_decoder = std::make_unique<BmpDecoder>();
            // break;
        default:
            return false;
    }

    // Create source image from raw data for header parsing
    Image temp_image(const_cast<unsigned char*>(raw_data_.data()), raw_data_.size(), false);
    temp_image.info = metadata_;
    
    // Try to get metadata without full decode
    unsigned long raw_needed_size = 0;
    if (temp_decoder->decodeHeader(temp_image, raw_needed_size))
    {
        metadata_ = temp_image.info; // Copy metadata from decoded header
        metadata_.is_valid = true;
        return true;
    }

    return false;
}

bool ImageLoader::createDecoder()
{
    switch (metadata_.format)
    {
        case ImageFormat::JPEG:
            decoder_ = std::make_unique<JPEGDecoder>();
            break;
        // case ImageFormat::PNG:
        //     decoder_ = std::make_unique<PngDecoder>();
        //     break;
        // case ImageFormat::BMP:
        //     decoder_ = std::make_unique<BmpDecoder>();
        //     break;
        default:
            common::log_error("Unsupported image format: %d", static_cast<int>(metadata_.format));
            return false;
    }

    return decoder_ != nullptr;
}

