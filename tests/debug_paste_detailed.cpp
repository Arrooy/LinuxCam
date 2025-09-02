#include <gtest/gtest.h>
#include <LinuxFace/Image/image.h>
#include <iostream>

using namespace linuxface;

TEST(DebugPaste, DetailedRGBAtoRGBA)
{
    // Recreate the exact test scenario
    Image img(Pixel(1, 2, 3, 255), 2, 2);  // Will be RGB initially
    std::cout << "Initial img format: " << (int)img.info.pixelSizeBytes << " bytes per pixel" << std::endl;
    
    img.convertToRGBAInplace();  // Convert to RGBA
    std::cout << "After conversion img format: " << (int)img.info.pixelSizeBytes << " bytes per pixel" << std::endl;
    std::cout << "Initial pixel data: (" << (int)img.data()[0] << ", " << (int)img.data()[1] << ", " << (int)img.data()[2] << ", " << (int)img.data()[3] << ")" << std::endl;
    
    Image other(Pixel(255, 0, 0, 128), 2, 2);  // Will be RGBA automatically  
    std::cout << "Other img format: " << (int)other.info.pixelSizeBytes << " bytes per pixel" << std::endl;
    std::cout << "Other pixel data: (" << (int)other.data()[0] << ", " << (int)other.data()[1] << ", " << (int)other.data()[2] << ", " << (int)other.data()[3] << ")" << std::endl;
    
    img.paste(other, true);  // expandCanvas = true
    
    std::cout << "After paste pixel data: (" << (int)img.data()[0] << ", " << (int)img.data()[1] << ", " << (int)img.data()[2] << ", " << (int)img.data()[3] << ")" << std::endl;
    std::cout << "Red component: " << (int)img.data()[0] << std::endl;
    std::cout << "Expected: ~128" << std::endl;
    
    EXPECT_NEAR(img.data()[0], 128, 1);  // Allow for rounding differences
}
