#pragma once

#include <cstring>

#include "LinuxFace/Image/image_utils.h"

// TODO: add Gtest.

namespace linuxface
{
namespace pixel_conversion
{

// Enum for conversion types to enable optimized switching
enum class ConversionType
{
    DIRECT_COPY,  // Same format, no conversion needed
    RGBA_TO_RGBA, // RGBA to RGBA with potential blending
    RGBA_TO_RGB,  // RGBA to RGB, ignore alpha
    RGB_TO_RGBA,  // RGB to RGBA, set alpha=255
    RGB_TO_RGB,   // RGB to RGB, direct copy
    GRAY_TO_RGB,  // Grayscale to RGB, replicate to all channels
    GRAY_TO_RGBA, // Grayscale to RGBA, replicate + alpha=255
    RGB_TO_GRAY,  // RGB to Grayscale using luminosity
    RGBA_TO_GRAY, // RGBA to Grayscale using luminosity
    GRAY_TO_GRAY, // Grayscale to Grayscale, direct copy
    FALLBACK      // Fallback for unusual cases
};

// Determine conversion type from source and destination pixel sizes
inline ConversionType getConversionType(unsigned char srcPixelSize, unsigned char dstPixelSize)
{
    if (srcPixelSize == dstPixelSize)
    {
        return ConversionType::DIRECT_COPY;
    }

    switch (srcPixelSize)
    {
        case 4: // RGBA source
            switch (dstPixelSize)
            {
                case 3:
                    return ConversionType::RGBA_TO_RGB;
                case 1:
                    return ConversionType::RGBA_TO_GRAY;
            }
            break;
        case 3: // RGB source
            switch (dstPixelSize)
            {
                case 4:
                    return ConversionType::RGB_TO_RGBA;
                case 1:
                    return ConversionType::RGB_TO_GRAY;
            }
            break;
        case 1: // Grayscale source
            switch (dstPixelSize)
            {
                case 3:
                    return ConversionType::GRAY_TO_RGB;
                case 4:
                    return ConversionType::GRAY_TO_RGBA;
            }
            break;
    }

    return ConversionType::FALLBACK;
}


// Helper functions for common conversion operations
// Convert RGB to grayscale using luminosity formula
inline unsigned char rgbToGrayscale(unsigned char r, unsigned char g, unsigned char b)
{
    return static_cast<unsigned char>(0.299f * r + 0.587f * g + 0.114f * b);
}

// Convert RGBA to RGB (drop alpha)
inline void rgbaToRgb(const unsigned char* rgba, unsigned char* rgb)
{
    rgb[0] = rgba[0];
    rgb[1] = rgba[1];
    rgb[2] = rgba[2];
}

// Convert RGB to RGBA (add alpha=255)
inline void rgbToRgba(const unsigned char* rgb, unsigned char* rgba)
{
    rgba[0] = rgb[0];
    rgba[1] = rgb[1];
    rgba[2] = rgb[2];
    rgba[3] = 255;
}

// Convert grayscale to RGB (replicate across channels)
inline void grayscaleToRgb(unsigned char gray, unsigned char* rgb)
{
    rgb[0] = rgb[1] = rgb[2] = gray;
}

// Convert grayscale to RGBA (replicate across channels, alpha=255)
inline void grayscaleToRgba(unsigned char gray, unsigned char* rgba)
{
    rgba[0] = rgba[1] = rgba[2] = gray;
    rgba[3] = 255;
}

// Unified pixel conversion function - handles single pixel conversion
inline void convertPixel(const unsigned char* src, unsigned char* dst, ConversionType convType,
                         unsigned char srcAlpha = 255, bool blend = false)
{
    switch (convType)
    {
        case ConversionType::DIRECT_COPY:
            if (blend && srcAlpha != 255 && srcAlpha != 0)
            {
                // RGBA -> RGBA blending
                for (int i = 0; i < 3; ++i)
                {
                    dst[i] = static_cast<unsigned char>((srcAlpha * src[i] + (255 - srcAlpha) * dst[i]) / 255);
                }
                dst[3] = srcAlpha;
            }
            else if (srcAlpha != 0)
            {
                // Direct copy for opaque pixels or non-RGBA formats
                std::memcpy(dst, src, 4); // Assume 4 bytes for direct copy case
            }
            break;

        case ConversionType::RGBA_TO_RGB:
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            break;

        case ConversionType::RGB_TO_RGBA:
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = 255;
            break;

        case ConversionType::RGB_TO_RGB:
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            break;

        case ConversionType::GRAY_TO_RGB:
            dst[0] = dst[1] = dst[2] = src[0];
            break;

        case ConversionType::GRAY_TO_RGBA:
            dst[0] = dst[1] = dst[2] = src[0];
            dst[3] = 255;
            break;

        case ConversionType::RGB_TO_GRAY:
        case ConversionType::RGBA_TO_GRAY:
        {
            unsigned char gray = rgbToGrayscale(src[0], src[1], src[2]);
            dst[0] = gray;
            break;
        }

        case ConversionType::GRAY_TO_GRAY:
            dst[0] = src[0];
            break;

        case ConversionType::FALLBACK:
        default:
            // Copy as many channels as possible - determine sizes dynamically
            unsigned char maxChannels = 4; // Maximum we support
            for (unsigned char i = 0; i < maxChannels; ++i)
            {
                // Simple fallback copy
                dst[i] = (i < 4) ? src[i] : 0;
            }
            break;
    }
}

// Optimized block copy for same-format operations
inline void copyPixelBlock(const unsigned char* srcData, unsigned char* dstData, size_t srcRowIdx, size_t dstRowIdx,
                           size_t copyWidth, unsigned char pixelSize)
{
    std::memcpy(dstData + dstRowIdx, srcData + srcRowIdx, copyWidth * pixelSize);
}

// Batch convert a row of pixels using optimized loops
inline void convertPixelRow(const unsigned char* srcRow, unsigned char* dstRow, size_t width, ConversionType convType,
                            bool enableBlending = false)
{
    switch (convType)
    {
        case ConversionType::RGBA_TO_RGB:
            for (size_t i = 0; i < width; ++i)
            {
                dstRow[i * 3 + 0] = srcRow[i * 4 + 0];
                dstRow[i * 3 + 1] = srcRow[i * 4 + 1];
                dstRow[i * 3 + 2] = srcRow[i * 4 + 2];
            }
            break;

        case ConversionType::RGB_TO_RGBA:
            for (size_t i = 0; i < width; ++i)
            {
                dstRow[i * 4 + 0] = srcRow[i * 3 + 0];
                dstRow[i * 4 + 1] = srcRow[i * 3 + 1];
                dstRow[i * 4 + 2] = srcRow[i * 3 + 2];
                dstRow[i * 4 + 3] = 255;
            }
            break;

        case ConversionType::GRAY_TO_RGB:
            for (size_t i = 0; i < width; ++i)
            {
                unsigned char gray = srcRow[i];
                dstRow[i * 3 + 0] = gray;
                dstRow[i * 3 + 1] = gray;
                dstRow[i * 3 + 2] = gray;
            }
            break;

        case ConversionType::GRAY_TO_RGBA:
            for (size_t i = 0; i < width; ++i)
            {
                unsigned char gray = srcRow[i];
                dstRow[i * 4 + 0] = gray;
                dstRow[i * 4 + 1] = gray;
                dstRow[i * 4 + 2] = gray;
                dstRow[i * 4 + 3] = 255;
            }
            break;

        case ConversionType::RGB_TO_GRAY:
            for (size_t i = 0; i < width; ++i)
            {
                unsigned char r = srcRow[i * 3 + 0];
                unsigned char g = srcRow[i * 3 + 1];
                unsigned char b = srcRow[i * 3 + 2];
                dstRow[i] = rgbToGrayscale(r, g, b);
            }
            break;

        case ConversionType::RGBA_TO_GRAY:
            for (size_t i = 0; i < width; ++i)
            {
                unsigned char r = srcRow[i * 4 + 0];
                unsigned char g = srcRow[i * 4 + 1];
                unsigned char b = srcRow[i * 4 + 2];
                dstRow[i] = rgbToGrayscale(r, g, b);
            }
            break;

        case ConversionType::RGB_TO_RGB:
            // Simple memcpy for RGB->RGB
            std::memcpy(dstRow, srcRow, width * 3);
            break;

        case ConversionType::GRAY_TO_GRAY:
            // Simple memcpy for Gray->Gray
            std::memcpy(dstRow, srcRow, width);
            break;

        default:
            // Fallback to pixel-by-pixel conversion
            for (size_t i = 0; i < width; ++i)
            {
                size_t srcOffset =
                    i
                    * (convType == ConversionType::GRAY_TO_RGB || convType == ConversionType::GRAY_TO_RGBA
                           ? 1
                           : (convType == ConversionType::RGB_TO_RGBA || convType == ConversionType::RGB_TO_GRAY ? 3
                                                                                                                 : 4));
                size_t dstOffset =
                    i
                    * (convType == ConversionType::RGBA_TO_GRAY || convType == ConversionType::RGB_TO_GRAY
                           ? 1
                           : (convType == ConversionType::RGBA_TO_RGB || convType == ConversionType::GRAY_TO_RGB ? 3
                                                                                                                 : 4));
                convertPixel(srcRow + srcOffset, dstRow + dstOffset, convType);
            }
            break;
    }
}
} // namespace pixel_conversion
} // namespace linuxface
