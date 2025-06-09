#ifndef CODEC_H
#define CODEC_H

#include "FunnyFace/codecFactory.h"
#include "FunnyFace/image.h"

namespace funnyface
{

class JPEGDecoder : public Decoder
{
  public:
    JPEGDecoder(const ConfigBuilder& config)
    {
        // config is ignored here because Decoder reads image info.
        if ((d_handle_ = tjInitDecompress()) == nullptr)
        {
            common::errno_log("JPEGDecoder Constructor failed to init decompressor");
            common::errno_log((const char*) tjGetErrorStr2(d_handle_));
            return;
        }
    }

    JPEGDecoder()
    {
        if (d_handle_ != nullptr)
        {
            int tj_stat = tjDestroy(d_handle_);

            if (tj_stat != 0)
            {
                common::errno_log("JPEGDecoder::~JPEGDecoder Deconstructor failed to deinit decompressor");
                common::errno_log((const char*) tjGetErrorStr2(d_handle_));
            }
        }
    }

    bool decode(const Image& srcImage, Image& outImage) override
    {
        int tj_stat = tjDecompress2(d_handle_, srcImage.data(), srcImage.size(), outImage.data(), 0, 0, 0,
                                    srcImage.info.TJPixelFormat, TJFLAG_NOREALLOC);

        // TODO: beingUsed must be removed with a better sync method.
        if (tj_stat != 0)
        {
            common::errno_log("JPEGDecoder::decodeImage - Failed to decode image");
            common::errno_log((const char*) tjGetErrorStr2(d_handle_));
            outImage.setBeingUsed(false);
            return false;
        }
        outImage.setBeingUsed(true);
        return true;
    }

    bool decodeHeader(Image& srcImage, unsigned long& raw_needed_size) override
    {
        if (!getJPEGHeaderInfo(srcImage))
        {
            return false;
        }

        srcImage.info.pixelSizeBytes = tjPixelSize[static_cast<int>(srcImage.info.TJPixelFormat)];
        raw_needed_size = rawSizeInBytes(srcImage);
        return true;
    }

  private:
    bool getJPEGHeaderInfo(Image& image)
    {
        int sample_format{TJSAMP_UNKNOWN};
        int color_space{-1};
        int width{-1};
        int height{-1};

        int tj_stat =
            tjDecompressHeader3(d_handle_, image.data(), image.size(), &width, &height, &sample_format, &color_space);

        if (tj_stat != 0 || sample_format == -1 || color_space == -1)
        {
            common::errno_log("JPEGDecoder::getJPEGHeaderInfo - Failed to decompress image header");
            common::log_error("JPEGDecoder::getJPEGHeaderInfo - stat %d cs %d sf%d", tj_stat, color_space,
                              sample_format);
            common::errno_log((const char*) tjGetErrorStr2(d_handle_));
            return false;
        }

        image.info.width = static_cast<unsigned long>(width);
        image.info.height = static_cast<unsigned long>(height);
        image.info.TJSampleFormat = static_cast<TJSAMP>(sample_format);
        image.info.TJColorSpace = static_cast<TJCS>(color_space);
        return true;
    }

    unsigned long rawSizeInBytes(const Image& image)
    {
        // Compute image size from image info and pixel data.
        int pixelSizeBytes = tjPixelSize[static_cast<int>(image.info.TJPixelFormat)];
        return image.info.width * image.info.height * pixelSizeBytes;
    }
    // Decompress handler
    tjhandle d_handle_;
};

class JPEGEncoder : public Encoder
{

  public:
    JPEGEncoder(const ConfigBuilder& config)
    {
        if ((c_handle_ = tjInitCompress()) == nullptr)
        {
            common::errno_log("JPEGManager::JPEGManager Constructor failed to init compressor");
            common::errno_log((const char*) tjGetErrorStr2(c_handle_));
            return;
        }

        if (!config.get("quality", quality))
        {
            common::log_error("JPEGEncoder - Unable to load parameter quality");
        }

        if (!config.get("width", width))
        {
            common::log_error("JPEGEncoder - Unable to load parameter width");
        }

        if (!config.get("height", height))
        {
            common::log_error("JPEGEncoder - Unable to load parameter height");
        }

        if (!config.get("pixelFormat", pixelFormat))
        {
            common::log_error("JPEGEncoder - Unable to load parameter pixelFormat");
        }

        if (!config.get("chrominance_subsampling", chrominance_subsampling))
        {
            common::log_error("JPEGEncoder - Unable to load parameter chrominance_subsampling");
        }

        // Print the configured values
        common::log_info("JPEGEncoder - Configured values:");
        common::log_info("JPEGEncoder - Width: %dx%d", width, height);
        common::log_info("JPEGEncoder - Pixel Format: %d", static_cast<int>(pixelFormat));
        common::log_info("JPEGEncoder - Chrominance Subsampling: %d", static_cast<int>(chrominance_subsampling));
    }

    ~JPEGEncoder()
    {
        if (c_handle_ != nullptr)
        {
            // Deallocate data buffer
            int tj_stat = tjDestroy(c_handle_);

            if (tj_stat != 0)
            {
                common::errno_log("JPEGManager::~JPEGManager Deconstructor failed to deinit compressor");
                common::errno_log((const char*) tjGetErrorStr2(c_handle_));
            }
        }
    }

    bool encode(const Image& srcImage, Image& outImage, unsigned long& compressedSize) override
    {
        // Validate input parameters
        if (!srcImage.data() || srcImage.size() == 0)
        {
            common::log_error("JPEGEncoder::encode - Invalid source image");
            return false;
        }

        // Validate that the source image matches our configured parameters
        if (static_cast<int>(srcImage.info.width) != width || 
            static_cast<int>(srcImage.info.height) != height)
        {
            common::log_error("JPEGEncoder::encode - Image size mismatch: expected %dx%d, got %lux%lu", 
                             width, height, srcImage.info.width, srcImage.info.height);
            return false;
        }

        // Validate pixel format compatibility
        TJPF sourcePixelFormat = srcImage.info.TJPixelFormat;
        if (sourcePixelFormat != pixelFormat)
        {
            common::log_warn("JPEGEncoder::encode - Pixel format mismatch: expected %d, got %d", 
                            static_cast<int>(pixelFormat), static_cast<int>(sourcePixelFormat));
            // Use the source image pixel format for encoding
            sourcePixelFormat = srcImage.info.TJPixelFormat;
        }

        // Validate buffer size
        size_t expectedSize = width * height * tjPixelSize[sourcePixelFormat];
        if (srcImage.size() < expectedSize)
        {
            common::log_error("JPEGEncoder::encode - Source buffer too small: expected %zu, got %zu", 
                             expectedSize, srcImage.size());
            return false;
        }

        unsigned char* outImageData = outImage.data();
        unsigned long outImageSize = outImage.size();

        // Ensure the pitch is properly calculated (width * pixel_size)
        int pitch = width * tjPixelSize[sourcePixelFormat];

        int tj_stat = tjCompress2(c_handle_, srcImage.data(), width, pitch, height, sourcePixelFormat, 
                                  &outImageData, &outImageSize, chrominance_subsampling, quality, TJFLAG_NOREALLOC);

        if (tj_stat != 0)
        {
            common::errno_log("JPEGEncoder::encode - Compressor failed to compress!");
            common::errno_log((const char*) tjGetErrorStr2(c_handle_));
            common::log_error("JPEGEncoder::encode - Parameters: w=%d h=%d pitch=%d pf=%d cs=%d q=%d", 
                             width, height, pitch, static_cast<int>(sourcePixelFormat), 
                             static_cast<int>(chrominance_subsampling), quality);
            return false;
        }

        compressedSize = outImageSize;
        return true;
    }

    unsigned long encodeSizeInBytes() override { return tjBufSize(width, height, chrominance_subsampling); }



  private:
    // Compress handler
    tjhandle c_handle_;

    int quality;
    int width;
    int height;
    TJPF pixelFormat;
    TJSAMP chrominance_subsampling;
};
class RAWEncoder : public Encoder
{
  public:
    RAWEncoder(const ConfigBuilder& config)
    {
        // RAW encoder does not need any specific configuration.
        common::log_info("RAWEncoder - Initialized with default settings");
    }

    bool encode(const Image& srcImage, Image& outImage, unsigned long& compressedSize)
    {
        if (outImage.size() == 0)
        {
            outImage.resize(srcImage.size());
            common::log_warn("RAWEncoder::encode - Resizing output image to %lu bytes", srcImage.size());
        }

        // For RAW encoding, we simply copy the data without compression.
        outImage.copyFrom(srcImage);
        compressedSize = srcImage.size();
        return true;
    }

    unsigned long encodeSizeInBytes() override
    {
        return 0; // RAW encoding does not have a fixed size.
    }
};

class RAWDecoder : public Decoder
{
  public:
    RAWDecoder(const ConfigBuilder& config)
    {
        // RAW decoder does not need any specific configuration.
        common::log_info("RAWDecoder - Initialized with default settings");
    }

    bool decode(const Image& srcImage, Image& outImage)
    {
        if (outImage.size() == 0)
        {
            outImage.resize(srcImage.size());
            common::log_warn("RAWDecoder::decode - Resizing output image to %lu bytes", srcImage.size());
        }

        // For RAW decoding, we simply copy the data without decompression.
        outImage.copyFrom(srcImage);

        outImage.setBeingUsed(true);
        return true;
    }

    bool decodeHeader(Image& srcImage, unsigned long& raw_needed_size) override
    {
        raw_needed_size = srcImage.size();
        return true;
    }
};

class BayerGBRGDecoder : public Decoder
{
  public:
    BayerGBRGDecoder(const ConfigBuilder& config)
    {
        // BayerGBRG decoder does not need any specific configuration.
        if (!config.get("width", width_))
        {
            common::log_error("BayerGBRGDecoder - Unable to load parameter width");
        }

        if (!config.get("height", height_))
        {
            common::log_error("BayerGBRGDecoder - Unable to load parameter height");
        }
        
        common::log_info("BayerGBRGDecoder - Initialized with %dx%d", width_, height_);
    }

    bool decode(const Image& srcImage, Image& outImage) override
    {
        if (srcImage.size() == 0)
        {
            common::log_error("BayerGBRGDecoder::decode - Source image is empty");
            return false;
        }

        // Calculate RGB output size (3 bytes per pixel)
        unsigned long rgbSize = width_ * height_ * 3;

        if (outImage.size() != rgbSize)
        {
            outImage.resize(rgbSize);
            common::log_warn("BayerGBRGDecoder::decode - Resizing output image to %lu bytes", rgbSize);
        }

        // Set output image info
        outImage.info = srcImage.info;
        outImage.info.width = width_;
        outImage.info.height = height_;
        outImage.info.pixelSizeBytes = 3; // RGB
        outImage.info.TJPixelFormat = TJPF_RGB;
        
        // Demosaic the Bayer GBRG image to RGB
        bool result = demosaicGBRG(srcImage, outImage);
        if (result)
        {
            outImage.setBeingUsed(true);
        }
        else
        {
            outImage.setBeingUsed(false);
        }
        return result;
    }

    bool decodeHeader(Image& srcImage, unsigned long& raw_needed_size) override
    {
        srcImage.info.width = width_;
        srcImage.info.height = height_;
        srcImage.info.pixelSizeBytes = 3; // RGB output
        srcImage.info.TJPixelFormat = TJPF_RGB;

        // RGB output needs 3 times the space (3 bytes per pixel vs 1 byte per pixel for Bayer)
        raw_needed_size = srcImage.info.width * srcImage.info.height * 3;
        return true;
    }

  private:
    // Bilinear demosaicing for GBRG Bayer pattern to RGB
    bool demosaicGBRG(const Image& bayerImage, Image& rgbImage)
    {
        const unsigned char* bayer = bayerImage.data();
        unsigned char* rgb = rgbImage.data();

        if (!bayer || !rgb)
        {
            common::log_error("BayerGBRGDecoder::demosaicGBRG - Invalid data pointers");
            return false;
        }

        int width = width_;
        int height = height_;

        // Validate input size
        if (bayerImage.size() < static_cast<size_t>(width * height))
        {
            common::log_error("BayerGBRGDecoder::demosaicGBRG - Input buffer too small: %zu < %d", 
                             bayerImage.size(), width * height);
            return false;
        }

        // Handle border pixels first (simple nearest neighbor)
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                int bayerIndex = y * width + x;
                int rgbIndex = (y * width + x) * 3;

                if (x == 0 || x == width - 1 || y == 0 || y == height - 1)
                {
                    // Border pixels - use simple replication
                    uint8_t value = bayer[bayerIndex];
                    rgb[rgbIndex] = value;     // R
                    rgb[rgbIndex + 1] = value; // G
                    rgb[rgbIndex + 2] = value; // B
                    continue;
                }

                // Interior pixels - proper demosaicing
                int up = (y - 1) * width + x;
                int down = (y + 1) * width + x;
                int left = y * width + (x - 1);
                int right = y * width + (x + 1);

                uint8_t value = bayer[bayerIndex];

                if ((y % 2 == 0) && (x % 2 == 0))
                {
                    // G pixel (even row, even col in GBRG)
                    rgb[rgbIndex] = (bayer[up] + bayer[down]) / 2;        // R
                    rgb[rgbIndex + 1] = value;                            // G
                    rgb[rgbIndex + 2] = (bayer[left] + bayer[right]) / 2; // B
                }
                else if ((y % 2 == 0) && (x % 2 == 1))
                {
                    // B pixel
                    rgb[rgbIndex] = (bayer[up - 1] + bayer[up + 1] + bayer[down - 1] + bayer[down + 1]) / 4; // R
                    rgb[rgbIndex + 1] = (bayer[left] + bayer[right] + bayer[up] + bayer[down]) / 4;          // G
                    rgb[rgbIndex + 2] = value;                                                               // B
                }
                else if ((y % 2 == 1) && (x % 2 == 0))
                {
                    // R pixel
                    rgb[rgbIndex] = value;                                                                       // R
                    rgb[rgbIndex + 1] = (bayer[left] + bayer[right] + bayer[up] + bayer[down]) / 4;              // G
                    rgb[rgbIndex + 2] = (bayer[up - 1] + bayer[up + 1] + bayer[down - 1] + bayer[down + 1]) / 4; // B
                }
                else
                {
                    // G pixel (odd row, odd col)
                    rgb[rgbIndex] = (bayer[left] + bayer[right]) / 2;  // R
                    rgb[rgbIndex + 1] = value;                         // G
                    rgb[rgbIndex + 2] = (bayer[up] + bayer[down]) / 2; // B
                }
            }
        }

        return true;
    }
    int width_{0};
    int height_{0};
};

} // namespace funnyface
#endif // CODEC_H
