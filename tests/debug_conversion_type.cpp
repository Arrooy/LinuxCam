#include <gtest/gtest.h>
#include <LinuxFace/Image/image.h>
#include <LinuxFace/Image/pixel_conversion.h>
#include <iostream>

using namespace linuxface;
using namespace linuxface::pixel_conversion;

// Helper function to convert pixel size to format
PixelFormat pixelSizeToFormat(unsigned char pixelSize) noexcept
{
    switch (pixelSize)
    {
        case 1: return PixelFormat::GRAYSCALE;
        case 3: return PixelFormat::RGB;
        case 4: return PixelFormat::RGBA;
        default: return PixelFormat::RGB; // Default fallback
    }
}

TEST(DebugPaste, PixelFormatCheck)
{
    // Test the new pixel format logic directly
    unsigned char srcPixelSize = 4;  // RGBA
    unsigned char dstPixelSize = 4;  // RGBA
    
    PixelFormat srcFormat = pixelSizeToFormat(srcPixelSize);
    PixelFormat dstFormat = pixelSizeToFormat(dstPixelSize);
    
    std::cout << "Source format for 4-byte pixel: " << static_cast<int>(srcFormat) << std::endl;
    std::cout << "Dest format for 4-byte pixel: " << static_cast<int>(dstFormat) << std::endl;
    std::cout << "RGB enum value: " << static_cast<int>(PixelFormat::RGB) << std::endl;
    std::cout << "RGBA enum value: " << static_cast<int>(PixelFormat::RGBA) << std::endl;
    
    // Test condition
    bool directCopyCondition = (srcFormat == dstFormat);
    std::cout << "Direct copy condition (srcFormat == dstFormat): " << directCopyCondition << std::endl;
    
    // Test alpha condition
    unsigned char srcAlpha = 128;
    bool needsBlending = (srcFormat == PixelFormat::RGBA && srcAlpha != 255);
    std::cout << "Needs blending (RGBA with alpha != 255): " << needsBlending << std::endl;
    
    // Test processPixel directly using ImageProcessor
    uint8_t src[4] = {255, 0, 0, 128};  // Red with 50% alpha
    uint8_t dst[4] = {0, 255, 0, 255};  // Green with full alpha
    
    std::cout << "\nDirect processPixel test:" << std::endl;
    std::cout << "Before: dst=(" << (int)dst[0] << "," << (int)dst[1] << "," << (int)dst[2] << "," << (int)dst[3] << ")" << std::endl;
    
    // Use the new architecture to process the pixel
    processPixel(src, dst, srcFormat, dstFormat, needsBlending, srcAlpha);
    
    std::cout << "After: dst=(" << (int)dst[0] << "," << (int)dst[1] << "," << (int)dst[2] << "," << (int)dst[3] << ")" << std::endl;
}
