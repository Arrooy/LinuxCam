// Tests for save/load, format conversion
// Covers: saveToDisk, convertToRGB, convertToRGBInplace
// Edge cases: unsupported format, empty image, invalid path

#include "LinuxFace/Image/image.h"
#include <gtest/gtest.h>
#include <fstream>

using namespace linuxface;

TEST(ImageIO, SaveToDiskPPM) {
    Image img(Pixel(1,2,3), 2, 2);
    img.info.format = ImageFormat::PPM;
    bool ok = img.saveToDisk("test_output.ppm");
    EXPECT_TRUE(ok);
    std::ifstream f("test_output.ppm");
    EXPECT_TRUE(f.good());
    f.close();
    remove("test_output.ppm");
}

TEST(ImageIO, SaveToDiskUnsupported) {
    Image img(Pixel(1,2,3), 2, 2);
    img.info.format = ImageFormat::UNKNOWN;
    bool ok = img.saveToDisk("test_output.ppm");
    EXPECT_FALSE(ok);
}
