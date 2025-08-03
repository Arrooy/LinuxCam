#ifndef CODEC_H
#define CODEC_H

#include <algorithm>
#include <functional>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/codecFactory.h"

namespace linuxface
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
        if ((d_handle_ = tjInitDecompress()) == nullptr)
        {
            common::errno_log("JPEGDecoder Constructor failed to init decompressor");
            common::errno_log((const char*) tjGetErrorStr2(d_handle_));
            return;
        }
    }

    ~JPEGDecoder()
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

        if (tj_stat != 0)
        {
            common::errno_log("JPEGDecoder::decodeImage - Failed to decode image");
            common::errno_log((const char*) tjGetErrorStr2(d_handle_));
            return false;
        }
        outImage.info.width = srcImage.info.width;
        outImage.info.height = srcImage.info.height;
        outImage.info.x = srcImage.info.x;
        outImage.info.y = srcImage.info.y;
        outImage.info.format = ImageFormat::RGB;
        outImage.info.TJPixelFormat = srcImage.info.TJPixelFormat;
        outImage.info.TJColorSpace = srcImage.info.TJColorSpace;
        outImage.info.pixelSizeBytes = 3;
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
        int sample_format{TJSAMP_444};
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
        image.info.TJPixelFormat = TJPF_RGB; // Default to RGB
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
        if (static_cast<int>(srcImage.info.width) != width || static_cast<int>(srcImage.info.height) != height)
        {
            common::log_error("JPEGEncoder::encode - Image size mismatch: expected %dx%d, got %lux%lu", width, height,
                              srcImage.info.width, srcImage.info.height);
            return false;
        }

        // Validate pixel format compatibility
        TJPF sourcePixelFormat = srcImage.info.TJPixelFormat;
        if (sourcePixelFormat != pixelFormat)
        {
            common::log_warn("JPEGEncoder::encode - Pixel format mismatch: expected %d, got %d",
                             static_cast<int>(pixelFormat), static_cast<int>(sourcePixelFormat));
        }

        // Validate buffer size
        size_t expectedSize = width * height * tjPixelSize[sourcePixelFormat];
        if (srcImage.size() < expectedSize)
        {
            common::log_error("JPEGEncoder::encode - Source buffer too small: expected %zu, got %zu", expectedSize,
                              srcImage.size());
            return false;
        }

        unsigned char* outImageData = outImage.data();
        unsigned long outImageSize = outImage.size();

        // Ensure the pitch is properly calculated (width * pixel_size)
        int pitch = width * tjPixelSize[sourcePixelFormat];

        int tj_stat = tjCompress2(c_handle_, srcImage.data(), width, pitch, height, sourcePixelFormat, &outImageData,
                                  &outImageSize, chrominance_subsampling, quality, TJFLAG_NOREALLOC);

        if (tj_stat != 0)
        {
            common::errno_log("JPEGEncoder::encode - Compressor failed to compress!");
            common::errno_log((const char*) tjGetErrorStr2(c_handle_));
            common::log_error("JPEGEncoder::encode - Parameters: w=%d h=%d pitch=%d pf=%d cs=%d q=%d", width, height,
                              pitch, static_cast<int>(sourcePixelFormat), static_cast<int>(chrominance_subsampling),
                              quality);
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
        return demosaicGBRG(srcImage, outImage);
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
            common::log_error("BayerGBRGDecoder::demosaicGBRG - Input buffer too small: %zu < %d", bayerImage.size(),
                              width * height);
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

class DepthZ16Decoder : public Decoder
{
  public:
    DepthZ16Decoder(const ConfigBuilder& config)
    {
        // Load configuration parameters with defaults
        if (!config.get("width", width_))
        {
            common::log_error("DepthZ16Decoder - Unable to load parameter width");
        }

        if (!config.get("height", height_))
        {
            common::log_error("DepthZ16Decoder - Unable to load parameter height");
        }

        if (!config.get("max_depth", max_depth_))
        {
            max_depth_ = 65535; // Default to 16-bit max value
            common::log_warn("DepthZ16Decoder - Using default max_depth: %d", max_depth_);
        }

        if (!config.get("color_map", color_map_))
        {
            color_map_ = 3; // Default to grayscale
            common::log_warn("DepthZ16Decoder - Using default color_map: grayscale");
        }

        common::log_info("DepthZ16Decoder - Initialized with %dx%d, max_depth: %d, color_map: %d", width_, height_,
                         max_depth_, color_map_);
    }

    bool decode(const Image& srcImage, Image& outImage) override
    {
        if (srcImage.size() == 0)
        {
            common::log_error("DepthZ16Decoder::decode - Source image is empty");
            return false;
        }

        // Validate input buffer size
        size_t expectedSize = width_ * height_ * 2; // 2 bytes per pixel for Z16
        if (srcImage.size() < expectedSize)
        {
            common::log_error("DepthZ16Decoder::decode - Source buffer too small: %zu < %zu", srcImage.size(),
                              expectedSize);
            return false;
        }

        // Calculate RGB output size and resize if needed
        unsigned long rgbSize = width_ * height_ * 3;
        if (outImage.size() != rgbSize)
        {
            outImage.resize(rgbSize);
            common::log_warn("DepthZ16Decoder::decode - Resizing output image to %lu bytes", rgbSize);
        }

        // Set output image info
        outImage.info = srcImage.info;
        outImage.info.width = width_;
        outImage.info.height = height_;
        outImage.info.pixelSizeBytes = 3;
        outImage.info.TJPixelFormat = TJPF_RGB;

        // Convert depth data to RGB
        const unsigned short* depthData = reinterpret_cast<const unsigned short*>(srcImage.data());
        unsigned char* rgbData = outImage.data();
        unsigned long width = static_cast<unsigned long>(width_);
        unsigned long height = static_cast<unsigned long>(height_);
        for (unsigned long row = 0; row < height; ++row)
        {
            for (unsigned long col = 0; col < width; ++col)
            {
                unsigned long depthIdx = row * width_ + col;
                unsigned long rgbIdx = depthIdx * 3;

                unsigned short depthValue = depthData[depthIdx];

                // Convert depth to RGB based on color map
                unsigned char r, g, b;
                depthToRGB(depthValue, r, g, b);

                rgbData[rgbIdx] = r;
                rgbData[rgbIdx + 1] = g;
                rgbData[rgbIdx + 2] = b;
            }
        }
        return true;
    }

    bool decodeHeader(Image& srcImage, unsigned long& raw_needed_size) override
    {
        srcImage.info.width = width_;
        srcImage.info.height = height_;
        srcImage.info.pixelSizeBytes = 3; // RGB output
        srcImage.info.TJPixelFormat = TJPF_RGB;

        raw_needed_size = width_ * height_ * 2;
        return true;
    }

  private:
    void depthToRGB(unsigned short depth, unsigned char& r, unsigned char& g, unsigned char& b)
    {
        if (depth == 0)
        {
            // Invalid depth (no measurement)
            r = g = b = 0;
            return;
        }

        switch (color_map_)
        {
            case 0: // Grayscale
            {
                float normalized = static_cast<float>(depth) / max_depth_;
                unsigned char intensity = static_cast<unsigned char>(normalized * 255);
                r = g = b = intensity;
                break;
            }
            case 1: // Jet colormap (blue to red)
            {
                float normalized = static_cast<float>(depth) / max_depth_;
                jetColorMap(normalized, r, g, b);
                break;
            }
            case 2: // Hot colormap (black to red to yellow to white)
            {
                float normalized = static_cast<float>(depth) / max_depth_;
                hotColorMap(normalized, r, g, b);
                break;
            }
            default: // Simple RGB split
            {
                r = static_cast<unsigned char>(depth & 0xFF);
                g = static_cast<unsigned char>((depth >> 8) & 0xFF);
                b = 0;
                break;
            }
        }
    }

    void jetColorMap(float value, unsigned char& r, unsigned char& g, unsigned char& b)
    {
        // Clamp value to [0, 1]
        value = std::max(0.0f, std::min(1.0f, value));

        if (value < 0.25f)
        {
            r = 0;
            g = static_cast<unsigned char>(255 * 4 * value);
            b = 255;
        }
        else if (value < 0.5f)
        {
            r = 0;
            g = 255;
            b = static_cast<unsigned char>(255 * (1 - 4 * (value - 0.25f)));
        }
        else if (value < 0.75f)
        {
            r = static_cast<unsigned char>(255 * 4 * (value - 0.5f));
            g = 255;
            b = 0;
        }
        else
        {
            r = 255;
            g = static_cast<unsigned char>(255 * (1 - 4 * (value - 0.75f)));
            b = 0;
        }
    }

    void hotColorMap(float value, unsigned char& r, unsigned char& g, unsigned char& b)
    {
        // Clamp value to [0, 1]
        value = std::max(0.0f, std::min(1.0f, value));

        if (value < 0.33f)
        {
            r = static_cast<unsigned char>(255 * 3 * value);
            g = 0;
            b = 0;
        }
        else if (value < 0.66f)
        {
            r = 255;
            g = static_cast<unsigned char>(255 * 3 * (value - 0.33f));
            b = 0;
        }
        else
        {
            r = 255;
            g = 255;
            b = static_cast<unsigned char>(255 * 3 * (value - 0.66f));
        }
    }

    int width_{0};
    int height_{0};
    int max_depth_{65535};
    int color_map_{0}; // 0=grayscale, 1=jet, 2=hot, 3=simple RGB split
};
class YUV422 : public Decoder
{
  public:
    struct YUV422Block
    {
        uint8_t y0;
        uint8_t u;
        uint8_t v;
        uint8_t y1;
    };
    using ConversionFunct = std::function<YUV422Block(const uint8_t* block)>;

    struct YUVtoRGBLUT
    {
        std::array<int16_t, 256> Cr_r{};
        std::array<int16_t, 256> Cb_b{};
        std::array<int16_t, 256> Cr_g{};
        std::array<int16_t, 256> Cb_g{};

        YUVtoRGBLUT()
        {
            for (int i = 0; i < 256; i++)
            {
                int val = i - 128;
                Cr_r[i] = static_cast<int16_t>(1.402 * val);
                Cb_b[i] = static_cast<int16_t>(1.772 * val);
                Cr_g[i] = static_cast<int16_t>(-0.71414 * val);
                Cb_g[i] = static_cast<int16_t>(-0.34414 * val);
            }
        }
    };


    YUV422(const ConfigBuilder& config)
    {
        if (!config.get("width", width_))
        {
            common::log_error("YUV422 - Unable to load parameter width");
        }

        if (!config.get("height", height_))
        {
            common::log_error("YUV422 - Unable to load parameter height");
        }
        lut_ = YUVtoRGBLUT();
        common::log_info("YUV422 - Initialized with %dx%d", width_, height_);
    }

    template <typename F>
    bool genericDecode(const Image& srcImage, Image& outImage, F pxConvert)
    {
        if (srcImage.size() == 0)
        {
            common::log_error("YUV422::decode - Source image is empty");
            return false;
        }

        const size_t expectedSize = width_ * height_ * 2;
        if (srcImage.size() < expectedSize)
        {
            common::log_error("YUV422::decode - Input buffer too small: %zu < %zu", srcImage.size(), expectedSize);
            return false;
        }

        const size_t rgbSize = width_ * height_ * 3;
        if (outImage.size() != rgbSize)
        {
            outImage.resize(rgbSize);
            common::log_warn("YUV422::decode - Resizing output image to %lu bytes", rgbSize);
        }

        outImage.info = srcImage.info;
        outImage.info.width = width_;
        outImage.info.height = height_;
        outImage.info.pixelSizeBytes = 3;
        outImage.info.TJPixelFormat = TJPF_RGB;

        bool result = decodeYUV(srcImage, outImage, pxConvert);
        return result;
    }

    bool decodeHeader(Image& srcImage, unsigned long& raw_needed_size) override
    {
        srcImage.info.width = width_;
        srcImage.info.height = height_;
        srcImage.info.pixelSizeBytes = 3;
        srcImage.info.TJPixelFormat = TJPF_RGB;

        raw_needed_size = width_ * height_ * 2;
        return true;
    }

  private:
    bool decodeYUV(const Image& yuvImage, Image& rgbImage, ConversionFunct funct)
    {
        const uint8_t* yuv = yuvImage.data();
        uint8_t* rgb = rgbImage.data();

        if (!yuv || !rgb)
        {
            common::log_error("YUV422::decodeYUV - Invalid data pointers");
            return false;
        }

        const int pixels = width_ * height_;
        uint8_t* rgbPtr = rgb;
        for (int i = 0, j = 0; i < pixels; i += 2, j += 4)
        {
            YUV422Block block = funct(yuv + j);

            convertYUVtoRGB(block.y0, block.u, block.v, rgbPtr);
            rgbPtr += 3;
            convertYUVtoRGB(block.y1, block.u, block.v, rgbPtr);
            rgbPtr += 3;
        }

        return true;
    }

    __attribute__((always_inline)) inline void convertYUVtoRGB(uint8_t y, uint8_t u, uint8_t v, uint8_t* rgb)
    {
        // YUV to RGB conversion using the YUVtoRGBLUT
        int C = static_cast<int>(y) - 16;
        if (C < 0)
        {
            C = 0;
        }

        int R = C + lut_.Cr_r[v];
        int G = C + lut_.Cb_g[u] + lut_.Cr_g[v];
        int B = C + lut_.Cb_b[u];

        // Clamp the results between 0 and 255
        rgb[0] = static_cast<uint8_t>(common::clamp(R, 0, 255));
        rgb[1] = static_cast<uint8_t>(common::clamp(G, 0, 255));
        rgb[2] = static_cast<uint8_t>(common::clamp(B, 0, 255));
    }

    int width_{0};
    int height_{0};
    YUVtoRGBLUT lut_;
};

class UYVY422Decoder : public YUV422
{
  public:
    UYVY422Decoder(const ConfigBuilder& config) : YUV422(config) {}

    bool decode(const Image& srcImage, Image& outImage) override
    {
        return genericDecode(srcImage, outImage, UYVY422BlockOrder);
    }

    static YUV422Block UYVY422BlockOrder(const uint8_t* block)
    {
        return {.y0 = block[1], .u = block[0], .v = block[2], .y1 = block[3]};
    }
};
class YUYV422Decoder : public YUV422
{
  public:
    YUYV422Decoder(const ConfigBuilder& config) : YUV422(config) {}

    bool decode(const Image& srcImage, Image& outImage) override
    {
        return genericDecode(srcImage, outImage, YUYV422BlockOrder);
    }

    static YUV422Block YUYV422BlockOrder(const uint8_t* block)
    {
        return {.y0 = block[0], .u = block[1], .v = block[3], .y1 = block[2]};
    }
};
class PPMDecoder : public Decoder
{
  public:
    PPMDecoder() {}
    ~PPMDecoder() override = default;

    bool decode(const Image& srcImage, Image& outImage) override
    {
        // Parse header
        size_t header_end = 0;
        unsigned long width = 0, height = 0;
        unsigned int maxval = 0;
        if (!parseHeader(srcImage.data(), srcImage.size(), header_end, width, height, maxval))
        {
            common::log_error("PPMDecoder::decode - Failed to parse PPM header");
            return false;
        }
        size_t pixel_data_size = width * height * 3;
        if (srcImage.size() < header_end + pixel_data_size)
        {
            common::log_error("PPMDecoder::decode - Not enough data for pixel array");
            return false;
        }
        outImage.resize(pixel_data_size);
        memcpy(outImage.data(), srcImage.data() + header_end, pixel_data_size);
        outImage.info.width = width;
        outImage.info.height = height;
        outImage.info.pixelSizeBytes = 3;
        outImage.info.format = ImageFormat::RGB;
        outImage.info.TJPixelFormat = TJPF_RGB;
        outImage.info.is_valid = true;
        return true;
    }

    bool decodeHeader(Image& srcImage, unsigned long& raw_needed_size) override
    {
        size_t header_end = 0;
        unsigned long width = 0, height = 0;
        unsigned int maxval = 0;
        bool header_ok = parseHeader(srcImage.data(), srcImage.size(), header_end, width, height, maxval);
        if (!header_ok)
        {
            common::log_error("PPMDecoder::decodeHeader - Failed to parse header");
            common::log_error("PPMDecoder::decodeHeader - Raw size: %zu", srcImage.size());
            return false;
        }
        size_t pixel_data_size = width * height * 3;
        if (srcImage.size() < header_end + pixel_data_size)
        {
            common::log_error("PPMDecoder::decodeHeader - Invalid header. Not enough data for pixel array");
            return false;
        }
        common::log_info("PPMDecoder::decodeHeader - Parsed header: width=%lu height=%lu maxval=%u header_end=%zu",
                         width, height, maxval, header_end);
        srcImage.info.width = width;
        srcImage.info.height = height;
        srcImage.info.pixelSizeBytes = 3;
        srcImage.info.format = ImageFormat::PPM;
        srcImage.info.TJPixelFormat = TJPF_RGB;
        raw_needed_size = pixel_data_size;
        return true;
    }

  private:
    // Minimal P6 PPM header parser
    bool parseHeader(const unsigned char* data, size_t size, size_t& header_end, unsigned long& width,
                     unsigned long& height, unsigned int& maxval)
    {
        if (size < 3 || data[0] != 'P' || data[1] != '6')
        {
            common::log_error("PPMDecoder::parseHeader - Not a P6 PPM file");
            return false;
        }
        size_t pos = 2;
        // Helper to skip whitespace and comments
        auto skipWhitespaceAndComments = [&](size_t& p)
        {
            while (p < size)
            {
                // Skip whitespace
                while (p < size && (data[p] == ' ' || data[p] == '\n' || data[p] == '\r' || data[p] == '\t'))
                {
                    p++;
                }
                // Skip comment lines
                if (p < size && data[p] == '#')
                {
                    while (p < size && data[p] != '\n')
                    {
                        p++;
                    }
                }
                else
                {
                    break;
                }
            }
        };
        // Read width, height, maxval
        auto readInt = [&](size_t& p, unsigned long& out)
        {
            out = 0;
            skipWhitespaceAndComments(p);
            if (p >= size)
            {
                common::log_error("PPMDecoder::parseHeader - Unexpected end of file while reading int");
                return false;
            }
            bool found_digit = false;
            while (p < size && data[p] >= '0' && data[p] <= '9')
            {
                out = out * 10 + (data[p] - '0');
                p++;
                found_digit = true;
            }
            if (!found_digit)
            {
                common::log_error("PPMDecoder::parseHeader - No digits found for int");
                return false;
            }
            return true;
        };
        if (!readInt(pos, width) || width == 0)
        {
            common::log_error("PPMDecoder::parseHeader - Failed to read width or width is zero");
            return false;
        }
        if (!readInt(pos, height) || height == 0)
        {
            common::log_error("PPMDecoder::parseHeader - Failed to read height or height is zero");
            return false;
        }
        unsigned long maxv = 0;
        if (!readInt(pos, maxv) || maxv == 0)
        {
            common::log_error("PPMDecoder::parseHeader - Failed to read maxval or maxval is zero");
            return false;
        }
        maxval = static_cast<unsigned int>(maxv);
        // Skip single whitespace after maxval and comments
        skipWhitespaceAndComments(pos);
        if (pos >= size)
        {
            common::log_error("PPMDecoder::parseHeader - Header ends past file size");
            return false;
        }
        header_end = pos;
        common::log_info("PPMDecoder::parseHeader - width=%lu height=%lu maxval=%u header_end=%zu", width, height,
                         maxval, header_end);
        return true;
    }
};
} // namespace linuxface
#endif // CODEC_H
