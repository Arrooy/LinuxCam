#include <gtest/gtest.h>
#include <LinuxFace/Image/alpha_blender.h>
#include <LinuxFace/Image/pixel_format.h>
#include <iostream>

using namespace linuxface::image;

TEST(DebugPaste, DirectAlphaBlending)
{
    // Test direct alpha blending functions
    uint8_t src[4] = {255, 0, 0, 128};  // Red with 50% alpha
    uint8_t dst[4] = {1, 2, 3, 255};    // RGB(1,2,3) with full alpha
    
    std::cout << "Testing AlphaBlender::blendRGBA() directly:" << std::endl;
    std::cout << "Before: src=(" << (int)src[0] << "," << (int)src[1] << "," << (int)src[2] << "," << (int)src[3] << "), dst=(" << (int)dst[0] << "," << (int)dst[1] << "," << (int)dst[2] << "," << (int)dst[3] << ")" << std::endl;
    
    AlphaBlender::blendRGBA(src, dst);
    
    std::cout << "After: dst=(" << (int)dst[0] << "," << (int)dst[1] << "," << (int)dst[2] << "," << (int)dst[3] << ")" << std::endl;
    
    // Reset for next test
    dst[0] = 1; dst[1] = 2; dst[2] = 3; dst[3] = 255;
    
    std::cout << "\nTesting AlphaBlender::blendPixel() with RGBA formats:" << std::endl;
    std::cout << "Before: dst=(" << (int)dst[0] << "," << (int)dst[1] << "," << (int)dst[2] << "," << (int)dst[3] << ")" << std::endl;
    
    AlphaBlender::blendPixel(src, dst, PixelFormat::RGBA, PixelFormat::RGBA, 128);
    
    std::cout << "After: dst=(" << (int)dst[0] << "," << (int)dst[1] << "," << (int)dst[2] << "," << (int)dst[3] << ")" << std::endl;
}
