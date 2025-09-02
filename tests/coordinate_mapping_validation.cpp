/**
 * Comprehensive coordinate mapping validation for paste operations
 * This test suite validates that coordinate mapping works correctly in all scenarios
 * Tests the internal copyPixelsWithBlending through public pasteAt interface
 */

#include <gtest/gtest.h>
#include <LinuxFace/Image/image.h>

using namespace linuxface;

class CoordinateMappingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a distinctive pattern in source image for easy verification
        sourceImage = Image(3 * 2 * 3); // 3x2 RGB
        sourceImage.info.width = 3;
        sourceImage.info.height = 2;
        sourceImage.info.format = ImageFormat::RGB;
        sourceImage.info.pixelSizeBytes = 3;
        
        // Fill source with distinctive pattern: (x+y*10, x*10+y, x*y+50)
        for (int y = 0; y < 2; y++) {
            for (int x = 0; x < 3; x++) {
                sourceImage.pxy(x, y, x+y*10, x*10+y, x*y+50, 255);
            }
        }
        
        // Create destination image - 6x5 to have room for various paste scenarios
        destImage = Image(6 * 5 * 3); // 6x5 RGB
        destImage.info.width = 6;
        destImage.info.height = 5;
        destImage.info.format = ImageFormat::RGB;
        destImage.info.pixelSizeBytes = 3;
        destImage.black(); // Fill with zeros for easy verification
    }
    
    Image sourceImage;
    Image destImage;
    
    // Helper to check if pixel in dest matches expected source pixel
    void verifyPixelMapping(int destX, int destY, int expectedSrcX, int expectedSrcY, const std::string& testCase = "") {
        Pixel destPixel = destImage(destX, destY);
        int expectedR = expectedSrcX + expectedSrcY * 10;
        int expectedG = expectedSrcX * 10 + expectedSrcY;
        int expectedB = expectedSrcX * expectedSrcY + 50;
        
        EXPECT_EQ(destPixel.r, expectedR) 
            << testCase << " - Dest(" << destX << "," << destY << ") should map to Src(" 
            << expectedSrcX << "," << expectedSrcY << ") - R channel mismatch. Got " << (int)destPixel.r;
        EXPECT_EQ(destPixel.g, expectedG) 
            << testCase << " - Dest(" << destX << "," << destY << ") should map to Src(" 
            << expectedSrcX << "," << expectedSrcY << ") - G channel mismatch. Got " << (int)destPixel.g;
        EXPECT_EQ(destPixel.b, expectedB) 
            << testCase << " - Dest(" << destX << "," << destY << ") should map to Src(" 
            << expectedSrcX << "," << expectedSrcY << ") - B channel mismatch. Got " << (int)destPixel.b;
    }
    
    void verifyPixelIsBlack(int destX, int destY, const std::string& testCase = "") {
        Pixel destPixel = destImage(destX, destY);
        EXPECT_EQ(destPixel.r, 0) << testCase << " - Dest(" << destX << "," << destY << ") should be black, got R=" << (int)destPixel.r;
        EXPECT_EQ(destPixel.g, 0) << testCase << " - Dest(" << destX << "," << destY << ") should be black, got G=" << (int)destPixel.g;
        EXPECT_EQ(destPixel.b, 0) << testCase << " - Dest(" << destX << "," << destY << ") should be black, got B=" << (int)destPixel.b;
    }
    
    void printSourcePattern() {
        std::cout << "\nSource Pattern (3x2):\n";
        for (int y = 0; y < 2; y++) {
            for (int x = 0; x < 3; x++) {
                Pixel p = sourceImage(x, y);
                std::cout << "(" << (int)p.r << "," << (int)p.g << "," << (int)p.b << ") ";
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }
    
    void printDestPattern() {
        std::cout << "\nDestination Pattern (6x5):\n";
        for (int y = 0; y < 5; y++) {
            for (int x = 0; x < 6; x++) {
                Pixel p = destImage(x, y);
                if (p.r == 0 && p.g == 0 && p.b == 0) {
                    std::cout << "(0,0,0) ";
                } else {
                    std::cout << "(" << (int)p.r << "," << (int)p.g << "," << (int)p.b << ") ";
                }
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }
};

TEST_F(CoordinateMappingTest, BasicPasteAtOrigin) {
    // Test Case 1: Simple paste at origin (0,0)
    // Source: 3x2, Dest: 6x5, paste at (0,0)
    // Expected: Src(0,0) -> Dest(0,0), Src(1,0) -> Dest(1,0), etc.
    
    printSourcePattern();
    destImage.pasteAt(sourceImage, 0, 0, false);
    printDestPattern();
    
    // Verify mappings
    verifyPixelMapping(0, 0, 0, 0, "BasicPasteAtOrigin"); // Dest(0,0) = Src(0,0)
    verifyPixelMapping(1, 0, 1, 0, "BasicPasteAtOrigin"); // Dest(1,0) = Src(1,0)
    verifyPixelMapping(2, 0, 2, 0, "BasicPasteAtOrigin"); // Dest(2,0) = Src(2,0)
    verifyPixelMapping(0, 1, 0, 1, "BasicPasteAtOrigin"); // Dest(0,1) = Src(0,1)
    verifyPixelMapping(1, 1, 1, 1, "BasicPasteAtOrigin"); // Dest(1,1) = Src(1,1)
    verifyPixelMapping(2, 1, 2, 1, "BasicPasteAtOrigin"); // Dest(2,1) = Src(2,1)
    
    // Verify untouched areas remain black
    verifyPixelIsBlack(3, 0, "BasicPasteAtOrigin");
    verifyPixelIsBlack(5, 0, "BasicPasteAtOrigin");
    verifyPixelIsBlack(0, 2, "BasicPasteAtOrigin");
    verifyPixelIsBlack(0, 4, "BasicPasteAtOrigin");
}

TEST_F(CoordinateMappingTest, PasteAtOffset) {
    // Test Case 2: Paste at offset (1,1)
    // Source: 3x2, Dest: 6x5, paste at (1,1)
    // Expected: Src(0,0) -> Dest(1,1), Src(1,0) -> Dest(2,1), etc.
    
    destImage.pasteAt(sourceImage, 1, 1, false);
    
    // Verify mappings
    verifyPixelMapping(1, 1, 0, 0, "PasteAtOffset"); // Dest(1,1) = Src(0,0)
    verifyPixelMapping(2, 1, 1, 0, "PasteAtOffset"); // Dest(2,1) = Src(1,0)
    verifyPixelMapping(3, 1, 2, 0, "PasteAtOffset"); // Dest(3,1) = Src(2,0)
    verifyPixelMapping(1, 2, 0, 1, "PasteAtOffset"); // Dest(1,2) = Src(0,1)
    verifyPixelMapping(2, 2, 1, 1, "PasteAtOffset"); // Dest(2,2) = Src(1,1)
    verifyPixelMapping(3, 2, 2, 1, "PasteAtOffset"); // Dest(3,2) = Src(2,1)
    
    // Verify untouched areas remain black
    verifyPixelIsBlack(0, 0, "PasteAtOffset");
    verifyPixelIsBlack(0, 1, "PasteAtOffset");
    verifyPixelIsBlack(4, 1, "PasteAtOffset");
    verifyPixelIsBlack(0, 3, "PasteAtOffset");
}

TEST_F(CoordinateMappingTest, PastePartialOverlapRight) {
    // Test Case 3: Paste with partial overlap on right edge
    // Source: 3x2, paste at (4,1) in 6x5 dest - rightmost column should be clipped
    
    destImage.pasteAt(sourceImage, 4, 1, false);
    
    // Only the visible part should be copied (x=4,5 are valid, x=6 would be out of bounds)
    verifyPixelMapping(4, 1, 0, 0, "PastePartialOverlapRight"); // Dest(4,1) = Src(0,0)
    verifyPixelMapping(5, 1, 1, 0, "PastePartialOverlapRight"); // Dest(5,1) = Src(1,0)
    verifyPixelMapping(4, 2, 0, 1, "PastePartialOverlapRight"); // Dest(4,2) = Src(0,1)
    verifyPixelMapping(5, 2, 1, 1, "PastePartialOverlapRight"); // Dest(5,2) = Src(1,1)
    
    // Src(2,x) should be clipped (would go to x=6 which is out of bounds)
    // Everything else should be black
    verifyPixelIsBlack(0, 0, "PastePartialOverlapRight");
    verifyPixelIsBlack(3, 1, "PastePartialOverlapRight");
}

TEST_F(CoordinateMappingTest, PastePartialOverlapBottom) {
    // Test Case 4: Paste with partial overlap on bottom edge
    // Source: 3x2, paste at (1,4) in 6x5 dest - bottom row should be clipped
    
    destImage.pasteAt(sourceImage, 1, 4, false);
    
    // Only the visible part should be copied (y=4 is valid, y=5 would be out of bounds)
    verifyPixelMapping(1, 4, 0, 0, "PastePartialOverlapBottom"); // Dest(1,4) = Src(0,0)
    verifyPixelMapping(2, 4, 1, 0, "PastePartialOverlapBottom"); // Dest(2,4) = Src(1,0)
    verifyPixelMapping(3, 4, 2, 0, "PastePartialOverlapBottom"); // Dest(3,4) = Src(2,0)
    
    // Src(x,1) should be clipped (would go to y=5 which is out of bounds)
    // Everything else should be black
    verifyPixelIsBlack(0, 0, "PastePartialOverlapBottom");
    verifyPixelIsBlack(1, 3, "PastePartialOverlapBottom");
}

TEST_F(CoordinateMappingTest, PasteCompletelyOutOfBounds) {
    // Test Case 5: Paste completely outside destination bounds
    
    destImage.pasteAt(sourceImage, 10, 10, false);
    
    // Everything should remain black - no copying should occur
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 6; x++) {
            verifyPixelIsBlack(x, y, "PasteCompletelyOutOfBounds");
        }
    }
}

TEST_F(CoordinateMappingTest, PasteAtNegativePosition) {
    // Test Case 6: Paste at negative position - test the fallback path
    // This should trigger the "fallback to blending" path in pasteImpl
    
    destImage.pasteAt(sourceImage, -1, -1, false);
    
    // Based on the fallback path logic, this should copy visible part
    // Src(1,1) should appear at Dest(0,0) because source is positioned at (-1,-1)
    verifyPixelMapping(0, 0, 1, 1, "PasteAtNegativePosition");
    verifyPixelMapping(1, 0, 2, 1, "PasteAtNegativePosition");
    
    // Rest should remain black  
    verifyPixelIsBlack(2, 0, "PasteAtNegativePosition");
    verifyPixelIsBlack(0, 1, "PasteAtNegativePosition");
    verifyPixelIsBlack(1, 1, "PasteAtNegativePosition");
}

TEST_F(CoordinateMappingTest, DebugCurrentMapping) {
    // Test Case 7: Debug what actually happens with a simple case
    // This is to understand current behavior vs expected behavior
    
    std::cout << "\n=== DEBUGGING CURRENT COORDINATE MAPPING ===\n";
    printSourcePattern();
    
    // Simple case: paste 3x2 source at (1,1) in 6x5 destination
    destImage.pasteAt(sourceImage, 1, 1, false);
    printDestPattern();
    
    // Check every destination pixel and report what we find
    std::cout << "Analysis of destination pixels:\n";
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 6; x++) {
            Pixel p = destImage(x, y);
            if (p.r != 0 || p.g != 0 || p.b != 0) {
                // Find which source pixel this matches
                bool found = false;
                for (int sy = 0; sy < 2 && !found; sy++) {
                    for (int sx = 0; sx < 3 && !found; sx++) {
                        Pixel sp = sourceImage(sx, sy);
                        if (sp.r == p.r && sp.g == p.g && sp.b == p.b) {
                            std::cout << "Dest(" << x << "," << y << ") = Src(" << sx << "," << sy << ")\n";
                            found = true;
                        }
                    }
                }
                if (!found) {
                    std::cout << "Dest(" << x << "," << y << ") = UNKNOWN(" << (int)p.r << "," << (int)p.g << "," << (int)p.b << ")\n";
                }
            }
        }
    }
    std::cout << "=== END DEBUG ===\n\n";
}
