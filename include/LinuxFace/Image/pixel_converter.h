#pragma once

#include "pixel_format.h"
#include <cstdint>
#include <cstddef>

namespace linuxface::image
{

/**
 * Pure conversion operations - no blending, just format transformation
 * Single responsibility: pixel format conversion only
 */
class PixelConverter
{
public:
    /**
     * Convert RGB to grayscale using standard luminosity formula
     * ITU-R BT.709 luma coefficients for perceptual brightness
     */
    static constexpr uint8_t rgbToGrayscale(uint8_t r, uint8_t g, uint8_t b) noexcept
    {
        return static_cast<uint8_t>(0.299f * r + 0.587f * g + 0.114f * b);
    }
    
    /**
     * Convert single pixel between formats
     */
    static void convertPixel(const uint8_t* src, uint8_t* dst, 
                           PixelFormat srcFormat, PixelFormat dstFormat) noexcept;
    
    /**
     * Convert a row of pixels for better performance
     */
    static void convertRow(const uint8_t* srcRow, uint8_t* dstRow, size_t pixelCount,
                         PixelFormat srcFormat, PixelFormat dstFormat) noexcept;

private:
    static void copyPixel(const uint8_t* src, uint8_t* dst, PixelFormat format) noexcept;
    static void copyRow(const uint8_t* src, uint8_t* dst, size_t pixelCount, PixelFormat format) noexcept;
    static void convertFromRGB(const uint8_t* src, uint8_t* dst, PixelFormat dstFormat) noexcept;
    static void convertFromRGBA(const uint8_t* src, uint8_t* dst, PixelFormat dstFormat) noexcept;
    static void convertFromGrayscale(const uint8_t* src, uint8_t* dst, PixelFormat dstFormat) noexcept;
};

} // namespace linuxface::image
