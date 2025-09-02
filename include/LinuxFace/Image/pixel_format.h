#pragma once

#include <cstdint>

namespace linuxface::image
{

/**
 * Standardized pixel format definitions with clear byte ordering
 */
enum class PixelFormat : uint8_t
{
    RGB = 3,      // 24-bit RGB (Red, Green, Blue)
    RGBA = 4,     // 32-bit RGBA (Red, Green, Blue, Alpha)
    GRAYSCALE = 1 // 8-bit Grayscale
};

/**
 * Pixel format utilities - single responsibility for format operations
 */
class PixelFormatInfo
{
public:
    static constexpr uint8_t getBytesPerPixel(PixelFormat format) noexcept
    {
        return static_cast<uint8_t>(format);
    }
    
    static constexpr bool hasAlpha(PixelFormat format) noexcept
    {
        return format == PixelFormat::RGBA;
    }
    
    static constexpr bool isColor(PixelFormat format) noexcept
    {
        return format == PixelFormat::RGB || format == PixelFormat::RGBA;
    }
    
    static constexpr bool isCompatible(PixelFormat src, PixelFormat dst) noexcept
    {
        // Compatible if same format or both are color formats
        return src == dst || (isColor(src) && isColor(dst));
    }
};

} // namespace linuxface::image
