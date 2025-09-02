#include "LinuxFace/Image/image_processor.h"

#include <stdexcept>

namespace linuxface::image
{

void ImageProcessor::processPixel(const uint8_t* src, uint8_t* dst, PixelFormat srcFormat, PixelFormat dstFormat,
                                  bool enableBlending, uint8_t alpha) noexcept
{
    if (enableBlending && canBlend(srcFormat, dstFormat))
    {
        AlphaBlender::blendPixel(src, dst, srcFormat, dstFormat, alpha);
    }
    else
    {
        PixelConverter::convertPixel(src, dst, srcFormat, dstFormat);
    }
}

void ImageProcessor::processRow(const uint8_t* srcRow, uint8_t* dstRow, size_t pixelCount, PixelFormat srcFormat,
                                PixelFormat dstFormat, bool enableBlending, uint8_t alpha) noexcept
{
    if (enableBlending && canBlend(srcFormat, dstFormat))
    {
        AlphaBlender::blendRow(srcRow, dstRow, pixelCount, srcFormat, dstFormat, alpha);
    }
    else
    {
        PixelConverter::convertRow(srcRow, dstRow, pixelCount, srcFormat, dstFormat);
    }
}

void ImageProcessor::processImage(const uint8_t* srcData, uint8_t* dstData, size_t width, size_t height,
                                  size_t srcStride, size_t dstStride, PixelFormat srcFormat, PixelFormat dstFormat,
                                  bool enableBlending, uint8_t alpha)
{
    if (!srcData || !dstData)
    {
        throw std::invalid_argument("Null image data pointers");
    }

    if (width == 0 || height == 0)
    {
        throw std::invalid_argument("Invalid image dimensions");
    }

    // Validate stride constraints
    const size_t minSrcStride = width * PixelFormatInfo::getBytesPerPixel(srcFormat);
    const size_t minDstStride = width * PixelFormatInfo::getBytesPerPixel(dstFormat);

    if (srcStride < minSrcStride || dstStride < minDstStride)
    {
        throw std::invalid_argument("Stride too small for image width");
    }

    // Process row by row
    for (size_t y = 0; y < height; ++y)
    {
        const uint8_t* srcRow = srcData + y * srcStride;
        uint8_t* dstRow = dstData + y * dstStride;

        processRow(srcRow, dstRow, width, srcFormat, dstFormat, enableBlending, alpha);
    }
}

void ImageProcessor::convertImage(const uint8_t* srcData, uint8_t* dstData, size_t width, size_t height,
                                  size_t srcStride, size_t dstStride, PixelFormat srcFormat, PixelFormat dstFormat)
{
    processImage(srcData, dstData, width, height, srcStride, dstStride, srcFormat, dstFormat, false, 255);
}

void ImageProcessor::blendImage(const uint8_t* srcData, uint8_t* dstData, size_t width, size_t height, size_t srcStride,
                                size_t dstStride, PixelFormat srcFormat, PixelFormat dstFormat, uint8_t alpha)
{
    if (!canBlend(srcFormat, dstFormat))
    {
        throw std::invalid_argument("Unsupported format combination for blending");
    }

    processImage(srcData, dstData, width, height, srcStride, dstStride, srcFormat, dstFormat, true, alpha);
}

size_t ImageProcessor::calculateBufferSize(size_t width, size_t height, PixelFormat format) noexcept
{
    return width * height * PixelFormatInfo::getBytesPerPixel(format);
}

size_t ImageProcessor::calculateStride(size_t width, PixelFormat format, size_t alignment) noexcept
{
    const size_t bytesPerRow = width * PixelFormatInfo::getBytesPerPixel(format);
    return ((bytesPerRow + alignment - 1) / alignment) * alignment; // Align to boundary
}

} // namespace linuxface::image
