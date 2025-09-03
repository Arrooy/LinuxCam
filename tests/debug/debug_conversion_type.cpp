#include <gtest/gtest.h>
#include <LinuxFace/Image/image.h>
#include <LinuxFace/Image/image_processor.h>
#include <iostream>

using namespace linuxface;

TEST(DebugPaste, PixelFormatCheck)
{
    // Test the new pixel format logic directly
    unsigned char srcPixelSize = 4;  // RGBA
    unsigned char dstPixelSize = 4;  // RGBA
    
    image::PixelFormat srcFormat = Image::pixelSizeToFormat(srcPixelSize);
    image::PixelFormat dstFormat = Image::pixelSizeToFormat(dstPixelSize);
    
    std::cout << "Source format for 4-byte pixel: " << static_cast<int>(srcFormat) << std::endl;
    std::cout << "Dest format for 4-byte pixel: " << static_cast<int>(dstFormat) << std::endl;
    std::cout << "RGB enum value: " << static_cast<int>(image::PixelFormat::RGB) << std::endl;
    std::cout << "RGBA enum value: " << static_cast<int>(image::PixelFormat::RGBA) << std::endl;
    
    // Test condition
    bool directCopyCondition = (srcFormat == dstFormat);
    std::cout << "Direct copy condition (srcFormat == dstFormat): " << directCopyCondition << std::endl;
    
    // Test alpha condition
    unsigned char srcAlpha = 128;
    bool needsBlending = (srcFormat == image::PixelFormat::RGBA && srcAlpha != 255);
    std::cout << "Needs blending (RGBA with alpha != 255): " << needsBlending << std::endl;
    
    // Test processPixel directly using ImageProcessor
    uint8_t src[4] = {255, 0, 0, 128};  // Red with 50% alpha
    uint8_t dst[4] = {0, 255, 0, 255};  // Green with full alpha
    
    std::cout << "\nDirect processPixel test:" << std::endl;
    std::cout << "Before: dst=(" << (int)dst[0] << "," << (int)dst[1] << "," << (int)dst[2] << "," << (int)dst[3] << ")" << std::endl;
    
    // Use ImageProcessor to process the pixel directly  
    image::ImageProcessor::processPixel(src, dst, srcFormat, dstFormat, needsBlending, srcAlpha);
    
    std::cout << "After: dst=(" << (int)dst[0] << "," << (int)dst[1] << "," << (int)dst[2] << "," << (int)dst[3] << ")" << std::endl;
}
