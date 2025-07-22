#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "LinuxFace/imageLoader.h"

using namespace linuxface;

const std::string assets_folder_path = "../tests/image_loader/assets/";

TEST(ImageFormatDetectorTest, DetectFormatFromData)
{
    // JPEG signature
    std::vector<unsigned char> jpeg_data = {0xFF, 0xD8, 0xFF, 0xE0};
    EXPECT_EQ(ImageFormatDetector::detectFormat(jpeg_data), ImageFormat::JPEG);
    // PNG signature
    std::vector<unsigned char> png_data = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    EXPECT_EQ(ImageFormatDetector::detectFormat(png_data), ImageFormat::PNG);
    // BMP signature
    std::vector<unsigned char> bmp_data = {0x42, 0x4D, 0x00, 0x00};
    EXPECT_EQ(ImageFormatDetector::detectFormat(bmp_data), ImageFormat::BMP);
    // Unknown
    std::vector<unsigned char> unknown_data = {0x00, 0x00};
    EXPECT_EQ(ImageFormatDetector::detectFormat(unknown_data), ImageFormat::UNKNOWN);
}

TEST(ImageFormatDetectorTest, DetectFormatFromPath)
{
    EXPECT_EQ(ImageFormatDetector::detectFormatFromPath("test.jpg"), ImageFormat::JPEG);
    EXPECT_EQ(ImageFormatDetector::detectFormatFromPath("test.jpeg"), ImageFormat::JPEG);
    EXPECT_EQ(ImageFormatDetector::detectFormatFromPath("test.png"), ImageFormat::PNG);
    EXPECT_EQ(ImageFormatDetector::detectFormatFromPath("test.bmp"), ImageFormat::BMP);
    EXPECT_EQ(ImageFormatDetector::detectFormatFromPath("test.txt"), ImageFormat::UNKNOWN);
}

TEST(ImageLoaderTest, LoadInvalidFile)
{
    ImageLoader loader(ImageLoader::LoadStrategy::LAZY);
    EXPECT_FALSE(loader.loadFromFile("nonexistent_file.jpg"));
    EXPECT_FALSE(loader.isValid());
}

TEST(ImageLoaderTest, LoadEmptyFile)
{
    std::string test_path = "assets/empty.jpg";
    std::ofstream out(test_path, std::ios::binary);
    out.close();
    ImageLoader loader(ImageLoader::LoadStrategy::LAZY);
    EXPECT_FALSE(loader.loadFromFile(test_path));
    EXPECT_FALSE(loader.isValid());
}

TEST(ImageLoaderTest, LoadCorruptedJPEGFile)
{
    std::string test_path = assets_folder_path + "corrupt.jpg";
    std::ofstream out(test_path, std::ios::binary);
    out << "corrupteddata";
    out.close();
    ImageLoader loader(ImageLoader::LoadStrategy::LAZY);
    EXPECT_FALSE(loader.loadFromFile(test_path));
    EXPECT_FALSE(loader.isValid());
}

TEST(ImageLoaderTest, LoadStrategyImmediate)
{
    std::string test_path = assets_folder_path + "image.jpg";
    std::ifstream f(test_path);
    if (!f.good())
    {
        GTEST_SKIP() << "Test JPEG file not found: " << test_path;
    }
    ImageLoader loader(ImageLoader::LoadStrategy::IMMEDIATE);
    ASSERT_TRUE(loader.loadFromFile(test_path));
    EXPECT_TRUE(loader.isValid());
    std::unique_ptr<Image> img;
    EXPECT_TRUE(loader.getImage(img));
    EXPECT_NE(img, nullptr);
    EXPECT_GT(img->size(), 0);
}

TEST(ImageLoaderTest, LoadStrategyMetadataOnly)
{
    std::string test_path = assets_folder_path + "image.jpg";
    std::ifstream f(test_path);
    if (!f.good())
    {
        GTEST_SKIP() << "Test JPEG file not found: " << test_path;
    }
    ImageLoader loader(ImageLoader::LoadStrategy::METADATA_ONLY);
    ASSERT_TRUE(loader.loadFromFile(test_path));
    EXPECT_TRUE(loader.isValid());
    const auto& meta = loader.getMetadata();
    EXPECT_EQ(meta.format, ImageFormat::JPEG);
    std::unique_ptr<Image> img;
    EXPECT_FALSE(loader.getImage(img)); // Should not decode image
}

TEST(ImageLoaderTest, DetectFormatFromEmptyData)
{
    std::vector<unsigned char> empty_data;
    EXPECT_EQ(ImageFormatDetector::detectFormat(empty_data), ImageFormat::UNKNOWN);
}

TEST(ImageLoaderTest, DetectFormatFromUnknownExtension)
{
    EXPECT_EQ(ImageFormatDetector::detectFormatFromPath(assets_folder_path + "image.unknown"), ImageFormat::UNKNOWN);
}

TEST(ImageLoaderTest, DetectFormatFromUppercaseExtension)
{
    EXPECT_EQ(ImageFormatDetector::detectFormatFromPath(assets_folder_path + "EXAMPLE.JPG"), ImageFormat::JPEG);
    EXPECT_EQ(ImageFormatDetector::detectFormatFromPath(assets_folder_path + "MAX1.PNG"), ImageFormat::PNG);
    EXPECT_EQ(ImageFormatDetector::detectFormatFromPath(assets_folder_path + "MAX.BMP"), ImageFormat::BMP);
}

TEST(ImageLoaderTest, LoadValidPPMFile)
{
    std::string test_path = assets_folder_path + "image.ppm";
    std::ifstream f(test_path, std::ios::binary);
    if (!f.good())
    {
        GTEST_SKIP() << "Test PPM file not found: " << test_path;
    }
    ImageLoader loader(ImageLoader::LoadStrategy::LAZY);
    ASSERT_TRUE(loader.loadFromFile(test_path));
    EXPECT_TRUE(loader.isValid());
    const auto& meta = loader.getMetadata();
    EXPECT_EQ(meta.format, ImageFormat::PPM);
    std::unique_ptr<Image> img;
    EXPECT_TRUE(loader.getImage(img));
    EXPECT_NE(img, nullptr);
    EXPECT_GT(img->size(), 0);
}

TEST(ImageLoaderTest, LoadValidJPEGFile_Assets)
{
    std::string test_path = assets_folder_path + "image.jpg";
    std::ifstream f(test_path);
    if (!f.good())
    {
        GTEST_SKIP() << "Test JPEG file not found: " << test_path;
    }
    ImageLoader loader(ImageLoader::LoadStrategy::LAZY);
    ASSERT_TRUE(loader.loadFromFile(test_path));
    EXPECT_TRUE(loader.isValid());
    const auto& meta = loader.getMetadata();
    EXPECT_EQ(meta.format, ImageFormat::JPEG);
    std::unique_ptr<Image> img;
    EXPECT_TRUE(loader.getImage(img));
    EXPECT_NE(img, nullptr);
    EXPECT_GT(img->size(), 0);
}

TEST(ImageLoaderTest, LoadCorruptedPPMFile)
{
    std::string test_path = assets_folder_path + "corrupt.ppm";
    // Write a bad header
    std::ofstream out(test_path, std::ios::binary);
    out << "P6\n2 2\n255\n"; // header OK
    out << "\xFF\x00\x00";   // only 3 bytes, should be 12 for 2x2
    out.close();
    ImageLoader loader(ImageLoader::LoadStrategy::LAZY);
    EXPECT_FALSE(loader.loadFromFile(test_path));
    EXPECT_FALSE(loader.isValid());
}

TEST(ImageLoaderTest, LoadUnsupportedGIFFile)
{
    std::string test_path = assets_folder_path + "image.gif";
    std::ofstream out(test_path, std::ios::binary);
    // Minimal GIF header
    out << "GIF89a";
    out.close();
    ImageLoader loader(ImageLoader::LoadStrategy::LAZY);
    EXPECT_FALSE(loader.loadFromFile(test_path));
    EXPECT_FALSE(loader.isValid());
}
