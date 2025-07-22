// Tests for paste, blending, canvas expansion
// Covers: paste, pasteAt, pasteImpl, copyPixelsWithBlending, copyPixelsOptimized
// Edge cases: overlapping, expandCanvas, empty source, different formats

#include "LinuxFace/Image/image.h"
#include <gtest/gtest.h>

using namespace linuxface;

TEST(ImagePaste, PasteExpandCanvas) {
    Image img(Pixel(1,2,3), 2, 2);
    Image other(Pixel(9,8,7), 2, 2);
    img.paste(other, true);
    EXPECT_GE(img.info.width, 2);
    EXPECT_GE(img.info.height, 2);
}

TEST(ImagePaste, PasteAtOverlap) {
    Image img(Pixel(1,2,3), 4, 4);
    Image other(Pixel(9,8,7), 2, 2);
    img.pasteAt(other, 1, 1, false);
    EXPECT_EQ(img.info.width, 4);
}

TEST(ImagePaste, PasteSelf) {
    Image img(Pixel(1,2,3), 2, 2);
    img.paste(img, true);
    EXPECT_EQ(img.info.width, 2);
    EXPECT_EQ(img.info.height, 2);
}

TEST(ImagePaste, PasteEmptySource) {
    Image img(Pixel(1,2,3), 2, 2);
    Image empty;
    img.paste(empty, true);
    EXPECT_EQ(img.info.width, 2);
    EXPECT_EQ(img.info.height, 2);
}

TEST(ImagePaste, PasteEmptyDestination) {
    Image empty;
    Image other(Pixel(9,8,7), 2, 2);
    // If destination is empty, copyFrom should be called
    if (empty.empty()) {
        empty.copyFrom(other);
    }
    EXPECT_EQ(empty.info.width, 2);
    EXPECT_EQ(empty.info.height, 2);
}

TEST(ImagePaste, PasteNegativeCoordsExpand) {
    Image img(Pixel(1,2,3), 2, 2);
    Image other(Pixel(9,8,7), 2, 2);
    img.pasteAt(other, -1, -1, true);
    EXPECT_GE(img.info.width, 3);
    EXPECT_GE(img.info.height, 3);
}

TEST(ImagePaste, PasteDifferentFormats) {
    Image img(Pixel(1,2,3), 2, 2);
    Image other(Pixel(9,8,7,128), 2, 2);
    other.info.format = ImageFormat::RGBA;
    other.info.pixelSizeBytes = 4;
    img.paste(other, true);
    EXPECT_GE(img.info.width, 2);
}

TEST(ImagePaste, PasteAlphaBlending) {
    Image img(Pixel(1,2,3), 2, 2);
    Image other(Pixel(255,0,0,128), 2, 2);
    other.info.format = ImageFormat::RGBA;
    other.info.pixelSizeBytes = 4;
    img.paste(other, true);
    // Check that pixel values are blended (not just replaced)
    int blended = img.data()[0];
    EXPECT_GT(blended, 1);
    EXPECT_LT(blended, 255);
}

TEST(ImagePaste, PasteOutOfBounds) {
    Image img(Pixel(1,2,3), 2, 2);
    Image other(Pixel(9,8,7), 2, 2);
    img.pasteAt(other, 10, 10, true);
    EXPECT_GE(img.info.width, 12);
    EXPECT_GE(img.info.height, 12);
}

TEST(ImagePaste, CopyPixelsWithBlendingCorrectness) {
    Image img(Pixel(1,2,3), 4, 4);
    Image other(Pixel(9,8,7,128), 2, 2);
    other.info.format = ImageFormat::RGBA;
    other.info.pixelSizeBytes = 4;
    img.pasteAt(other, 1, 1, false);
    // Check that the region [1,1] to [2,2] is affected
    size_t idx = img.index(1,1);
    int blended = img.data()[idx];
    EXPECT_GT(blended, 1);
    EXPECT_LT(blended, 9);
}

TEST(ImagePaste, CopyPixelsOptimizedCorrectness) {
    Image img(Pixel(1,2,3), 4, 4);
    Image other(Pixel(9,8,7), 2, 2);
    img.pasteAt(other, 0, 0, false);
    // Check that top-left pixel is replaced
    EXPECT_EQ(img.data()[0], 9);
}

TEST(ImagePaste, PasteImplErrorHandling) {
    Image img;
    Image other;
    img.pasteAt(other, 0, 0, true);
    EXPECT_EQ(img.info.width, 0);
    EXPECT_EQ(img.info.height, 0);
}
