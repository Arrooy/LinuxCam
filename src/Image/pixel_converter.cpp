#include "LinuxFace/Image/pixel_converter.h"
#include <cstring>

namespace linuxface::image
{

void PixelConverter::convertPixel(const uint8_t* src, uint8_t* dst, 
                                 PixelFormat srcFormat, PixelFormat dstFormat) noexcept
{
    if (srcFormat == dstFormat)
    {
        copyPixel(src, dst, srcFormat);
        return;
    }
    
    // Delegate to specific conversion functions
    switch (srcFormat)
    {
        case PixelFormat::RGB:
            convertFromRGB(src, dst, dstFormat);
            break;
        case PixelFormat::RGBA:
            convertFromRGBA(src, dst, dstFormat);
            break;
        case PixelFormat::GRAYSCALE:
            convertFromGrayscale(src, dst, dstFormat);
            break;
    }
}

void PixelConverter::convertRow(const uint8_t* srcRow, uint8_t* dstRow, size_t pixelCount,
                               PixelFormat srcFormat, PixelFormat dstFormat) noexcept
{
    if (srcFormat == dstFormat)
    {
        copyRow(srcRow, dstRow, pixelCount, srcFormat);
        return;
    }
    
    const uint8_t srcBytes = PixelFormatInfo::getBytesPerPixel(srcFormat);
    const uint8_t dstBytes = PixelFormatInfo::getBytesPerPixel(dstFormat);
    
    for (size_t i = 0; i < pixelCount; ++i)
    {
        convertPixel(srcRow + i * srcBytes, dstRow + i * dstBytes, srcFormat, dstFormat);
    }
}

void PixelConverter::copyPixel(const uint8_t* src, uint8_t* dst, PixelFormat format) noexcept
{
    const uint8_t bytes = PixelFormatInfo::getBytesPerPixel(format);
    std::memcpy(dst, src, bytes);
}

void PixelConverter::copyRow(const uint8_t* src, uint8_t* dst, size_t pixelCount, PixelFormat format) noexcept
{
    const size_t totalBytes = pixelCount * PixelFormatInfo::getBytesPerPixel(format);
    std::memcpy(dst, src, totalBytes);
}

void PixelConverter::convertFromRGB(const uint8_t* src, uint8_t* dst, PixelFormat dstFormat) noexcept
{
    switch (dstFormat)
    {
        case PixelFormat::RGB:
            dst[0] = src[0]; // R
            dst[1] = src[1]; // G  
            dst[2] = src[2]; // B
            break;
        case PixelFormat::RGBA:
            dst[0] = src[0]; // R
            dst[1] = src[1]; // G  
            dst[2] = src[2]; // B
            dst[3] = 255;    // A (opaque)
            break;
        case PixelFormat::GRAYSCALE:
            dst[0] = rgbToGrayscale(src[0], src[1], src[2]);
            break;
        default:
            break;
    }
}

void PixelConverter::convertFromRGBA(const uint8_t* src, uint8_t* dst, PixelFormat dstFormat) noexcept
{
    switch (dstFormat)
    {
        case PixelFormat::RGBA:
            dst[0] = src[0]; // R
            dst[1] = src[1]; // G
            dst[2] = src[2]; // B
            dst[3] = src[3]; // A
            break;
        case PixelFormat::RGB:
            dst[0] = src[0]; // R
            dst[1] = src[1]; // G
            dst[2] = src[2]; // B (drop alpha)
            break;
        case PixelFormat::GRAYSCALE:
            dst[0] = rgbToGrayscale(src[0], src[1], src[2]);
            break;
        default:
            break;
    }
}

void PixelConverter::convertFromGrayscale(const uint8_t* src, uint8_t* dst, PixelFormat dstFormat) noexcept
{
    const uint8_t gray = src[0];
    switch (dstFormat)
    {
        case PixelFormat::RGB:
            dst[0] = dst[1] = dst[2] = gray;
            break;
        case PixelFormat::RGBA:
            dst[0] = dst[1] = dst[2] = gray;
            dst[3] = 255; // Opaque
            break;
        default:
            break;
    }
}

} // namespace linuxface::image
