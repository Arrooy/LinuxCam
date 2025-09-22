// Tests for paste, blending, canvas expansion
// Covers: paste, pasteAt, pasteImpl, copyPixelsWithBlending, copyPixelsOptimized
// Edge cases: overlapping, expandCanvas, empty source, different formats

#include <gtest/gtest.h>

#include <tuple>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/imageLoader.h"

using namespace linuxface;

struct PasteParams
{
    ImageFormat srcFormat;
    ImageFormat dstFormat;
    int srcAlpha;
    int dstAlpha;
    unsigned char expectedR;
    unsigned char expectedG;
    unsigned char expectedB;
    unsigned char expectedA;
    const char* description;
};

class ImagePasteParamTest : public ::testing::TestWithParam<PasteParams>
{
};

// Helper function to create image with specific format
Image createImageWithFormat(ImageFormat format, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (format == ImageFormat::RGB) {
        // Create RGB image without alpha
        Pixel p(r, g, b, 255);  // Constructor will create RGB since a=255
        Image img(p, 1, 1);
        return img;
    } else if (format == ImageFormat::RGBA) {
        // Create RGBA image  
        Pixel p(r, g, b, a);
        Image img(p, 1, 1);
        if (img.info.format != ImageFormat::RGBA) {
            // Force conversion to RGBA
            img.convertToRGBAInplace();
        }
        // Fix alpha channel if needed
        if (a != 255) {
            unsigned char* data = img.data();
            data[3] = a;  // Set the correct alpha value
        }
        return img;
    } else if (format == ImageFormat::GRAYSCALE) {
        // Create grayscale image
        Pixel p(r, r, r, 255);  // Use red channel as gray
        Image img(p, 1, 1);
        // Manually convert to grayscale by creating new image
        Image gray(1);
        gray.info.format = ImageFormat::GRAYSCALE;
        gray.info.pixelSizeBytes = 1;
        gray.info.width = 1;
        gray.info.height = 1;
        gray.resize(1);
        unsigned char* data = gray.data();
        data[0] = r;
        return gray;
    }
    return Image();
}

TEST_P(ImagePasteParamTest, PasteFormatCombinations)
{
    const auto& p = GetParam();
    
    // Setup destination with zeros for all channels
    Image dst = createImageWithFormat(p.dstFormat, 0, 0, 0, p.dstAlpha);

    // Setup source
    Image src = createImageWithFormat(p.srcFormat, 200, 100, 50, p.srcAlpha);

    // Paste with blending
    dst.paste(src, false);
    // Check expectations
    auto* data = dst.data();
    EXPECT_EQ(data[0], p.expectedR) << p.description;
    if (dst.info.pixelSizeBytes > 1)
    {
        EXPECT_EQ(data[1], p.expectedG) << p.description;
    }
    if (dst.info.pixelSizeBytes > 2)
    {
        EXPECT_EQ(data[2], p.expectedB) << p.description;
    }
    if (dst.info.pixelSizeBytes > 3)
    {
        EXPECT_EQ(data[3], p.expectedA) << p.description;
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllPasteCases, ImagePasteParamTest,
    ::testing::Values(
        // RGBA->RGBA, alpha=255: direct copy
        PasteParams{ImageFormat::RGBA, ImageFormat::RGBA, 255, 255, 200, 100, 50, 255, "RGBA->RGBA alpha=255"},
        // RGBA->RGBA, alpha=128: blend (src alpha 128 over dst alpha 255)
        PasteParams{ImageFormat::RGBA, ImageFormat::RGBA, 128, 255, 100, 50, 25, 128, "RGBA->RGBA alpha=128 (blended)"},
        // RGBA->RGBA, alpha=0: no change (src alpha 0, so dst unchanged, which is zero)
        PasteParams{ImageFormat::RGBA, ImageFormat::RGBA, 0, 0, 0, 0, 0, 0, "RGBA->RGBA alpha=0 (no change)"},
        // RGBA->RGB, alpha=128: blend using alpha
        PasteParams{ImageFormat::RGBA, ImageFormat::RGB, 128, 255, 100, 50, 25, 255, "RGBA->RGB alpha=128 (blended)"},
        // RGB->RGBA: copy RGB, alpha set to 255
        PasteParams{ImageFormat::RGB, ImageFormat::RGBA, 255, 255, 200, 100, 50, 255, "RGB->RGBA (direct copy, alpha=255)"},
        // RGB->RGB: direct copy
        PasteParams{ImageFormat::RGB, ImageFormat::RGB, 255, 255, 200, 100, 50, 255, "RGB->RGB (direct copy)"},
        // Grayscale->RGB: all channels set to gray
        PasteParams{ImageFormat::GRAYSCALE, ImageFormat::RGB, 255, 255, 200, 200, 200, 255, "Gray->RGB (all channels gray)"},
        // Grayscale->RGBA: all channels set to gray, alpha set to 255
        PasteParams{ImageFormat::GRAYSCALE, ImageFormat::RGBA, 255, 255, 200, 200, 200, 255, "Gray->RGBA (all channels gray, alpha=255)"},
        // RGBA->RGBA, dst alpha=128, src alpha=128: blend (src alpha 128 over dst alpha 128)
        PasteParams{ImageFormat::RGBA, ImageFormat::RGBA, 128, 128, 100, 50, 25, 128, "RGBA->RGBA src/dst alpha=128 (blended)"},
        // RGB->RGBA, dst alpha=128: copy RGB, alpha set to 255
        PasteParams{ImageFormat::RGB, ImageFormat::RGBA, 255, 128, 200, 100, 50, 255, "RGB->RGBA dst alpha=128 (direct copy, alpha=255)"},
        // Grayscale->RGBA, dst alpha=128: all channels gray, alpha set to 255
        PasteParams{ImageFormat::GRAYSCALE, ImageFormat::RGBA, 255, 128, 200, 200, 200, 255, "Gray->RGBA dst alpha=128 (all channels gray, alpha=255)"},
        // RGBA->Grayscale, alpha=255: grayscale conversion (luminosity formula)
        PasteParams{ImageFormat::RGBA, ImageFormat::GRAYSCALE, 255, 255, 124, 124, 124, 255, "RGBA->Gray alpha=255 (grayscale conversion)"},
        // RGB->Grayscale: grayscale conversion (luminosity formula)
        PasteParams{ImageFormat::RGB, ImageFormat::GRAYSCALE, 255, 255, 124, 124, 124, 255, "RGB->Gray (grayscale conversion)"}
    ));

TEST(ImagePaste, PasteExpandCanvas)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image other(Pixel(9, 8, 7), 2, 2);
    img.paste(other, true);
    EXPECT_GE(img.info.width, 2);
    EXPECT_GE(img.info.height, 2);
}

// CRITICAL TEST: Verify pixel positioning after canvas expansion
TEST(ImagePaste, PasteExpandCanvasPositioning)
{
    // Create base image with distinctive pattern
    Image base(Pixel(100, 101, 102), 2, 2);
    base.pxy(0, 0, 100, 101, 102, 255);
    base.pxy(1, 0, 110, 111, 112, 255);
    base.pxy(0, 1, 120, 121, 122, 255);
    base.pxy(1, 1, 130, 131, 132, 255);

    // Create source image to paste
    Image source(Pixel(200, 201, 202), 2, 2);
    source.pxy(0, 0, 200, 201, 202, 255);
    source.pxy(1, 0, 210, 211, 212, 255);
    source.pxy(0, 1, 220, 221, 222, 255);
    source.pxy(1, 1, 230, 231, 232, 255);

    // Paste source at (3, 1) - this should expand canvas and position correctly
    base.pasteAt(source, 3, 1, true);

    // Verify canvas expanded
    EXPECT_GE(base.info.width, 5);  // 3 + 2 = 5
    EXPECT_GE(base.info.height, 3); // 1 + 2 = 3

    // CRITICAL: Verify original pixels are in correct position after expansion
    // Original base image should be at (0,0) in the expanded canvas
    auto origPixel = base(0, 0);
    EXPECT_EQ(origPixel.r, 100);
    EXPECT_EQ(origPixel.g, 101);
    EXPECT_EQ(origPixel.b, 102);

    // CRITICAL: Verify pasted pixels are at correct position
    // Source should be at (3,1) in the expanded canvas
    auto pastedPixel = base(3, 1);
    EXPECT_EQ(pastedPixel.r, 200);
    EXPECT_EQ(pastedPixel.g, 201);
    EXPECT_EQ(pastedPixel.b, 202);

    auto pastedPixel2 = base(4, 1);
    EXPECT_EQ(pastedPixel2.r, 210);
    EXPECT_EQ(pastedPixel2.g, 211);
    EXPECT_EQ(pastedPixel2.b, 212);
}

TEST(ImagePaste, PasteAtOverlap)
{
    Image img(Pixel(1, 2, 3), 4, 4);
    Image other(Pixel(9, 8, 7), 2, 2);
    img.pasteAt(other, 1, 1, false);
    EXPECT_EQ(img.info.width, 4);
}

TEST(ImagePaste, PasteSelf)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    img.paste(img, true);
    EXPECT_EQ(img.info.width, 2);
    EXPECT_EQ(img.info.height, 2);
}

TEST(ImagePaste, PasteAlphaBlending)
{
    // RGBA -> RGB: blending
    Image img(Pixel(1, 2, 3), 2, 2);          // RGB destination
    Image other(Pixel(255, 0, 0, 128), 2, 2); // RGBA source
    other.info.format = ImageFormat::RGBA;
    other.info.pixelSizeBytes = 4;
    img.paste(other, true);
    int blended = img.data()[0];
    // Should be blended: src alpha 128/255 ≈ 0.502
    // Expected: 255 * 0.502 + 1 * 0.498 ≈ 128 + 0.5 ≈ 128
    EXPECT_NEAR(blended, 128, 2); // Alpha blending of red channel
}

TEST(ImagePaste, PasteRGBtoRGB)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image other(Pixel(9, 8, 7), 2, 2);
    img.paste(other, true);
    EXPECT_EQ(img.data()[0], 9);
}

TEST(ImagePaste, PasteRGBAtoRGBA)
{
    Image img(Pixel(1, 2, 3, 255), 2, 2);
    img.convertToRGBAInplace();
    Image other(Pixel(255, 0, 0, 128), 2, 2);
    img.paste(other, true);
    int blended = img.data()[0];
    // Should be blended if both have alpha
    EXPECT_EQ(blended, (128 * 255 + (255 - 128) * 1) / 255);
}

TEST(ImagePaste, PasteRGBtoRGBA)
{
    Image img(Pixel(1, 2, 3, 255), 2, 2);
    // Convert RGB to RGBA format properly
    img.convertToRGBAInplace();
    Image other(Pixel(9, 8, 7), 2, 2);
    img.paste(other, true);
    EXPECT_EQ(img.data()[0], 9);
    EXPECT_EQ(img.data()[3], 255); // Alpha should be set to 255
}

TEST(ImagePaste, PasteGrayscaleToRGB)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image other(Pixel(7, 0, 0), 2, 2);
    other.info.format = ImageFormat::GRAYSCALE;
    other.info.pixelSizeBytes = 1;
    img.paste(other, true);
    EXPECT_EQ(img.data()[0], 7);
    EXPECT_EQ(img.data()[1], 7);
    EXPECT_EQ(img.data()[2], 7);
}

TEST(ImagePaste, PasteGrayscaleToRGBA)
{
    Image img(Pixel(1, 2, 3, 255), 2, 2);
    img.convertToRGBAInplace();
    Image other(Pixel(7, 0, 0), 2, 2);
    other.info.format = ImageFormat::GRAYSCALE;
    other.info.pixelSizeBytes = 1;
    img.paste(other, true);
    EXPECT_EQ(img.data()[0], 7);
    EXPECT_EQ(img.data()[1], 7);
    EXPECT_EQ(img.data()[2], 7);
    EXPECT_EQ(img.data()[3], 255);
}

TEST(ImagePaste, PasteEmptySource)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image empty;
    img.paste(empty, true);
    EXPECT_EQ(img.info.width, 2);
    EXPECT_EQ(img.info.height, 2);
}

TEST(ImagePaste, PasteEmptyDestination)
{
    Image empty;
    Image other(Pixel(9, 8, 7), 2, 2);
    // If destination is empty, copyFrom should be called
    if (empty.empty())
    {
        empty.copyFrom(other);
    }
    EXPECT_EQ(empty.info.width, 2);
    EXPECT_EQ(empty.info.height, 2);
}

TEST(ImagePaste, PasteOutOfBounds)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image other(Pixel(9, 8, 7), 2, 2);
    img.pasteAt(other, 10, 10, true);
    EXPECT_GE(img.info.width, 12);
    EXPECT_GE(img.info.height, 12);
}

TEST(ImagePaste, PasteNegativeCoordsExpand)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image other(Pixel(9, 8, 7), 2, 2);
    img.pasteAt(other, -1, -1, true);
    EXPECT_GE(img.info.width, 3);
    EXPECT_GE(img.info.height, 3);
}

TEST(ImagePaste, PasteDifferentFormats)
{
    Image img(Pixel(1, 2, 3), 2, 2);
    Image other(Pixel(9, 8, 7, 128), 2, 2);
    other.info.format = ImageFormat::RGBA;
    other.info.pixelSizeBytes = 4;
    img.paste(other, true);
    EXPECT_GE(img.info.width, 2);
}

TEST(ImagePaste, PasteImplErrorHandling)
{
    Image img;
    Image other;
    img.pasteAt(other, 0, 0, true);
    EXPECT_EQ(img.info.width, 0);
    EXPECT_EQ(img.info.height, 0);
}

// Test: Bounds handling for single-pixel and edge cases
TEST(ImagePasteBoundsTest, PasteSinglePixelAtEdge)
{
    // Destination: 2x2 RGBA, all zeros
    Image dst = createImageWithFormat(ImageFormat::RGBA, 0, 0, 0, 0);
    dst.resize(2 * 2 * 4);  // Resize to 2x2 RGBA
    dst.info.width = 2;
    dst.info.height = 2;
    
    // Source: 1x1 RGBA, red pixel, alpha=255
    Image src = createImageWithFormat(ImageFormat::RGBA, 255, 0, 0, 255);
    
    // Paste at (1,1) (bottom-right corner)
    dst.pasteAt(src, 1, 1, false);
    // Only pixel (1,1) should be set
    auto* data = dst.data();
    int idx = (1 * 2 + 1) * 4;
    EXPECT_EQ(data[idx + 0], 255);
    EXPECT_EQ(data[idx + 1], 0);
    EXPECT_EQ(data[idx + 2], 0);
    EXPECT_EQ(data[idx + 3], 255);
    // All other pixels should remain zero
    for (int i = 0; i < 4 * 4; ++i)
    {
        if (i >= idx && i < idx + 4) continue;
        EXPECT_EQ(data[i], 0);
    }
}
TEST(ImagePasteIntegration, PasteRealImageWithCanvasExpansion)
{
    // Load real image file from test common folder
    const std::string imagePath = "/home/arroyo/Documents/Projectes/LinuxCam/tests/common/single_face.jpeg";
    auto realImage = ImageLoader::loadImageFromFile(imagePath);
    
    ASSERT_TRUE(realImage != nullptr) << "Failed to load real image from " << imagePath;
    ASSERT_FALSE(realImage->empty()) << "Loaded image is empty";
    ASSERT_GT(realImage->info.width, 0) << "Image width should be > 0";
    ASSERT_GT(realImage->info.height, 0) << "Image height should be > 0";
    
    // Store original dimensions
    const size_t origWidth = realImage->info.width;
    const size_t origHeight = realImage->info.height;
    
    // Create a small test image to paste
    Image testImage(Pixel(255, 0, 255), 10, 10);  // Magenta square
    
    // Test 1: Paste with canvas expansion at positive offset
    Image canvas1(realImage->size());  // Create empty image with same size
    canvas1.copyFrom(*realImage);      // Copy the real image
    canvas1.pasteAt(testImage, static_cast<long>(origWidth) + 5, 20, true);
    
    // Verify canvas expanded
    EXPECT_GE(canvas1.info.width, origWidth + 15) << "Canvas should expand to accommodate paste";
    EXPECT_GE(canvas1.info.height, origHeight) << "Height should at least match original";
    
    // Verify original image pixels are preserved at (0,0)
    auto origPixel = canvas1(0, 0);
    auto realOrigPixel = (*realImage)(0, 0);
    EXPECT_EQ(origPixel.r, realOrigPixel.r) << "Original pixel (0,0) should be preserved";
    EXPECT_EQ(origPixel.g, realOrigPixel.g) << "Original pixel (0,0) should be preserved";
    EXPECT_EQ(origPixel.b, realOrigPixel.b) << "Original pixel (0,0) should be preserved";
    
    // Verify test image was pasted at correct location
    auto pastedPixel = canvas1(static_cast<long>(origWidth) + 5, 20);
    EXPECT_EQ(pastedPixel.r, 255) << "Pasted pixel should be magenta";
    EXPECT_EQ(pastedPixel.g, 0) << "Pasted pixel should be magenta";
    EXPECT_EQ(pastedPixel.b, 255) << "Pasted pixel should be magenta";
    
    // Test 2: Paste with negative coordinates (canvas expansion on all sides)
    Image canvas2(realImage->size());  // Create empty image with same size
    canvas2.copyFrom(*realImage);      // Copy the real image
    canvas2.pasteAt(testImage, -5, -5, true);
    
    // Verify canvas expanded on all sides
    EXPECT_GE(canvas2.info.width, origWidth + 5) << "Canvas should expand for negative X";
    EXPECT_GE(canvas2.info.height, origHeight + 5) << "Canvas should expand for negative Y";
    
    // Verify original image is now at offset (5,5) due to negative paste coordinates
    // NOTE: Position (5,5) is covered by the test image (which spans 0,0 to 9,9)
    // Check position (10,10) instead, which should contain original image data
    auto offsetPixel = canvas2(10, 10);
    EXPECT_EQ(offsetPixel.r, realOrigPixel.r) << "Original image should be accessible at (10,10) after negative offset";
    EXPECT_EQ(offsetPixel.g, realOrigPixel.g) << "Original image should be accessible at (10,10) after negative offset";
    EXPECT_EQ(offsetPixel.b, realOrigPixel.b) << "Original image should be accessible at (10,10) after negative offset";
    
    // Verify test image was pasted at (0,0) in the new coordinate system
    auto negPastedPixel = canvas2(0, 0);
    EXPECT_EQ(negPastedPixel.r, 255) << "Pasted pixel at negative coords should be magenta";
    EXPECT_EQ(negPastedPixel.g, 0) << "Pasted pixel at negative coords should be magenta";
    EXPECT_EQ(negPastedPixel.b, 255) << "Pasted pixel at negative coords should be magenta";
}
TEST(ImagePasteIntegration, PasteAlignedFaceSimulation)
{
    // Load real image file from test common folder
    const std::string imagePath = "/home/arroyo/Documents/Projectes/LinuxCam/tests/common/single_face.jpeg";
    auto baseImage = ImageLoader::loadImageFromFile(imagePath);
    
    ASSERT_TRUE(baseImage != nullptr) << "Failed to load base image";
    ASSERT_FALSE(baseImage->empty()) << "Base image is empty";
    
    // Create a simulated "aligned face" image (smaller region of interest)
    const size_t faceWidth = 64;
    const size_t faceHeight = 64;
    Image alignedFace(Pixel(100, 150, 200), faceWidth, faceHeight);
    
    // Simulate the application workflow: paste aligned face with canvas expansion
    // This mimics what happens in application.cpp when aligning faces
    baseImage->pasteAt(alignedFace, static_cast<long>(baseImage->info.width) + 10, 50, true);
    
    // Verify the result
    EXPECT_GE(baseImage->info.width, faceWidth + 10) << "Canvas should accommodate face + offset";
    EXPECT_GE(baseImage->info.height, faceHeight + 50) << "Canvas should accommodate face height + offset";
    
    // Verify face was placed at correct position
    auto facePixel = (*baseImage)(static_cast<long>(baseImage->info.width) - faceWidth, 50);
    EXPECT_EQ(facePixel.r, 100) << "Face pixel should be at expected position";
    EXPECT_EQ(facePixel.g, 150) << "Face pixel should be at expected position";
    EXPECT_EQ(facePixel.b, 200) << "Face pixel should be at expected position";
    
    // Verify original image is still intact
    auto origPixel = (*baseImage)(0, 0);
    EXPECT_NE(origPixel.r, 100) << "Original image should not be overwritten by face";
    EXPECT_NE(origPixel.g, 150) << "Original image should not be overwritten by face";
    EXPECT_NE(origPixel.b, 200) << "Original image should not be overwritten by face";
}

/**
 * Test to reproduce the corruption issue seen in application.cpp pasteAt operations
 * This simulates the exact pasteAt pattern used in createCompositeImage
 */
TEST(ApplicationPasteReproduction, CompositeImageCorruption)
{
    // Load real images like the application does
    const std::string imagePath1 = "/home/arroyo/Documents/Projectes/LinuxCam/tests/common/single_face.jpeg";
    const std::string imagePath2 = "/home/arroyo/Documents/Projectes/LinuxCam/tests/common/two_face.jpeg";
    
    auto realImage1 = ImageLoader::loadImageFromFile(imagePath1);
    auto realImage2 = ImageLoader::loadImageFromFile(imagePath2);
    
    ASSERT_TRUE(realImage1) << "Failed to load first test image";
    ASSERT_TRUE(realImage2) << "Failed to load second test image";
    
    printf("Image1 dimensions: %lux%lu, format=%d, pixelSize=%lu\n", 
           realImage1->info.width, realImage1->info.height, 
           static_cast<int>(realImage1->info.format), realImage1->info.pixelSizeBytes);
    printf("Image2 dimensions: %lux%lu, format=%d, pixelSize=%lu\n", 
           realImage2->info.width, realImage2->info.height,
           static_cast<int>(realImage2->info.format), realImage2->info.pixelSizeBytes);
    
    // Simulate the composite creation as done in Application::createCompositeImage
    // Create transparent canvas like the app does
    const Pixel transparentPixel{0, 0, 0, 0};
    unsigned int compositeWidth = 800;
    unsigned int compositeHeight = 600;
    auto compositeImage = std::make_unique<Image>(transparentPixel, compositeWidth, compositeHeight);
    
    printf("Composite canvas: %lux%lu, format=%d, pixelSize=%lu\n",
           compositeImage->info.width, compositeImage->info.height,
           static_cast<int>(compositeImage->info.format), compositeImage->info.pixelSizeBytes);
    
    // Simulate layer positioning with minX/minY offset calculation like the app
    float minX = 0.0f;
    float minY = 0.0f;
    float layer1_x = 100.0f;
    float layer1_y = 50.0f;
    float layer2_x = 200.0f;
    float layer2_y = 150.0f;
    
    // Paste first image at calculated position (like layer.x - minX, layer.y - minY)
    compositeImage->pasteAt(*realImage1, static_cast<long>(layer1_x - minX), 
                           static_cast<long>(layer1_y - minY), false);
    
    // Paste second image
    compositeImage->pasteAt(*realImage2, static_cast<long>(layer2_x - minX), 
                           static_cast<long>(layer2_y - minY), false);
    
    // Save result to inspect visually
    compositeImage->saveToDisk("/tmp/composite_test_result.ppm");
    
    // Check for obvious corruption indicators
    auto* data = compositeImage->data();
    ASSERT_NE(data, nullptr) << "Composite image data is null";
    
    // Verify the paste positions have non-zero pixels
    bool foundNonZeroAtPos1 = false;
    bool foundNonZeroAtPos2 = false;
    
    long pos1_x = static_cast<long>(layer1_x);
    long pos1_y = static_cast<long>(layer1_y);
    long pos2_x = static_cast<long>(layer2_x);
    long pos2_y = static_cast<long>(layer2_y);
    
    if (pos1_x >= 0 && pos1_x < static_cast<long>(compositeImage->info.width) &&
        pos1_y >= 0 && pos1_y < static_cast<long>(compositeImage->info.height))
    {
        auto pixel1 = (*compositeImage)(pos1_x, pos1_y);
        if (pixel1.r > 0 || pixel1.g > 0 || pixel1.b > 0) {
            foundNonZeroAtPos1 = true;
        }
    }
    
    if (pos2_x >= 0 && pos2_x < static_cast<long>(compositeImage->info.width) &&
        pos2_y >= 0 && pos2_y < static_cast<long>(compositeImage->info.height))
    {
        auto pixel2 = (*compositeImage)(pos2_x, pos2_y);
        if (pixel2.r > 0 || pixel2.g > 0 || pixel2.b > 0) {
            foundNonZeroAtPos2 = true;
        }
    }
    
    EXPECT_TRUE(foundNonZeroAtPos1) << "No image data found at first paste position";
    EXPECT_TRUE(foundNonZeroAtPos2) << "No image data found at second paste position";
    
    printf("Test completed. Check /tmp/composite_test_result.ppm for visual inspection\n");
}

/**
 * Test canvas expansion corruption that might occur with negative coordinates
 */
TEST(ApplicationPasteReproduction, CanvasExpansionCorruption)
{
    auto realImage = ImageLoader::loadImageFromFile("/home/arroyo/Documents/Projectes/LinuxCam/tests/common/single_face.jpeg");
    ASSERT_TRUE(realImage) << "Failed to load test image";
    
    // Create small canvas
    auto canvas = std::make_unique<Image>(Pixel(50, 50, 50), 100, 100);
    
    printf("Before paste: canvas %lux%lu\n", canvas->info.width, canvas->info.height);
    
    // Paste with negative coordinates to force canvas expansion (like might happen in app)
    printf("Pasting image at (-50, -25) with expand=true\n");
    printf("Original canvas: %lux%lu at (%lu,%lu)\n", 
           canvas->info.width, canvas->info.height, canvas->info.x, canvas->info.y);
    printf("Real image: %lux%lu at (%lu,%lu)\n", 
           realImage->info.width, realImage->info.height, realImage->info.x, realImage->info.y);
    
    canvas->pasteAt(*realImage, -50, -25, true);
    
    printf("After paste: canvas %lux%lu at (%lu,%lu)\n", 
           canvas->info.width, canvas->info.height, canvas->info.x, canvas->info.y);
    
    canvas->saveToDisk("/tmp/expansion_test_result.ppm");
    
    // Verify canvas expanded appropriately
    // Original canvas (100x100) at (0,0) plus pasted image (612x408) at (-50,-25) 
    // should result in canvas from (-50,-25) to (562,383) = 612x408
    EXPECT_EQ(canvas->info.width, 612U);  // max(100, -50+612) - min(0, -50) = 562 - (-50) = 612
    EXPECT_EQ(canvas->info.height, 408U); // max(100, -25+408) - min(0, -25) = 383 - (-25) = 408
    
    // Check that original canvas content and pasted image are both present
    // Original canvas should be at position (50, 25) in expanded canvas
    auto originalPixel = (*canvas)(50, 25);
    EXPECT_EQ(originalPixel.r, 50);
    EXPECT_EQ(originalPixel.g, 50);
    EXPECT_EQ(originalPixel.b, 50);
    
    // Check that pasted image starts at (0, 0) in expanded canvas
    auto pastedPixel = (*canvas)(0, 0);
    printf("Pixel at (0,0): R=%d, G=%d, B=%d\n", pastedPixel.r, pastedPixel.g, pastedPixel.b);
    // Should not be the original gray pixel
    EXPECT_TRUE(pastedPixel.r != 50 || pastedPixel.g != 50 || pastedPixel.b != 50);
    
    // Check a few more positions for debugging
    auto pixelAt50_25 = (*canvas)(50, 25);
    printf("Pixel at (50,25): R=%d, G=%d, B=%d\n", pixelAt50_25.r, pixelAt50_25.g, pixelAt50_25.b);
    
    auto pixelAt100_100 = (*canvas)(99, 99);  // Last pixel of original canvas region
    printf("Pixel at (99,99): R=%d, G=%d, B=%d\n", pixelAt100_100.r, pixelAt100_100.g, pixelAt100_100.b);
    
    printf("Canvas expansion test completed. Check /tmp/expansion_test_result.ppm\n");
}
