/**
 * Text Drawing Edge Cases Tests
 *
 * Tests for edge cases and boundary conditions:
 * - Out-of-bounds rendering
 * - Very large scale values
 * - Image boundary handling
 * - Memory and performance edge cases
 * - Null/invalid input handling
 */

#include <gtest/gtest.h>
#include <limits>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/text_draw.h"

using namespace linuxface;

class TextDrawEdgeCasesTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        small_image = std::make_unique<Image>(Pixel(0, 0, 0), 10, 10);
        standard_image = std::make_unique<Image>(Pixel(0, 0, 0), 100, 100);
        large_image = std::make_unique<Image>(Pixel(0, 0, 0), 1000, 1000);

        white_color = Pixel(255, 255, 255);
        red_color = Pixel(255, 0, 0);
    }

    std::unique_ptr<Image> small_image;
    std::unique_ptr<Image> standard_image;
    std::unique_ptr<Image> large_image;
    Pixel white_color;
    Pixel red_color;
};

// ===== Out-of-Bounds Rendering Tests =====
TEST_F(TextDrawEdgeCasesTest, DrawTextCompletelyOutOfBounds)
{
    // Draw text completely outside the image bounds
    drawText(*small_image, 100, 100, "Test", white_color, 1, false);

    // Image should remain unchanged
    for (int y = 0; y < small_image->info.height; y++)
    {
        for (int x = 0; x < small_image->info.width; x++)
        {
            EXPECT_EQ((*small_image)(x, y), Pixel(0, 0, 0)) << "Pixel at (" << x << "," << y << ") should remain black";
        }
    }
}

TEST_F(TextDrawEdgeCasesTest, DrawTextNegativeCoordinates)
{
    // Draw text at negative coordinates
    drawText(*standard_image, -50, -50, "Test", white_color, 1, false);

    // Should not crash, and visible pixels should be unaffected
    // (implementation may clip or ignore out-of-bounds pixels)
    EXPECT_NO_THROW(drawText(*standard_image, -10, -10, "Hi", white_color, 1, false));
}

TEST_F(TextDrawEdgeCasesTest, DrawTextPartiallyOutOfBounds)
{
    // Draw text that extends beyond image boundaries
    int img_width = standard_image->info.width;
    int img_height = standard_image->info.height;

    // Text starting near right edge
    drawText(*standard_image, img_width - 10, 10, "LongText", white_color, 1, false);

    // Text starting near bottom edge
    drawText(*standard_image, 10, img_height - 4, "Test", white_color, 1, false);

    // Should not crash
    EXPECT_NO_THROW(drawText(*standard_image, img_width - 5, img_height - 5, "X", white_color, 1, false));
}

TEST_F(TextDrawEdgeCasesTest, FillBlockOutOfBounds)
{
    // Fill block completely out of bounds
    EXPECT_NO_THROW(small_image->fillRect(100, 100, 5, 5, white_color));

    // Fill block at negative coordinates
    EXPECT_NO_THROW(small_image->fillRect(-10, -10, 5, 5, white_color));

    // Fill block partially out of bounds
    EXPECT_NO_THROW(small_image->fillRect(8, 8, 5, 5, white_color));
}

// ===== Large Scale Value Tests =====
TEST_F(TextDrawEdgeCasesTest, VeryLargeScale)
{
    // Test very large scale values
    EXPECT_NO_THROW(drawText(*large_image, 10, 10, "A", white_color, 100, false));
    EXPECT_NO_THROW(drawText(*large_image, 10, 10, "B", white_color, 500, false));

    // Should not crash even with extremely large scale
    EXPECT_NO_THROW(drawText(*large_image, 10, 10, "C", white_color, 1000, false));
}

TEST_F(TextDrawEdgeCasesTest, ScaleOverflowInTextSize)
{
    // Test scale values that might cause integer overflow in size calculations
    std::string long_text = "This is a very long text string that should test overflow conditions";

    // With large scale, width calculation might overflow
    EXPECT_NO_THROW({
        TextSize size = getTextSize(long_text, 1000);
        // Just make sure we get some result without crashing
        EXPECT_GE(size.width, 0); // Might wrap around to negative in overflow case
        EXPECT_GE(size.height, 0);
    });
}

// ===== Very Long Text Tests =====
TEST_F(TextDrawEdgeCasesTest, VeryLongText)
{
    // Create a very long text string
    std::string long_text(1000, 'A');

    EXPECT_NO_THROW(drawText(*large_image, 10, 10, long_text, white_color, 1, false));

    // Test text size calculation for long text
    TextSize size = getTextSize(long_text, 1);
    EXPECT_EQ(size.width, 8000); // 1000 chars * 8 pixels each
    EXPECT_EQ(size.height, 8);
}

TEST_F(TextDrawEdgeCasesTest, EmptyAndWhitespaceStrings)
{
    // Empty string
    EXPECT_NO_THROW(drawText(*standard_image, 10, 10, "", white_color, 1, false));

    // Only spaces
    EXPECT_NO_THROW(drawText(*standard_image, 10, 20, "   ", white_color, 1, false));

    // Only newlines (though basic drawText doesn't handle newlines)
    EXPECT_NO_THROW(drawText(*standard_image, 10, 30, "\n\n\n", white_color, 1, false));

    // Mixed whitespace
    EXPECT_NO_THROW(drawText(*standard_image, 10, 40, " \t \n ", white_color, 1, false));
}

// ===== Image Size Edge Cases =====
TEST_F(TextDrawEdgeCasesTest, VerySmallImage)
{
    // Create 1x1 image
    auto tiny_image = std::make_unique<Image>(Pixel(0, 0, 0), 1, 1);

    EXPECT_NO_THROW(drawText(*tiny_image, 0, 0, "A", white_color, 1, false));
    EXPECT_NO_THROW(drawCharDDA(*tiny_image, 0, 0, 'X', white_color, 1));
    EXPECT_NO_THROW(tiny_image->fillRect(0, 0, 1, 1, white_color));
}

TEST_F(TextDrawEdgeCasesTest, ZeroDimensionHandling)
{
    // Test with theoretically problematic dimensions
    // Note: Image constructor might have its own validation

    // Test text operations that should handle zero dimensions gracefully
    EXPECT_NO_THROW(standard_image->fillRect(10, 10, 0, 0, white_color));

    TextSize zero_size = getTextSize("", 1);
    EXPECT_EQ(zero_size.width, 0);
}

// ===== Character Edge Cases =====
TEST_F(TextDrawEdgeCasesTest, AllASCIICharacters)
{
    // Test all possible ASCII characters
    for (int c = 0; c < 256; c++)
    {
        char ch = static_cast<char>(c);

        // Should not crash for any character value
        EXPECT_NO_THROW(drawCharDDA(*standard_image, 10, 10, ch, white_color, 1))
            << "Character with ASCII value " << c << " should not crash";

        EXPECT_NO_THROW(isCharacterRenderable(ch)) << "isCharacterRenderable should not crash for ASCII " << c;

        EXPECT_NO_THROW(getFontGlyph(ch)) << "getFontGlyph should not crash for ASCII " << c;
    }
}

TEST_F(TextDrawEdgeCasesTest, BinaryDataAsText)
{
    // Test with binary data that might contain null terminators
    std::string binary_data;
    binary_data.push_back('\0');
    binary_data.push_back('A');
    binary_data.push_back('\0');
    binary_data.push_back('B');
    binary_data.push_back(static_cast<char>(255));

    EXPECT_NO_THROW(drawText(*standard_image, 10, 10, binary_data, white_color, 1, false));
    EXPECT_NO_THROW(countRenderableCharacters(binary_data));
}

// ===== Centering Edge Cases =====
TEST_F(TextDrawEdgeCasesTest, CenteringAtImageBoundaries)
{
    int img_width = standard_image->info.width;
    int img_height = standard_image->info.height; // Center at image corners
    EXPECT_NO_THROW(drawText(*standard_image, 0, 0, "Corner", white_color, 1, true));
    EXPECT_NO_THROW(drawText(*standard_image, img_width - 1, 0, "Corner", white_color, 1, true));
    EXPECT_NO_THROW(drawText(*standard_image, 0, img_height - 1, "Corner", white_color, 1, true));
    EXPECT_NO_THROW(drawText(*standard_image, img_width - 1, img_height - 1, "Corner", white_color, 1, true));

    // Center outside image bounds
    EXPECT_NO_THROW(drawText(*standard_image, -100, -100, "Outside", white_color, 1, true));
    EXPECT_NO_THROW(drawText(*standard_image, img_width + 100, img_height + 100, "Outside", white_color, 1, true));
}

TEST_F(TextDrawEdgeCasesTest, CenteringWithLargeText)
{
    std::string large_text(100, 'X'); // 800 pixels wide at scale 1

    // Center large text at various positions
    EXPECT_NO_THROW(drawText(*standard_image, 50, 50, large_text, white_color, 1, true));
    EXPECT_NO_THROW(drawText(*large_image, 500, 500, large_text, white_color, 5, true));
}

// ===== Memory and Performance Edge Cases =====
TEST_F(TextDrawEdgeCasesTest, RepeatedOperations)
{
    // Test many repeated operations for memory leaks or performance issues
    for (int i = 0; i < 1000; i++)
    {
        drawCharDDA(*standard_image, i % 90, (i / 90) % 90, 'A' + (i % 26), white_color, 1);
    }

    // Should complete without issues
    EXPECT_TRUE(true);
}

TEST_F(TextDrawEdgeCasesTest, RapidTextSizeCalculations)
{
    // Test rapid text size calculations
    for (int i = 0; i < 10000; i++)
    {
        std::string test_text = "Test" + std::to_string(i);
        TextSize size = getTextSize(test_text, (i % 10) + 1);
        EXPECT_GT(size.width, 0);
        EXPECT_GT(size.height, 0);
    }
}

// ===== Color Edge Cases =====
TEST_F(TextDrawEdgeCasesTest, ExtremeColorValues)
{
    // Test with extreme color values
    Pixel max_color(255, 255, 255, 255);
    Pixel min_color(0, 0, 0, 0);
    Pixel mid_color(128, 128, 128, 128);

    EXPECT_NO_THROW(drawText(*standard_image, 10, 10, "Max", max_color, 1, false));
    EXPECT_NO_THROW(drawText(*standard_image, 10, 20, "Min", min_color, 1, false));
    EXPECT_NO_THROW(drawText(*standard_image, 10, 30, "Mid", mid_color, 1, false));

    // Test individual color channels
    Pixel red_only(255, 0, 0, 255);
    Pixel green_only(0, 255, 0, 255);
    Pixel blue_only(0, 0, 255, 255);

    EXPECT_NO_THROW(drawText(*standard_image, 10, 40, "R", red_only, 1, false));
    EXPECT_NO_THROW(drawText(*standard_image, 20, 40, "G", green_only, 1, false));
    EXPECT_NO_THROW(drawText(*standard_image, 30, 40, "B", blue_only, 1, false));
}

// ===== Integer Overflow and Underflow Tests =====
TEST_F(TextDrawEdgeCasesTest, CoordinateOverflow)
{
    // Test with very large coordinates that might cause overflow
    int large_coord = std::numeric_limits<int>::max() / 2;

    EXPECT_NO_THROW(drawText(*standard_image, large_coord, large_coord, "Test", white_color, 1, false));
    EXPECT_NO_THROW(standard_image->fillRect(large_coord, large_coord, 1, 1, white_color));
}

TEST_F(TextDrawEdgeCasesTest, CoordinateUnderflow)
{
    // Test with very small (negative) coordinates
    int small_coord = std::numeric_limits<int>::min() / 2;

    EXPECT_NO_THROW(drawText(*standard_image, small_coord, small_coord, "Test", white_color, 1, false));
    EXPECT_NO_THROW(standard_image->fillRect(small_coord, small_coord, 1, 1, white_color));
}

// ===== Stress Tests =====
TEST_F(TextDrawEdgeCasesTest, MixedOperationsStress)
{
    // Stress test with mixed operations
    for (int i = 0; i < 100; i++)
    {
        // Random-ish positions and scales
        int x = (i * 17) % standard_image->info.width;
        int y = (i * 23) % standard_image->info.height;
        int scale = (i % 5) + 1;
        char c = 'A' + (i % 26);

        EXPECT_NO_THROW({
            drawCharDDA(*standard_image, x, y, c, white_color, scale);
            standard_image->fillRect(x + 10, y + 10, scale, scale, red_color);

            std::string text = "T" + std::to_string(i % 10);
            drawText(*standard_image, x, y + 20, text, white_color, scale, i % 2 == 0);
        });
    }
}

TEST_F(TextDrawEdgeCasesTest, AllFontGlyphsAccessible)
{
    // Verify all font glyphs can be accessed without issues
    for (int c = 0; c <= 127; c++)
    {
        const unsigned char* glyph = getFontGlyph(static_cast<char>(c));
        EXPECT_NE(glyph, nullptr) << "Font glyph for ASCII " << c << " should be accessible";

        // Verify we can read all 8 bytes of the glyph data
        EXPECT_NO_THROW({
            for (int row = 0; row < 8; row++)
            {
                unsigned char volatile byte = glyph[row]; // Volatile to prevent optimization
                (void) byte;                              // Suppress unused variable warning
            }
        }) << "Should be able to read all glyph data for ASCII "
           << c;
    }
}
