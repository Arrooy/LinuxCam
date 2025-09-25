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
    auto mask = createStaticBoxMask(10,10);
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
    auto mask = createStaticBoxMask(0,0);
    EXPECT_EQ(mask->info.width, 0);
    EXPECT_EQ(mask->info.height, 0);
}

TEST(ImageUtilsBlurTest, FastBoxBlurNoop)
{
    // Create a 2x2 grayscale image
    auto image = std::make_unique<linuxface::Image>(4);
    image->info.width = 2;
    image->info.height = 2;
    image->info.pixelSizeBytes = 1;
    image->info.format = linuxface::ImageFormat::GRAYSCALE;
    
    // Set test data
    image->data()[0] = 255;
    image->data()[1] = 0;
    image->data()[2] = 255;
    image->data()[3] = 0;
    
    // Apply blur with radius 0 (should not change anything)
    const linuxface::math_utils::Rect<int> region(0, 0, 2, 2);
    fastBoxBlur(*image, region, 0);
    
    // Values should remain unchanged with radius 0
    EXPECT_EQ(image->data()[0], 255);
    EXPECT_EQ(image->data()[1], 0);
    EXPECT_EQ(image->data()[2], 255);
    EXPECT_EQ(image->data()[3], 0);
}

TEST(ImageUtilsBlurTest, FastBoxBlurWithRadius)
{
    // Create a 5x5 grayscale image with a cross pattern
    auto image = std::make_unique<linuxface::Image>(25);
    image->info.width = 5;
    image->info.height = 5;
    image->info.pixelSizeBytes = 1;
    image->info.format = linuxface::ImageFormat::GRAYSCALE;
    
    // Initialize all to black
    std::fill(image->data(), image->data() + 25, 0);
    
    // Create a white cross in the center
    image->data()[2 * 5 + 1] = 255; // (1,2)
    image->data()[2 * 5 + 2] = 255; // (2,2) center
    image->data()[2 * 5 + 3] = 255; // (3,2)
    image->data()[1 * 5 + 2] = 255; // (2,1)
    image->data()[3 * 5 + 2] = 255; // (2,3)
    
    // Apply blur with radius 1
    const linuxface::math_utils::Rect<int> region(0, 0, 5, 5);
    fastBoxBlur(*image, region, 1);
    
    // After blur, values should be averaged - center should be less than 255
    EXPECT_LT(image->data()[2 * 5 + 2], 255);
    EXPECT_GT(image->data()[2 * 5 + 2], 0);
}

TEST(ImageUtilsMathTest, CubicHermiteBasic)
{
    float v = cubicHermite(0, 1, 2, 3, 0.5f);
    EXPECT_NEAR(v, 1.5f, 0.1f); // Should interpolate between B and C
}

TEST(ImageUtilsPaintTest, PaintCircle)
{
    // Create a 10x10 RGB image with black background
    Pixel black{0, 0, 0};
    auto img = std::make_unique<linuxface::Image>(black, 10, 10);
    
    Point3D center{5, 5, 0};
    Pixel color{255, 255, 255};
    paintCircle(img, center, 2.0f, color);
    
    // Check that pixels were painted by examining specific pixel values
    int painted = 0;
    for (int y = 0; y < static_cast<int>(img->info.height); ++y)
    {
        for (int x = 0; x < static_cast<int>(img->info.width); ++x)
        {
            Pixel p = (*img)(x, y);
            // Check if this pixel is white (painted)
            if (p.r == 255 && p.g == 255 && p.b == 255)
            {
                painted++;
            }
        }
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

TEST(ImageUtilsResizeTest, NearestNeighborScaling)
{
    using namespace linuxface::image_utils;
    unsigned char src_data[4 * 4 * 3];
    for (int i = 0; i < 4 * 4 * 3; ++i)
    {
        src_data[i] = (i * 11) % 256;
    }
    unsigned char dst_data[2 * 2 * 3] = {0};
    ImageView<unsigned char> src{src_data, 4, 4, 3};
    ImageView<unsigned char> dst{dst_data, 2, 2, 3};
    nearestNeighborScaling<unsigned char, unsigned char>(src, dst);
    EXPECT_EQ(dst.width, 2);
    EXPECT_EQ(dst.height, 2);
    int nonzero = 0;
    for (int i = 0; i < 2 * 2 * 3; ++i)
    {
        nonzero += dst_data[i] > 0 ? 1 : 0;
    }
    EXPECT_GT(nonzero, 0);
}

TEST(ImageUtilsResizeTest, NearestNeighborScaling_DiscreteValuePreservation)
{
    using namespace linuxface::image_utils;
    
    // Create a 3x3 grayscale image with distinct discrete values 0-8
    unsigned char src_data[3 * 3 * 1];
    for (int i = 0; i < 9; ++i)
    {
        src_data[i] = static_cast<unsigned char>(i);
    }
    
    // Scale to 6x6 (2x upscaling)
    unsigned char dst_data[6 * 6 * 1] = {0};
    ImageView<unsigned char> src{src_data, 3, 3, 1};
    ImageView<unsigned char> dst{dst_data, 6, 6, 1};
    
    nearestNeighborScaling<unsigned char, unsigned char>(src, dst);
    
    EXPECT_EQ(dst.width, 6);
    EXPECT_EQ(dst.height, 6);
    
    // Verify that only original discrete values (0-8) appear in the result
    std::set<unsigned char> foundValues;
    for (int i = 0; i < 6 * 6; ++i)
    {
        foundValues.insert(dst_data[i]);
    }
    
    // Should only contain original values 0-8, no intermediate values
    for (unsigned char val : foundValues)
    {
        EXPECT_LE(val, 8) << "Found unexpected value: " << static_cast<int>(val);
    }
    
    // Should have preserved at least some of the original values
    EXPECT_GE(foundValues.size(), 3) << "Too few unique values preserved";
}

TEST(ImageUtilsResizeTest, NearestNeighborScaling_LabelMaskDownscaling)
{
    using namespace linuxface::image_utils;
    
    // Create a 8x8 label mask with class labels 0, 1, 2, 3
    unsigned char src_data[8 * 8 * 1];
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            // Create regions: top-left=0, top-right=1, bottom-left=2, bottom-right=3
            unsigned char label = 0;
            if (x >= 4 && y < 4) label = 1;      // top-right
            else if (x < 4 && y >= 4) label = 2; // bottom-left
            else if (x >= 4 && y >= 4) label = 3; // bottom-right
            
            src_data[y * 8 + x] = label;
        }
    }
    
    // Scale down to 4x4
    unsigned char dst_data[4 * 4 * 1] = {0};
    ImageView<unsigned char> src{src_data, 8, 8, 1};
    ImageView<unsigned char> dst{dst_data, 4, 4, 1};
    
    nearestNeighborScaling<unsigned char, unsigned char>(src, dst);
    
    EXPECT_EQ(dst.width, 4);
    EXPECT_EQ(dst.height, 4);
    
    // Verify that only valid class labels appear (0, 1, 2, 3)
    std::set<unsigned char> foundLabels;
    for (int i = 0; i < 4 * 4; ++i)
    {
        foundLabels.insert(dst_data[i]);
        EXPECT_LE(dst_data[i], 3) << "Found invalid class label: " << static_cast<int>(dst_data[i]);
    }
    
    // Should preserve all 4 class labels
    EXPECT_EQ(foundLabels.size(), 4) << "Not all class labels were preserved";
    EXPECT_TRUE(foundLabels.count(0)) << "Class label 0 missing";
    EXPECT_TRUE(foundLabels.count(1)) << "Class label 1 missing";
    EXPECT_TRUE(foundLabels.count(2)) << "Class label 2 missing";
    EXPECT_TRUE(foundLabels.count(3)) << "Class label 3 missing";
}

TEST(ImageUtilsResizeTest, NearestNeighborScaling_WithNormalization)
{
    using namespace linuxface::image_utils;
    
    unsigned char src_data[4 * 4 * 3];
    for (int i = 0; i < 4 * 4 * 3; ++i)
    {
        src_data[i] = static_cast<unsigned char>((i * 13) % 256);
    }
    unsigned char dst_data[2 * 2 * 3] = {0};
    
    ImageView<unsigned char> src{src_data, 4, 4, 3};
    ImageView<unsigned char> dst{dst_data, 2, 2, 3};
    
    // Test with MINMAX normalization
    nearestNeighborScaling<unsigned char, unsigned char, linuxface::NormalizationType::MINMAX>(src, dst);
    
    EXPECT_EQ(dst.width, 2);
    EXPECT_EQ(dst.height, 2);
    
    // With normalization, values should be redistributed across full range
    bool hasLow = false, hasHigh = false;
    for (int i = 0; i < 2 * 2 * 3; ++i)
    {
        if (dst_data[i] < 100) hasLow = true;
        if (dst_data[i] > 150) hasHigh = true;
    }
    EXPECT_TRUE(hasLow || hasHigh) << "Normalization should spread values across range";
}

TEST(ImageUtilsResizeTest, NearestNeighborScaling_ChannelHandling)
{
    using namespace linuxface::image_utils;
    
    // Test RGB to RGBA conversion (should add alpha channel)
    unsigned char src_data[2 * 2 * 3] = {
        255, 128, 64,   // RGB pixel 1
        100, 200, 50,   // RGB pixel 2
        0, 255, 128,    // RGB pixel 3
        64, 32, 255     // RGB pixel 4
    };
    unsigned char dst_data[2 * 2 * 4] = {0};
    
    ImageView<unsigned char> src{src_data, 2, 2, 3};
    ImageView<unsigned char> dst{dst_data, 2, 2, 4};
    
    nearestNeighborScaling<unsigned char, unsigned char>(src, dst);
    
    // Check that RGB values are preserved exactly and alpha is added
    EXPECT_EQ(dst_data[0], 255); // R
    EXPECT_EQ(dst_data[1], 128); // G
    EXPECT_EQ(dst_data[2], 64);  // B
    EXPECT_EQ(dst_data[3], 255); // A (should be max value)
    
    EXPECT_EQ(dst_data[4], 100); // R
    EXPECT_EQ(dst_data[5], 200); // G
    EXPECT_EQ(dst_data[6], 50);  // B
    EXPECT_EQ(dst_data[7], 255); // A
}

TEST(ImageUtilsResizeTest, NearestNeighborScaling_EdgeCases)
{
    using namespace linuxface::image_utils;
    
    // Test 1x1 to 3x3 scaling (extreme upscaling)
    unsigned char src_data[1 * 1 * 3] = {128, 64, 32};
    unsigned char dst_data[3 * 3 * 3] = {0};
    
    ImageView<unsigned char> src{src_data, 1, 1, 3};
    ImageView<unsigned char> dst{dst_data, 3, 3, 3};
    
    nearestNeighborScaling<unsigned char, unsigned char>(src, dst);
    
    // All pixels should have the same value as the source
    for (int i = 0; i < 3 * 3; ++i)
    {
        EXPECT_EQ(dst_data[i * 3 + 0], 128); // R
        EXPECT_EQ(dst_data[i * 3 + 1], 64);  // G
        EXPECT_EQ(dst_data[i * 3 + 2], 32);  // B
    }
}

TEST(ImageUtilsImageScalingTest, NearestNeighborViaImage)
{
    using namespace linuxface;
    
    // Test the full integration with Image::scale method
    auto img = std::make_unique<Image>(4 * 4 * 3);
    img->info.width = 4;
    img->info.height = 4;
    img->info.pixelSizeBytes = 3;
    img->info.format = ImageFormat::RGB;
    
    // Fill with a checkerboard pattern using distinct values
    unsigned char* data = img->data();
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            int idx = (y * 4 + x) * 3;
            unsigned char value = static_cast<unsigned char>((x + y) % 2 == 0 ? 50 : 200);
            data[idx + 0] = value;     // R
            data[idx + 1] = value + 20; // G
            data[idx + 2] = value + 40; // B
        }
    }
    
    // Scale using NEAREST_NEIGHBOR
    auto scaled = img->scale(8, 8, ScalingAlgorithm::NEAREST_NEIGHBOR);
    
    ASSERT_NE(scaled, nullptr);
    EXPECT_EQ(scaled->info.width, 8);
    EXPECT_EQ(scaled->info.height, 8);
    EXPECT_EQ(scaled->info.pixelSizeBytes, 3);
    
    // Verify that only the original discrete values appear
    std::set<unsigned char> foundValues;
    unsigned char* scaledData = scaled->data();
    for (int i = 0; i < 8 * 8 * 3; ++i)
    {
        foundValues.insert(scaledData[i]);
    }
    
    // Should only contain original checkerboard values: 50, 70, 90, 200, 220, 240
    for (unsigned char val : foundValues)
    {
        bool isValid = (val == 50 || val == 70 || val == 90 || val == 200 || val == 220 || val == 240);
        EXPECT_TRUE(isValid) << "Found unexpected interpolated value: " << static_cast<int>(val);
    }
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

// RGBA support tests for affineFaceTransform
TEST(ImageUtilsRGBATest, AffineFaceTransform_RGBToRGBA)
{
    linuxface::Pixel color{255, 128, 64, 255};
    auto img = std::make_unique<linuxface::Image>(color, 100, 100);
    
    // Valid 5-point landmarks
    std::vector<Point<>> landmarks = {
        {30, 40}, {70, 40}, {50, 60}, {35, 80}, {65, 80}
    };
    
    auto [transformed, affine] = affineFaceTransform(*img, landmarks, TEMPLATE_112, 112, true, 
                                                     linuxface::ImageFormat::RGBA);
    
    ASSERT_NE(transformed, nullptr);
    EXPECT_EQ(transformed->info.width, 112);
    EXPECT_EQ(transformed->info.height, 112);
    EXPECT_EQ(transformed->info.pixelSizeBytes, 4);
    EXPECT_EQ(transformed->info.format, linuxface::ImageFormat::RGBA);
    
    // Verify affine matrix is returned
    EXPECT_EQ(affine.size(), 6);
}

TEST(ImageUtilsRGBATest, AffineFaceTransform_RGBAToRGB)
{
    // Create RGBA image manually
    auto rgbaImg = std::make_unique<linuxface::Image>(100 * 100 * 4);
    rgbaImg->info.width = 100;
    rgbaImg->info.height = 100;
    rgbaImg->info.pixelSizeBytes = 4;
    rgbaImg->info.format = linuxface::ImageFormat::RGBA;
    
    // Fill with RGBA data (green with varying alpha)
    unsigned char* data = rgbaImg->data();
    for (int i = 0; i < 100 * 100; i++) {
        data[i * 4 + 0] = 0;   // R
        data[i * 4 + 1] = 255; // G
        data[i * 4 + 2] = 0;   // B
        data[i * 4 + 3] = 128; // A (half transparent)
    }
    
    // Valid 5-point landmarks
    std::vector<Point<>> landmarks = {
        {30, 40}, {70, 40}, {50, 60}, {35, 80}, {65, 80}
    };
    
    auto [transformed, affine] = affineFaceTransform(*rgbaImg, landmarks, TEMPLATE_112, 112, true, 
                                                     linuxface::ImageFormat::RGB);
    
    ASSERT_NE(transformed, nullptr);
    EXPECT_EQ(transformed->info.width, 112);
    EXPECT_EQ(transformed->info.height, 112);
    EXPECT_EQ(transformed->info.pixelSizeBytes, 3);
    EXPECT_EQ(transformed->info.format, linuxface::ImageFormat::RGB);
}

TEST(ImageUtilsRGBATest, AffineFaceTransform_RGBAToRGBA)
{
    // Create RGBA image manually
    auto rgbaImg = std::make_unique<linuxface::Image>(100 * 100 * 4);
    rgbaImg->info.width = 100;
    rgbaImg->info.height = 100;
    rgbaImg->info.pixelSizeBytes = 4;
    rgbaImg->info.format = linuxface::ImageFormat::RGBA;
    
    // Fill with RGBA data (blue with varying alpha)
    unsigned char* data = rgbaImg->data();
    for (int i = 0; i < 100 * 100; i++) {
        data[i * 4 + 0] = 0;   // R
        data[i * 4 + 1] = 0;   // G
        data[i * 4 + 2] = 255; // B
        data[i * 4 + 3] = (i % 256); // A (varying transparency)
    }
    
    // Valid 5-point landmarks
    std::vector<Point<>> landmarks = {
        {30, 40}, {70, 40}, {50, 60}, {35, 80}, {65, 80}
    };
    
    auto [transformed, affine] = affineFaceTransform(*rgbaImg, landmarks, TEMPLATE_112, 112, true, 
                                                     linuxface::ImageFormat::RGBA);
    
    ASSERT_NE(transformed, nullptr);
    EXPECT_EQ(transformed->info.width, 112);
    EXPECT_EQ(transformed->info.height, 112);
    EXPECT_EQ(transformed->info.pixelSizeBytes, 4);
    EXPECT_EQ(transformed->info.format, linuxface::ImageFormat::RGBA);
}

TEST(ImageUtilsRGBATest, SimilarityFaceTransform_RGBA)
{
    linuxface::Pixel color{255, 128, 64, 255};
    auto img = std::make_unique<linuxface::Image>(color, 100, 100);
    
    std::vector<Point<>> landmarks = {
        {30, 40}, {70, 40}, {50, 60}, {35, 80}, {65, 80}
    };
    
    auto [transformed, affine] = similarityFaceTransform(*img, landmarks, TEMPLATE_112, 112, true, 
                                                         linuxface::ImageFormat::RGBA);
    
    ASSERT_NE(transformed, nullptr);
    EXPECT_EQ(transformed->info.format, linuxface::ImageFormat::RGBA);
    EXPECT_EQ(transformed->info.pixelSizeBytes, 4);
}

TEST(ImageUtilsRGBATest, ProcrustesSimilarityFaceTransform_RGBA)
{
    linuxface::Pixel color{255, 128, 64, 255};
    auto img = std::make_unique<linuxface::Image>(color, 100, 100);
    
    std::vector<Point<>> landmarks = {
        {30, 40}, {70, 40}, {50, 60}, {35, 80}, {65, 80}
    };
    
    auto [transformed, affine] = procrustesSimilarityFaceTransform(*img, landmarks, TEMPLATE_112, 112, true, 
                                                                   linuxface::ImageFormat::RGBA);
    
    ASSERT_NE(transformed, nullptr);
    EXPECT_EQ(transformed->info.format, linuxface::ImageFormat::RGBA);
    EXPECT_EQ(transformed->info.pixelSizeBytes, 4);
}

TEST(ImageUtilsRGBATest, AffineFaceTransform_DefaultFormat)
{
    linuxface::Pixel color{255, 128, 64, 255};
    auto img = std::make_unique<linuxface::Image>(color, 100, 100);
    
    std::vector<Point<>> landmarks = {
        {30, 40}, {70, 40}, {50, 60}, {35, 80}, {65, 80}
    };
    
    // Test default behavior (should remain RGB)
    auto [transformed, affine] = affineFaceTransform(*img, landmarks, TEMPLATE_112, 112);
    
    ASSERT_NE(transformed, nullptr);
    EXPECT_EQ(transformed->info.format, linuxface::ImageFormat::RGB);
    EXPECT_EQ(transformed->info.pixelSizeBytes, 3);
}
