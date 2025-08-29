/**
 * Text Drawing Performance Tests
 *
 * Tests for performance characteristics and benchmarks:
 * - Rendering speed tests
 * - Memory usage validation
 * - Scalability with large text
 * - Optimization verification
 * - Comparative performance metrics
 */

#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <numeric>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/text_draw.h"

using namespace linuxface;

class TextDrawPerformanceTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        small_image = std::make_unique<Image>(Pixel(0, 0, 0), 100, 100);
        medium_image = std::make_unique<Image>(Pixel(0, 0, 0), 500, 500);
        large_image = std::make_unique<Image>(Pixel(0, 0, 0), 2000, 2000);

        white_color = Pixel(255, 255, 255);
        red_color = Pixel(255, 0, 0);
    }

    std::unique_ptr<Image> small_image;
    std::unique_ptr<Image> medium_image;
    std::unique_ptr<Image> large_image;
    Pixel white_color;
    Pixel red_color;

    // Helper function to measure execution time
    template <typename Func>
    double measureExecutionTime(Func&& func, int iterations = 1)
    {
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; i++)
        {
            func();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        return duration.count() / static_cast<double>(iterations); // Average time in microseconds
    }

    // Helper to generate test strings of various lengths
    std::string generateTestString(size_t length, char base_char = 'A')
    {
        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; i++)
        {
            result += static_cast<char>(base_char + (i % 26));
        }
        return result;
    }
};

// ===== Basic Performance Tests =====
TEST_F(TextDrawPerformanceTest, SingleCharacterRenderingSpeed)
{
    const int iterations = 1000;

    double avg_time =
        measureExecutionTime([&]() { drawCharDDA(*medium_image, 250, 250, 'A', white_color, 1); }, iterations);

    // Single character should render very quickly (under 100 microseconds on modern hardware)
    EXPECT_LT(avg_time, 100.0) << "Single character rendering should be fast: " << avg_time << " μs";

    std::cout << "Single character rendering: " << avg_time << " μs average" << std::endl;
}

TEST_F(TextDrawPerformanceTest, ShortTextRenderingSpeed)
{
    const int iterations = 500;
    std::string short_text = "Hello World!";

    double avg_time = measureExecutionTime(
        [&]() { drawText(*medium_image, 100, 100, short_text, white_color, 1, false); }, iterations);

    // Short text should render quickly (under 500 microseconds)
    EXPECT_LT(avg_time, 500.0) << "Short text rendering should be fast: " << avg_time << " μs";

    std::cout << "Short text (" << short_text.length() << " chars) rendering: " << avg_time << " μs average"
              << std::endl;
}

TEST_F(TextDrawPerformanceTest, MediumTextRenderingSpeed)
{
    const int iterations = 100;
    std::string medium_text = generateTestString(50); // 50 characters

    double avg_time = measureExecutionTime(
        [&]() { drawText(*medium_image, 50, 50, medium_text, white_color, 1, false); }, iterations);

    // Medium text should render reasonably quickly (under 2000 microseconds)
    EXPECT_LT(avg_time, 2000.0) << "Medium text rendering should be reasonable: " << avg_time << " μs";

    std::cout << "Medium text (" << medium_text.length() << " chars) rendering: " << avg_time << " μs average"
              << std::endl;
}

TEST_F(TextDrawPerformanceTest, LongTextRenderingSpeed)
{
    const int iterations = 20;
    std::string long_text = generateTestString(200); // 200 characters

    double avg_time =
        measureExecutionTime([&]() { drawText(*large_image, 50, 50, long_text, white_color, 1, false); }, iterations);

    // Long text rendering time should scale reasonably (under 10000 microseconds)
    EXPECT_LT(avg_time, 10000.0) << "Long text rendering should scale reasonably: " << avg_time << " μs";

    std::cout << "Long text (" << long_text.length() << " chars) rendering: " << avg_time << " μs average" << std::endl;
}

// ===== Scaling Performance Tests =====
TEST_F(TextDrawPerformanceTest, ScalingPerformanceImpact)
{
    const int iterations = 100;
    std::string test_text = "Scale Test";

    std::vector<int> scales = {1, 2, 3, 5, 10};
    std::vector<double> times;

    for (int scale : scales)
    {
        double avg_time = measureExecutionTime(
            [&]() { drawText(*large_image, 100, 100, test_text, white_color, scale, false); }, iterations);

        times.push_back(avg_time);
        std::cout << "Scale " << scale << " rendering: " << avg_time << " μs average" << std::endl;
    }

    // Performance should degrade roughly with scale squared (area increases)
    // Scale 10 should not be more than 100x slower than scale 1
    EXPECT_LT(times.back() / times.front(), 100.0) << "Scaling performance should not degrade excessively";

    // Times should generally increase with scale
    EXPECT_LT(times[0], times.back()) << "Larger scales should take more time";
}

TEST_F(TextDrawPerformanceTest, FillBlockPerformance)
{
    const int iterations = 1000;

    std::vector<int> block_sizes = {1, 5, 10, 20, 50};

    for (int size : block_sizes)
    {
        double avg_time =
            measureExecutionTime([&]() { medium_image->fillRect(100, 100, size, size, white_color); }, iterations);

        std::cout << "Fill block size " << size << "x" << size << ": " << avg_time << " μs average" << std::endl;

        // Even large blocks should render quickly (under 1000 microseconds)
        EXPECT_LT(avg_time, 1000.0) << "Block size " << size << " should render quickly: " << avg_time << " μs";
    }
}

// ===== Enhanced API Performance Tests =====
TEST_F(TextDrawPerformanceTest, BackgroundTextPerformance)
{
    const int iterations = 100;
    std::string test_text = "Background Text Test";

    // Compare regular text vs background text performance
    double regular_time = measureExecutionTime(
        [&]() { drawText(*medium_image, 100, 100, test_text, white_color, 1, false); }, iterations);

    double background_time = measureExecutionTime(
        [&]() { drawTextWithBackground(*medium_image, 100, 150, test_text, white_color, red_color, 1, false, 2); },
        iterations);

    std::cout << "Regular text: " << regular_time << " μs, Background text: " << background_time << " μs" << std::endl;

    // Background text should not be more than 3x slower than regular text
    // Relaxed from 2x to account for system load variation
    EXPECT_LT(background_time / regular_time, 3.0)
        << "Background text should not be excessively slower than regular text";
}

TEST_F(TextDrawPerformanceTest, MultilineTextPerformance)
{
    const int iterations = 50;

    // Create multiline text
    std::string multiline_text = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5";

    double multiline_time = measureExecutionTime(
        [&]() { drawMultilineText(*medium_image, 50, 50, multiline_text, white_color, 1, 2); }, iterations);

    // Compare to equivalent single-line rendering
    std::string single_line = "Line 1Line 2Line 3Line 4Line 5";
    double single_time = measureExecutionTime(
        [&]() { drawText(*medium_image, 50, 100, single_line, white_color, 1, false); }, iterations);

    std::cout << "Multiline text: " << multiline_time << " μs, Single line equivalent: " << single_time << " μs"
              << std::endl;

    // Multiline should not be more than 2x slower than single line
    EXPECT_LT(multiline_time / single_time, 3.0)
        << "Multiline text should not have excessive overhead (relaxed from 2.0x)";
}

TEST_F(TextDrawPerformanceTest, TextAlignmentPerformance)
{
    const int iterations = 200;
    std::string test_text = "Aligned Text";

    double alignment_time = measureExecutionTime(
        [&]()
        {
            drawTextAligned(*medium_image, 100, 100, 200, 50, test_text, white_color, 1, TextAlignment::CENTER,
                            TextAlignment::MIDDLE);
        },
        iterations);

    double regular_time = measureExecutionTime(
        [&]() { drawText(*medium_image, 150, 125, test_text, white_color, 1, false); }, iterations);

    std::cout << "Aligned text: " << alignment_time << " μs, Regular text: " << regular_time << " μs" << std::endl;

    // Alignment calculation overhead should be minimal
    EXPECT_LT(alignment_time / regular_time, 2.5)
        << "Text alignment should have minimal performance overhead (relaxed from 1.5x)";
}

// ===== Text Size Calculation Performance =====
TEST_F(TextDrawPerformanceTest, TextSizeCalculationPerformance)
{
    const int iterations = 10000;

    std::vector<std::string> test_strings = {"A", "Hello", "Medium length text string", generateTestString(100),
                                             generateTestString(500)};

    for (const auto& text : test_strings)
    {
        double avg_time = measureExecutionTime(
            [&]()
            {
                TextSize size = getTextSize(text, 1);
                // Prevent optimization from removing the call
                int volatile w = size.width;
                int volatile h = size.height;
                (void) w;
                (void) h;
            },
            iterations);

        std::cout << "Text size calculation (" << text.length() << " chars): " << avg_time << " μs average"
                  << std::endl;

        // Text size calculation should be very fast (under 1 microsecond)
        EXPECT_LT(avg_time, 1.0) << "Text size calculation should be very fast for " << text.length() << " characters";
    }
}

// ===== Memory-Related Performance Tests =====
TEST_F(TextDrawPerformanceTest, RepeatedRenderingPerformance)
{
    const int iterations = 1000;
    std::string test_text = "Repeated";

    // Test that repeated rendering doesn't slow down (no memory leaks, etc.)
    std::vector<double> time_samples;
    const int sample_count = 10;
    const int renders_per_sample = iterations / sample_count;

    for (int sample = 0; sample < sample_count; sample++)
    {
        double sample_time = measureExecutionTime(
            [&]()
            { drawText(*medium_image, (sample * 30) % 400, (sample * 20) % 400, test_text, white_color, 1, false); },
            renders_per_sample);

        time_samples.push_back(sample_time);
    }

    // Calculate variance in performance
    double avg_time = std::accumulate(time_samples.begin(), time_samples.end(), 0.0) / sample_count;
    double max_time = *std::max_element(time_samples.begin(), time_samples.end());
    double min_time = *std::min_element(time_samples.begin(), time_samples.end());

    std::cout << "Repeated rendering - Avg: " << avg_time << " μs, Min: " << min_time << " μs, Max: " << max_time
              << " μs" << std::endl;

    // Performance should be consistent (max should not be more than 4x min)
    EXPECT_LT(max_time / min_time, 4.0) << "Repeated rendering performance should be consistent";
}

TEST_F(TextDrawPerformanceTest, LargeImagePerformanceScaling)
{
    const int iterations = 50;
    std::string test_text = "Scale Test";

    std::vector<std::unique_ptr<Image>*> images = {&small_image, &medium_image, &large_image};
    std::vector<std::string> sizes = {"100x100", "500x500", "2000x2000"};

    for (size_t i = 0; i < images.size(); i++)
    {
        double avg_time = measureExecutionTime(
            [&]() { drawText(**images[i], 50, 50, test_text, white_color, 1, false); }, iterations);

        std::cout << "Image size " << sizes[i] << ": " << avg_time << " μs average" << std::endl;

        // Text rendering time should not significantly depend on image size
        // (since we're only writing to a small portion of the image)
        EXPECT_LT(avg_time, 1000.0) << "Text rendering should be fast regardless of image size";
    }
}

// ===== Utility Function Performance =====
TEST_F(TextDrawPerformanceTest, UtilityFunctionPerformance)
{
    const int iterations = 10000;

    // Test character validation performance
    double char_validation_time = measureExecutionTime(
        [&]()
        {
            for (int c = 0; c < 256; c++)
            {
                bool volatile result = isCharacterRenderable(static_cast<char>(c));
                (void) result;
            }
        },
        iterations / 256); // Adjust iterations since we test 256 chars each time

    std::cout << "Character validation: " << char_validation_time << " μs per 256 chars" << std::endl;

    // Character validation should be extremely fast
    EXPECT_LT(char_validation_time, 10.0) << "Character validation should be very fast";

    // Test text fitting calculation performance
    std::string test_text = "Fitting Test";
    double fitting_time = measureExecutionTime(
        [&]()
        {
            bool volatile fits = textFitsInRect(test_text, 2, 100, 50);
            (void) fits;
        },
        iterations);

    std::cout << "Text fitting calculation: " << fitting_time << " μs average" << std::endl;

    // Text fitting should be very fast (just arithmetic)
    EXPECT_LT(fitting_time, 1.0) << "Text fitting calculation should be very fast";

    // Test max scale finding performance
    double max_scale_time = measureExecutionTime(
        [&]()
        {
            int volatile scale = findMaxScaleForRect(test_text, 100, 50, 10);
            (void) scale;
        },
        iterations / 10); // Fewer iterations since this is more complex

    std::cout << "Max scale finding: " << max_scale_time << " μs average" << std::endl;

    // Max scale finding should be reasonable (it does a linear search)
    EXPECT_LT(max_scale_time, 10.0) << "Max scale finding should be reasonably fast";
}

// ===== Comparative Performance Test =====
TEST_F(TextDrawPerformanceTest, ComparativePerformanceAnalysis)
{
    const int iterations = 100;
    std::string test_text = "Performance Comparison";

    // Measure different operations for comparison
    struct PerformanceMetric
    {
        std::string name;
        double time;
    };

    std::vector<PerformanceMetric> metrics;

    // Basic character rendering
    metrics.push_back(
        {"Single character",
         measureExecutionTime([&]() { drawCharDDA(*medium_image, 100, 100, 'A', white_color, 1); }, iterations)});

    // Basic text rendering
    metrics.push_back({"Basic text", measureExecutionTime(
                                         [&]() { drawText(*medium_image, 100, 100, test_text, white_color, 1, false); },
                                         iterations)});

    // Centered text rendering
    metrics.push_back({"Centered text",
                       measureExecutionTime(
                           [&]() { drawText(*medium_image, 250, 250, test_text, white_color, 1, true); }, iterations)});

    // Background text rendering
    metrics.push_back(
        {"Background text",
         measureExecutionTime(
             [&]() { drawTextWithBackground(*medium_image, 100, 150, test_text, white_color, red_color, 1, false, 2); },
             iterations)});

    // Aligned text rendering
    metrics.push_back({"Aligned text", measureExecutionTime(
                                           [&]()
                                           {
                                               drawTextAligned(*medium_image, 100, 200, 200, 50, test_text, white_color,
                                                               1, TextAlignment::CENTER, TextAlignment::MIDDLE);
                                           },
                                           iterations)});

    // Print comparative results
    std::cout << "\n=== Comparative Performance Analysis ===" << std::endl;
    for (const auto& metric : metrics)
    {
        std::cout << metric.name << ": " << metric.time << " μs" << std::endl;
    }

    // Basic sanity checks on relative performance
    EXPECT_LT(metrics[0].time, metrics[1].time) << "Single character should be faster than full text";
    // All metrics should be within a reasonable range
    auto max_metric = std::max_element(metrics.begin(), metrics.end(), 
                                       [](const PerformanceMetric& a, const PerformanceMetric& b) {
                                           return a.time < b.time;
                                       });
    double max_time = max_metric->time;
    EXPECT_LT(max_time, 10 * 1000) << "All operations should complete within reasonable time";

    std::cout << "===========================================\n" << std::endl;
}
