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
        outImage.setBeingUsed(true);
        if (tj_stat != 0)
        {
            common::errno_log("JPEGDecoder::decodeImage - Failed to decode image");
            common::errno_log((const char*) tjGetErrorStr2(d_handle_));
            return false;
        }
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
        unsigned char* outImageData = outImage.data();
        unsigned long outImageSize = outImage.size();

        int tj_stat = tjCompress2(c_handle_, srcImage.data(), width, 0, height, pixelFormat, &outImageData,
                                  &outImageSize, chrominance_subsampling, quality, TJFLAG_NOREALLOC);

        if (tj_stat != 0)
        {
            common::errno_log("JPEGEncoder::encode - Compressor failed to compress!");
            common::errno_log((const char*) tjGetErrorStr2(c_handle_));
            return false;
        }

        compressedSize = outImageSize;
        return true;
    }

    unsigned long encodeSizeInBytes() override
    {
        return tjBufSize(width, height, chrominance_subsampling);
    }



  private:
    // Compress handler
    tjhandle c_handle_;

    int quality;
    int width;
    int height;
    TJPF pixelFormat;
    TJSAMP chrominance_subsampling;
};
class RAWEncoder
{
};
class RAWDecoder
{
};

} // namespace funnyface
#endif // CODEC_H
