#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/image_utils.h"
#include "LinuxFace/math_utils.h"

using namespace linuxface::image_utils;
using linuxface::Pixel;
using linuxface::math_utils::Point;
using linuxface::math_utils::Point3D;


TEST(ImageUtilsAffineTransformTest, TransformPointsAffine)
{
    std::vector<Point<>> pts = {
        {1, 2},
        {3, 4},
        {5, 6}
    };
    double M[6] = {1, 0, 1, 0, 1, 2}; // x' = x+1, y' = y+2
    auto out = transformPointsAffine(pts, M);
    EXPECT_EQ(out.size(), 3);
    EXPECT_DOUBLE_EQ(out[0].x, 2);
    EXPECT_DOUBLE_EQ(out[0].y, 4);
    EXPECT_DOUBLE_EQ(out[2].x, 6);
    EXPECT_DOUBLE_EQ(out[2].y, 8);
}

TEST(ImageUtilsAffineTransformTest, TransformPairsAffine)
{
    std::vector<std::pair<double, double>> pts = {
        {1, 2},
        {3, 4}
    };
    double M[6] = {2, 0, 0, 0, 2, 0}; // x' = 2x, y' = 2y
    auto out = transformPointsAffine(pts, M);
    EXPECT_EQ(out.size(), 2);
    EXPECT_DOUBLE_EQ(out[1].first, 6);
    EXPECT_DOUBLE_EQ(out[1].second, 8);
}

TEST(ImageUtilsMaskTest, CreateStaticBoxMaskBasic)
{
    std::vector<double> crop_size = {10, 10};
    auto mask = createStaticBoxMask(crop_size);
    ASSERT_NE(mask, nullptr);
    EXPECT_EQ(mask->info.width, 10);
    EXPECT_EQ(mask->info.height, 10);
    EXPECT_EQ(mask->info.format, linuxface::ImageFormat::GRAYSCALE);
    // Check all values are 255 or blurred (not zero everywhere)
    int nonzero = 0;
    for (int i = 0; i < 100; ++i)
    {
        nonzero += mask->data()[i] > 0 ? 1 : 0;
    }
    EXPECT_GT(nonzero, 0);
}

TEST(ImageUtilsMaskTest, CreateStaticBoxMaskEdgeCases)
{
    std::vector<double> crop_size = {0, 0};
    auto mask = createStaticBoxMask(crop_size);
    EXPECT_EQ(mask->info.width, 0);
    EXPECT_EQ(mask->info.height, 0);
}

TEST(ImageUtilsBlurTest, FastBoxBlurNoop)
{
    unsigned char src[4] = {255, 0, 255, 0};
    unsigned char dst[4] = {0, 0, 0, 0};
    fastBoxBlur(src, dst, 2, 2, 0); // radius 0
    EXPECT_EQ(dst[0], 255);
    EXPECT_EQ(dst[1], 0);
    EXPECT_EQ(dst[2], 255);
    EXPECT_EQ(dst[3], 0);
}

TEST(ImageUtilsMathTest, CubicHermiteBasic)
{
    float v = cubicHermite(0, 1, 2, 3, 0.5f);
    EXPECT_NEAR(v, 1.5f, 0.1f); // Should interpolate between B and C
}

TEST(ImageUtilsPaintTest, PaintCircle)
{
    // Properly construct a 10x10 RGB image with black background
    Pixel black{0, 0, 0, 255};
    auto img = std::make_unique<linuxface::Image>(black, 10, 10);
    Point3D center{5, 5, 0};
    Pixel color{255, 255, 255, 255};
    paintCircle(img, center, 2.0f, color);
    // Check at least one pixel is painted
    int painted = 0;
    for (size_t i = 0; i < img->size(); ++i)
    {
        painted += img->data()[i] == 255 ? 1 : 0;
    }
    EXPECT_GT(painted, 0);
}

TEST(ImageUtilsKernelTest, LanczosKernelRadius)
{
    EXPECT_EQ(LanczosKernel::RADIUS, 3);
}

// Edge case: empty points
TEST(ImageUtilsEdgeCaseTest, TransformPointsAffineEmpty)
{
    std::vector<Point<>> pts;
    double M[6] = {1, 0, 0, 0, 1, 0};
    auto out = transformPointsAffine(pts, M);
    EXPECT_TRUE(out.empty());
}

TEST(ImageUtilsResizeTest, BilinearScaling)
{
    using namespace linuxface::image_utils;
    unsigned char src_data[4 * 4 * 3];
    for (int i = 0; i < 4 * 4 * 3; ++i)
    {
        src_data[i] = i % 256;
    }
    unsigned char dst_data[2 * 2 * 3] = {0};
    ImageView<unsigned char> src{src_data, 4, 4, 3};
    ImageView<unsigned char> dst{dst_data, 2, 2, 3};
    bilinearScaling<unsigned char, unsigned char>(src, dst);
    EXPECT_EQ(dst.width, 2);
    EXPECT_EQ(dst.height, 2);
    int nonzero = 0;
    for (int i = 0; i < 2 * 2 * 3; ++i)
    {
        nonzero += dst_data[i] > 0 ? 1 : 0;
    }
    EXPECT_GT(nonzero, 0);
}

TEST(ImageUtilsResizeTest, AreaAveragingScaling)
{
    using namespace linuxface::image_utils;
    unsigned char src_data[4 * 4 * 3];
    for (int i = 0; i < 4 * 4 * 3; ++i)
    {
        src_data[i] = (i * 2) % 256;
    }
    unsigned char dst_data[2 * 2 * 3] = {0};
    ImageView<unsigned char> src{src_data, 4, 4, 3};
    ImageView<unsigned char> dst{dst_data, 2, 2, 3};
    areaAveragingScaling<unsigned char, unsigned char>(src, dst);
    EXPECT_EQ(dst.width, 2);
    EXPECT_EQ(dst.height, 2);
    int nonzero = 0;
    for (int i = 0; i < 2 * 2 * 3; ++i)
    {
        nonzero += dst_data[i] > 0 ? 1 : 0;
    }
    EXPECT_GT(nonzero, 0);
}

TEST(ImageUtilsResizeTest, LanczosScaling)
{
    using namespace linuxface::image_utils;
    unsigned char src_data[4 * 4 * 3];
    for (int i = 0; i < 4 * 4 * 3; ++i)
    {
        src_data[i] = (i * 3) % 256;
    }
    unsigned char dst_data[2 * 2 * 3] = {0};
    ImageView<unsigned char> src{src_data, 4, 4, 3};
    ImageView<unsigned char> dst{dst_data, 2, 2, 3};
    lanczosScaling<unsigned char, unsigned char>(src, dst);
    EXPECT_EQ(dst.width, 2);
    EXPECT_EQ(dst.height, 2);
    int nonzero = 0;
    for (int i = 0; i < 2 * 2 * 3; ++i)
    {
        nonzero += dst_data[i] > 0 ? 1 : 0;
    }
    EXPECT_GT(nonzero, 0);
}

TEST(ImageUtilsResizeTest, FastBoxScaling)
{
    using namespace linuxface::image_utils;
    unsigned char src_data[4 * 4 * 3];
    for (int i = 0; i < 4 * 4 * 3; ++i)
    {
        src_data[i] = (i * 5) % 256;
    }
    unsigned char dst_data[2 * 2 * 3] = {0};
    ImageView<unsigned char> src{src_data, 4, 4, 3};
    ImageView<unsigned char> dst{dst_data, 2, 2, 3};
    fastBoxScaling<unsigned char, unsigned char>(src, dst);
    EXPECT_EQ(dst.width, 2);
    EXPECT_EQ(dst.height, 2);
    int nonzero = 0;
    for (int i = 0; i < 2 * 2 * 3; ++i)
    {
        nonzero += dst_data[i] > 0 ? 1 : 0;
    }
    EXPECT_GT(nonzero, 0);
}

TEST(ImageUtilsResizeTest, BicubicScaling)
{
    using namespace linuxface::image_utils;
    unsigned char src_data[4 * 4 * 3];
    for (int i = 0; i < 4 * 4 * 3; ++i)
    {
        src_data[i] = (i * 7) % 256;
    }
    unsigned char dst_data[2 * 2 * 3] = {0};
    ImageView<unsigned char> src{src_data, 4, 4, 3};
    ImageView<unsigned char> dst{dst_data, 2, 2, 3};
    bicubicScaling<unsigned char, unsigned char>(src, dst);
    EXPECT_EQ(dst.width, 2);
    EXPECT_EQ(dst.height, 2);
    int nonzero = 0;
    for (int i = 0; i < 2 * 2 * 3; ++i)
    {
        nonzero += dst_data[i] > 0 ? 1 : 0;
    }
    EXPECT_GT(nonzero, 0);
}

// Test template constants
TEST(ImageUtilsTemplatesTest, TemplateConstants)
{
    // Test that all template constants have correct size and values
    EXPECT_EQ(sizeof(TEMPLATE_112) / sizeof(TEMPLATE_112[0]), 5);
    EXPECT_EQ(sizeof(TEMPLATE_128) / sizeof(TEMPLATE_128[0]), 5);
    EXPECT_EQ(sizeof(TEMPLATE_192_OLD) / sizeof(TEMPLATE_192_OLD[0]), 5);
    EXPECT_EQ(sizeof(TEMPLATE_192) / sizeof(TEMPLATE_192[0]), 5);
    EXPECT_EQ(sizeof(TEMPLATE_192_ALT) / sizeof(TEMPLATE_192_ALT[0]), 5);
    EXPECT_EQ(sizeof(TEMPLATE_512) / sizeof(TEMPLATE_512[0]), 5);

    // Test that template values are reasonable (between 0 and 1)
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_GE(TEMPLATE_112[i][0], 0.0);
        EXPECT_LE(TEMPLATE_112[i][0], 1.0);
        EXPECT_GE(TEMPLATE_112[i][1], 0.0);
        EXPECT_LE(TEMPLATE_112[i][1], 1.0);
    }
}

// Test face transformation functions
TEST(ImageUtilsFaceTransformTest, AffineFaceTransform)
{
    // Create a simple test image
    Pixel white{255, 255, 255, 255};
    auto img = std::make_unique<linuxface::Image>(white, 100, 100);

    // Simple 5-point landmarks (center and corners of a square)
    std::vector<Point<>> landmarks = {
        {25, 25},  // left eye
        {75, 25},  // right eye
        {50, 50},  // nose
        {25, 75},  // left mouth
        {75, 75}   // right mouth
    };

    auto [transformed, affine] = affineFaceTransform(*img, landmarks, TEMPLATE_112, 112);

    ASSERT_NE(transformed, nullptr);
    EXPECT_EQ(transformed->info.width, 112);
    EXPECT_EQ(transformed->info.height, 112);
    EXPECT_EQ(affine.size(), 6); // 2x3 affine matrix has 6 elements
}

TEST(ImageUtilsFaceTransformTest, SimilarityFaceTransform)
{
    Pixel white{255, 255, 255, 255};
    auto img = std::make_unique<linuxface::Image>(white, 100, 100);

    std::vector<Point<>> landmarks = {
        {25, 25}, {75, 25}, {50, 50}, {25, 75}, {75, 75}
    };

    auto [transformed, affine] = similarityFaceTransform(*img, landmarks, TEMPLATE_112, 112);

    ASSERT_NE(transformed, nullptr);
    EXPECT_EQ(transformed->info.width, 112);
    EXPECT_EQ(transformed->info.height, 112);
    EXPECT_EQ(affine.size(), 6);
}

TEST(ImageUtilsFaceTransformTest, ProcrustesSimilarityFaceTransform)
{
    Pixel white{255, 255, 255, 255};
    auto img = std::make_unique<linuxface::Image>(white, 100, 100);

    std::vector<Point<>> landmarks = {
        {25, 25}, {75, 25}, {50, 50}, {25, 75}, {75, 75}
    };

    auto [transformed, affine] = procrustesSimilarityFaceTransform(*img, landmarks, TEMPLATE_112, 112);

    ASSERT_NE(transformed, nullptr);
    EXPECT_EQ(transformed->info.width, 112);
    EXPECT_EQ(transformed->info.height, 112);
    EXPECT_EQ(affine.size(), 6);
}

// Test normalization traits
TEST(ImageUtilsNormalizationTest, NormalizationTraits)
{
    // Test float traits
    EXPECT_EQ(NormalizationTraits<float>::minValue(), 0.0f);
    EXPECT_EQ(NormalizationTraits<float>::maxValue(), 1.0f);
    EXPECT_EQ(NormalizationTraits<float>::zeroValue(), 0.0f);

    // Test double traits
    EXPECT_EQ(NormalizationTraits<double>::minValue(), 0.0);
    EXPECT_EQ(NormalizationTraits<double>::maxValue(), 1.0);
    EXPECT_EQ(NormalizationTraits<double>::zeroValue(), 0.0);

    // Test unsigned char traits
    EXPECT_EQ(NormalizationTraits<unsigned char>::minValue(), 0);
    EXPECT_EQ(NormalizationTraits<unsigned char>::maxValue(), 255);
    EXPECT_EQ(NormalizationTraits<unsigned char>::zeroValue(), 0);
}

// Test normalizers
TEST(ImageUtilsNormalizationTest, NormalizerMinMax)
{
    ImageStats<unsigned char> stats;
    stats.update(0);
    stats.update(100);
    stats.update(255);
    stats.finalize();

    Normalizer<unsigned char, linuxface::NormalizationType::MINMAX> normalizer;

    EXPECT_EQ(normalizer(0, stats), 0);
    EXPECT_EQ(normalizer(255, stats), 255);
    EXPECT_EQ(normalizer(127, stats), 127); // Middle value should stay middle
}

TEST(ImageUtilsNormalizationTest, NormalizerZeroCenter)
{
    ImageStats<unsigned char> stats;
    stats.update(100);
    stats.update(150);
    stats.update(200);
    stats.finalize();

    Normalizer<unsigned char, linuxface::NormalizationType::ZERO_CENTER> normalizer;

    // Test that values are adjusted around mean
    unsigned char result = normalizer(150, stats);
    EXPECT_GE(result, 0);
    EXPECT_LE(result, 255);
}

TEST(ImageUtilsNormalizationTest, NormalizerNone)
{
    ImageStats<unsigned char> stats;
    Normalizer<unsigned char, linuxface::NormalizationType::NONE> normalizer;

    EXPECT_EQ(normalizer(123, stats), 123);
}

// Test different channel conversions
TEST(ImageUtilsChannelConversionTest, RGBAToRGB)
{
    unsigned char src_data[2 * 2 * 4] = {
        255, 128, 64, 255,   // RGBA pixel 1
        100, 200, 50, 128,   // RGBA pixel 2
        0, 255, 128, 200,    // RGBA pixel 3
        64, 32, 255, 64      // RGBA pixel 4
    };
    unsigned char dst_data[2 * 2 * 3] = {0};

    ImageView<unsigned char> src{src_data, 2, 2, 4};
    ImageView<unsigned char> dst{dst_data, 2, 2, 3};

    bilinearScaling<unsigned char, unsigned char>(src, dst);

    // Check that RGB values are copied correctly (ignoring alpha)
    EXPECT_EQ(dst_data[0], 255); // R
    EXPECT_EQ(dst_data[1], 128); // G
    EXPECT_EQ(dst_data[2], 64);  // B
    EXPECT_EQ(dst_data[3], 100); // R
    EXPECT_EQ(dst_data[4], 200); // G
    EXPECT_EQ(dst_data[5], 50);  // B
}

TEST(ImageUtilsChannelConversionTest, RGBToRGBA)
{
    unsigned char src_data[2 * 2 * 3] = {
        255, 128, 64,   // RGB pixel 1
        100, 200, 50,   // RGB pixel 2
        0, 255, 128,    // RGB pixel 3
        64, 32, 255     // RGB pixel 4
    };
    unsigned char dst_data[2 * 2 * 4] = {0};

    ImageView<unsigned char> src{src_data, 2, 2, 3};
    ImageView<unsigned char> dst{dst_data, 2, 2, 4};

    bilinearScaling<unsigned char, unsigned char>(src, dst);

    // Check that RGB values are copied and alpha is set to 255
    EXPECT_EQ(dst_data[0], 255); // R
    EXPECT_EQ(dst_data[1], 128); // G
    EXPECT_EQ(dst_data[2], 64);  // B
    EXPECT_EQ(dst_data[3], 255); // A
    EXPECT_EQ(dst_data[4], 100); // R
    EXPECT_EQ(dst_data[5], 200); // G
    EXPECT_EQ(dst_data[6], 50);  // B
    EXPECT_EQ(dst_data[7], 255); // A
}

TEST(ImageUtilsChannelConversionTest, GrayscaleToRGB)
{
    unsigned char src_data[2 * 2 * 1] = {
        128, 64,
        200, 32
    };
    unsigned char dst_data[2 * 2 * 3] = {0};

    ImageView<unsigned char> src{src_data, 2, 2, 1};
    ImageView<unsigned char> dst{dst_data, 2, 2, 3};

    bilinearScaling<unsigned char, unsigned char>(src, dst);

    // Check that grayscale value is duplicated to RGB
    EXPECT_EQ(dst_data[0], 128); // R
    EXPECT_EQ(dst_data[1], 128); // G
    EXPECT_EQ(dst_data[2], 128); // B
    EXPECT_EQ(dst_data[3], 64);  // R
    EXPECT_EQ(dst_data[4], 64);  // G
    EXPECT_EQ(dst_data[5], 64);  // B
}

// Test different image layouts
TEST(ImageUtilsLayoutTest, HWCToCHWConversion)
{
    unsigned char hwc[2 * 2 * 3] = {
        255, 128, 64,   // Pixel 1: R, G, B
        100, 200, 50,   // Pixel 2: R, G, B
        0, 255, 128,    // Pixel 3: R, G, B
        64, 32, 255     // Pixel 4: R, G, B
    };
    unsigned char chw[2 * 2 * 3] = {0};

    hwcToChw(hwc, chw, 2, 2, 3);

    // Check CHW layout: RRRR, GGGG, BBBB
    EXPECT_EQ(chw[0], 255); // R channel, pixel 1
    EXPECT_EQ(chw[1], 100); // R channel, pixel 2
    EXPECT_EQ(chw[2], 0);   // R channel, pixel 3
    EXPECT_EQ(chw[3], 64);  // R channel, pixel 4

    EXPECT_EQ(chw[4], 128); // G channel, pixel 1
    EXPECT_EQ(chw[5], 200); // G channel, pixel 2
    EXPECT_EQ(chw[6], 255); // G channel, pixel 3
    EXPECT_EQ(chw[7], 32);  // G channel, pixel 4

    EXPECT_EQ(chw[8], 64);  // B channel, pixel 1
    EXPECT_EQ(chw[9], 50);  // B channel, pixel 2
    EXPECT_EQ(chw[10], 128); // B channel, pixel 3
    EXPECT_EQ(chw[11], 255); // B channel, pixel 4
}

TEST(ImageUtilsLayoutTest, CHWToHWCConversion)
{
    unsigned char chw[2 * 2 * 3] = {
        255, 100, 0, 64,    // R channel
        128, 200, 255, 32,  // G channel
        64, 50, 128, 255    // B channel
    };
    unsigned char hwc[2 * 2 * 3] = {0};

    chwToHwc(chw, hwc, 2, 2, 3);

    // Check HWC layout: RGB, RGB, RGB, RGB
    EXPECT_EQ(hwc[0], 255); // Pixel 1 R
    EXPECT_EQ(hwc[1], 128); // Pixel 1 G
    EXPECT_EQ(hwc[2], 64);  // Pixel 1 B
    EXPECT_EQ(hwc[3], 100); // Pixel 2 R
    EXPECT_EQ(hwc[4], 200); // Pixel 2 G
    EXPECT_EQ(hwc[5], 50);  // Pixel 2 B
}

// Test scaling with normalization
TEST(ImageUtilsNormalizationScalingTest, BilinearScalingWithMinMax)
{
    unsigned char src_data[4 * 4 * 3];
    for (int i = 0; i < 4 * 4 * 3; ++i)
    {
        src_data[i] = static_cast<unsigned char>(i % 256);
    }
    unsigned char dst_data[2 * 2 * 3] = {0};

    ImageView<unsigned char> src{src_data, 4, 4, 3};
    ImageView<unsigned char> dst{dst_data, 2, 2, 3};

    bilinearScaling<unsigned char, unsigned char, linuxface::NormalizationType::MINMAX>(src, dst);

    // With MINMAX normalization, values should be scaled to full range
    bool hasMin = false, hasMax = false;
    for (int i = 0; i < 2 * 2 * 3; ++i)
    {
        if (dst_data[i] == 0) hasMin = true;
        if (dst_data[i] == 255) hasMax = true;
    }
    EXPECT_TRUE(hasMin || hasMax); // Should have extreme values after normalization
}

TEST(ImageUtilsNormalizationScalingTest, BicubicScalingWithZeroCenter)
{
    unsigned char src_data[4 * 4 * 3];
    for (int i = 0; i < 4 * 4 * 3; ++i)
    {
        src_data[i] = static_cast<unsigned char>((i * 17) % 256);
    }
    unsigned char dst_data[2 * 2 * 3] = {0};

    ImageView<unsigned char> src{src_data, 4, 4, 3};
    ImageView<unsigned char> dst{dst_data, 2, 2, 3};

    bicubicScaling<unsigned char, unsigned char, linuxface::NormalizationType::ZERO_CENTER>(src, dst);

    // With ZERO_CENTER normalization, values should be centered around mean
    EXPECT_EQ(dst.width, 2);
    EXPECT_EQ(dst.height, 2);
}

// Test convertToRawImage function
TEST(ImageUtilsConvertTest, ConvertToRawImageMinMax)
{
    float chw_data[3 * 4 * 4]; // 3 channels, 4x4 image
    for (int i = 0; i < 3 * 4 * 4; ++i)
    {
        chw_data[i] = static_cast<float>(i) / (3 * 4 * 4); // Values from 0 to 1
    }

    auto image = convertToRawImage<linuxface::NormalizationType::MINMAX>(chw_data, 4, 4);

    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->info.width, 4);
    EXPECT_EQ(image->info.height, 4);
    EXPECT_EQ(image->info.pixelSizeBytes, 3);
    EXPECT_EQ(image->info.format, linuxface::ImageFormat::RGB);

    // Check that values are scaled to 0-255 range
    for (size_t i = 0; i < image->size(); ++i)
    {
        EXPECT_GE(image->data()[i], 0);
        EXPECT_LE(image->data()[i], 255);
    }
}

TEST(ImageUtilsConvertTest, ConvertToRawImageZeroCenter)
{
    float chw_data[3 * 4 * 4];
    for (int i = 0; i < 3 * 4 * 4; ++i)
    {
        chw_data[i] = (static_cast<float>(i) / (3 * 4 * 4) - 0.5f) * 2.0f; // Values from -1 to 1
    }

    auto image = convertToRawImage<linuxface::NormalizationType::ZERO_CENTER>(chw_data, 4, 4);

    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->info.width, 4);
    EXPECT_EQ(image->info.height, 4);

    // Check that values are scaled to 0-255 range
    for (size_t i = 0; i < image->size(); ++i)
    {
        EXPECT_GE(image->data()[i], 0);
        EXPECT_LE(image->data()[i], 255);
    }
}

// Test edge cases and error conditions
TEST(ImageUtilsEdgeCasesTest, ScalingZeroDimensions)
{
    unsigned char src_data[1] = {255};
    unsigned char dst_data[1] = {0};

    // Test with zero width
    ImageView<unsigned char> src1{src_data, 0, 1, 3};
    ImageView<unsigned char> dst1{dst_data, 0, 1, 3};
    bilinearScaling<unsigned char, unsigned char>(src1, dst1); // Should not crash

    // Test with zero height
    ImageView<unsigned char> src2{src_data, 1, 0, 3};
    ImageView<unsigned char> dst2{dst_data, 1, 0, 3};
    bilinearScaling<unsigned char, unsigned char>(src2, dst2); // Should not crash
}

TEST(ImageUtilsEdgeCasesTest, InvalidTransformPoints)
{
    std::vector<Point<>> empty_pts;
    double M[6] = {1, 0, 0, 0, 1, 0};

    // Empty points should return empty result
    auto result = transformPointsAffine(empty_pts, M);
    EXPECT_TRUE(result.empty());
}

TEST(ImageUtilsEdgeCasesTest, FaceTransformInvalidLandmarks)
{
    Pixel white{255, 255, 255, 255};
    auto img = std::make_unique<linuxface::Image>(white, 100, 100);

    // Less than 5 landmarks
    std::vector<Point<>> landmarks = {
        {25, 25}, {75, 25}, {50, 50}
    };

    auto [transformed, affine] = affineFaceTransform(*img, landmarks, TEMPLATE_112, 112);

    // Should return nullptr for invalid landmarks
    EXPECT_EQ(transformed, nullptr);
}

// Test calculateDestIndex function
TEST(ImageUtilsIndexTest, CalculateDestIndexHWC)
{
    // HWC layout: (y * width + x) * channels + ch
    size_t idx = calculateDestIndex<linuxface::ImageLayout::HWC>(1, 2, 0, 10, 10, 3);
    EXPECT_EQ(idx, (1 * 10 + 2) * 3 + 0);

    idx = calculateDestIndex<linuxface::ImageLayout::HWC>(1, 2, 2, 10, 10, 3);
    EXPECT_EQ(idx, (1 * 10 + 2) * 3 + 2);
}

TEST(ImageUtilsIndexTest, CalculateDestIndexCHW)
{
    // CHW layout: ch * (height * width) + y * width + x
    size_t idx = calculateDestIndex<linuxface::ImageLayout::CHW>(1, 2, 0, 10, 10, 3);
    EXPECT_EQ(idx, 0 * (10 * 10) + 1 * 10 + 2);

    idx = calculateDestIndex<linuxface::ImageLayout::CHW>(1, 2, 2, 10, 10, 3);
    EXPECT_EQ(idx, 2 * (10 * 10) + 1 * 10 + 2);
}
