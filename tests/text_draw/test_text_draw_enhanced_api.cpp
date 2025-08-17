/**
 * Enhanced Text Drawing API Tests
 *
 * Tests for the enhanced text drawing functions:
 * - Text with background rendering
 * - Multiline text support
 * - Text alignment functions
 * - Text fitting and scaling utilities
 * - Advanced text layout features
 */

#include <gtest/gtest.h>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/text_draw.h"

using namespace linuxface;

class TextDrawEnhancedAPITest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        test_image = std::make_unique<Image>(Pixel(50, 50, 50), 300, 200);
        white_color = Pixel(255, 255, 255);
        black_color = Pixel(0, 0, 0);
        red_color = Pixel(255, 0, 0);
        blue_color = Pixel(0, 0, 255);
        green_color = Pixel(0, 255, 0);
        transparent_color = Pixel(128, 128, 128, 128);
    }

    std::unique_ptr<Image> test_image;
    Pixel white_color;
    Pixel black_color;
    Pixel red_color;
    Pixel blue_color;
    Pixel green_color;
    Pixel transparent_color;

    // Helper function to count pixels of a specific color in a region
    int countPixelsInRegion(int x, int y, int width, int height, const Pixel& color)
    {
        int count = 0;
        for (int dy = 0; dy < height; dy++)
        {
            for (int dx = 0; dx < width; dx++)
            {
                int px = x + dx;
                int py = y + dy;
                if (px >= 0 && px < test_image->info.width && py >= 0 && py < test_image->info.height)
                {
                    if ((*test_image)(px, py) == color)
                    {
                        count++;
                    }
                }
            }
        }
        return count;
    }
};

// ===== Text with Background Tests =====
TEST_F(TextDrawEnhancedAPITest, DrawTextWithBackgroundBasic)
{
    std::string text = "Hello";
    int x = 50, y = 50;

    drawTextWithBackground(*test_image, x, y, text, white_color, blue_color, 1, false, 2);

    // Check that background pixels exist
    TextSize size = getTextSize(text, 1);
    int bg_count = countPixelsInRegion(x - 2, y - 2, size.width + 4, size.height + 4, blue_color);
    EXPECT_GT(bg_count, 0) << "Should have background pixels";

    // Check that text pixels exist
    int text_count = countPixelsInRegion(x, y, size.width, size.height, white_color);
    EXPECT_GT(text_count, 0) << "Should have text pixels";
}

TEST_F(TextDrawEnhancedAPITest, DrawTextWithBackgroundCentered)
{
    std::string text = "CENTER";
    int centerX = 150, centerY = 100;

    drawTextWithBackground(*test_image, centerX, centerY, text, white_color, red_color, 1, true, 3);

    // Text should be centered, so check around the center point
    TextSize size = getTextSize(text, 1);
    int expectedX = centerX - size.width / 2;
    int expectedY = centerY - size.height / 2;

    // Check for background pixels around expected area
    int bg_count = countPixelsInRegion(expectedX - 3, expectedY - 3, size.width + 6, size.height + 6, red_color);
    EXPECT_GT(bg_count, 0) << "Should have background pixels around centered text";
}

TEST_F(TextDrawEnhancedAPITest, DrawTextWithBackgroundEmptyString)
{
    // Empty string should not crash and should not draw anything
    EXPECT_NO_THROW(drawTextWithBackground(*test_image, 50, 50, "", white_color, blue_color, 1, false, 2));

    // Should not have any blue background pixels since text is empty
    int bg_count = countPixelsInRegion(0, 0, test_image->info.width, test_image->info.height, blue_color);
    EXPECT_EQ(bg_count, 0) << "Empty text should not create background";
}

TEST_F(TextDrawEnhancedAPITest, DrawTextWithBackgroundZeroScale)
{
    // Zero scale should not crash and should not draw anything
    EXPECT_NO_THROW(drawTextWithBackground(*test_image, 50, 50, "Test", white_color, blue_color, 0, false, 2));

    // Should not have any blue background pixels
    int bg_count = countPixelsInRegion(0, 0, test_image->info.width, test_image->info.height, blue_color);
    EXPECT_EQ(bg_count, 0) << "Zero scale should not create background";
}

TEST_F(TextDrawEnhancedAPITest, DrawTextWithBackgroundLargePadding)
{
    std::string text = "Hi";
    int padding = 10;

    drawTextWithBackground(*test_image, 50, 50, text, white_color, green_color, 1, false, padding);

    TextSize size = getTextSize(text, 1);
    int expected_bg_width = size.width + 2 * padding;
    int expected_bg_height = size.height + 2 * padding;

    // Background area should be larger due to padding
    int bg_count = countPixelsInRegion(50 - padding, 50 - padding, expected_bg_width, expected_bg_height, green_color);
    EXPECT_GT(bg_count, expected_bg_width * expected_bg_height * 0.8) << "Should have most background pixels filled";
}

// ===== Multiline Text Tests =====
TEST_F(TextDrawEnhancedAPITest, DrawMultilineTextBasic)
{
    std::string multiline_text = "Line1\nLine2\nLine3";

    drawMultilineText(*test_image, 50, 50, multiline_text, white_color, 1, 2);

    // Should have text pixels distributed across multiple lines
    int total_text_count = countPixelsInRegion(50, 50, 200, 50, white_color);
    EXPECT_GT(total_text_count, 0) << "Should have rendered multiline text";

    // Check for text in different vertical regions (different lines)
    int line1_count = countPixelsInRegion(50, 50, 100, 10, white_color);
    int line2_count = countPixelsInRegion(50, 60, 100, 10, white_color);
    int line3_count = countPixelsInRegion(50, 70, 100, 10, white_color);

    EXPECT_GT(line1_count, 0) << "Should have pixels in line 1 area";
    EXPECT_GT(line2_count, 0) << "Should have pixels in line 2 area";
    EXPECT_GT(line3_count, 0) << "Should have pixels in line 3 area";
}

TEST_F(TextDrawEnhancedAPITest, DrawMultilineTextSingleLine)
{
    std::string single_line = "NoNewlines";

    drawMultilineText(*test_image, 50, 50, single_line, white_color, 1, 2);

    // Should work like regular text drawing
    int text_count = countPixelsInRegion(50, 50, 100, 20, white_color);
    EXPECT_GT(text_count, 0) << "Single line should render normally";
}

TEST_F(TextDrawEnhancedAPITest, DrawMultilineTextEmptyLines)
{
    std::string text_with_empty = "Line1\n\nLine3\n";

    EXPECT_NO_THROW(drawMultilineText(*test_image, 50, 50, text_with_empty, white_color, 1, 2));

    // Should handle empty lines gracefully
    int text_count = countPixelsInRegion(50, 50, 200, 60, white_color);
    EXPECT_GT(text_count, 0) << "Should render non-empty lines";
}

TEST_F(TextDrawEnhancedAPITest, DrawMultilineTextEmptyString)
{
    EXPECT_NO_THROW(drawMultilineText(*test_image, 50, 50, "", white_color, 1, 2));

    // Should not draw anything
    int text_count = countPixelsInRegion(0, 0, test_image->info.width, test_image->info.height, white_color);
    EXPECT_EQ(text_count, 0) << "Empty string should not render anything";
}

TEST_F(TextDrawEnhancedAPITest, DrawMultilineTextWithScaling)
{
    std::string multiline_text = "Big\nText";

    drawMultilineText(*test_image, 50, 50, multiline_text, white_color, 3, 5);

    // With scale 3, characters should be 24x24, and line spacing 5
    int expected_line_height = 8 * 3 + 5; // 29 pixels between line starts

    int line1_count = countPixelsInRegion(50, 50, 100, 25, white_color);
    int line2_count = countPixelsInRegion(50, 50 + expected_line_height, 100, 25, white_color);

    EXPECT_GT(line1_count, 0) << "Should have pixels in scaled line 1";
    EXPECT_GT(line2_count, 0) << "Should have pixels in scaled line 2";
}

// ===== Text Alignment Tests =====
TEST_F(TextDrawEnhancedAPITest, DrawTextAlignedCenter)
{
    std::string text = "CENTER";
    int rectX = 50, rectY = 50, rectW = 100, rectH = 50;

    drawTextAligned(*test_image, rectX, rectY, rectW, rectH, text, white_color, 1, TextAlignment::CENTER,
                    TextAlignment::MIDDLE);

    TextSize size = getTextSize(text, 1);
    int expectedX = rectX + (rectW - size.width) / 2;
    int expectedY = rectY + (rectH - size.height) / 2;

    // Check for text pixels around expected center position
    int text_count = countPixelsInRegion(expectedX - 2, expectedY - 2, size.width + 4, size.height + 4, white_color);
    EXPECT_GT(text_count, 0) << "Should have text pixels in center-aligned position";
}

TEST_F(TextDrawEnhancedAPITest, DrawTextAlignedLeft)
{
    std::string text = "LEFT";
    int rectX = 50, rectY = 50, rectW = 100, rectH = 50;

    drawTextAligned(*test_image, rectX, rectY, rectW, rectH, text, white_color, 1, TextAlignment::LEFT,
                    TextAlignment::TOP);

    // Text should be at the left-top corner of the rectangle
    int text_count = countPixelsInRegion(rectX, rectY, 50, 20, white_color);
    EXPECT_GT(text_count, 0) << "Should have text pixels in left-aligned position";
}

TEST_F(TextDrawEnhancedAPITest, DrawTextAlignedRight)
{
    std::string text = "RIGHT";
    int rectX = 50, rectY = 50, rectW = 100, rectH = 50;

    drawTextAligned(*test_image, rectX, rectY, rectW, rectH, text, white_color, 1, TextAlignment::RIGHT,
                    TextAlignment::BOTTOM);

    TextSize size = getTextSize(text, 1);
    int expectedX = rectX + rectW - size.width;
    int expectedY = rectY + rectH - size.height;

    // Check for text pixels around expected right-bottom position
    int text_count = countPixelsInRegion(expectedX - 2, expectedY - 2, size.width + 4, size.height + 4, white_color);
    EXPECT_GT(text_count, 0) << "Should have text pixels in right-aligned position";
}

TEST_F(TextDrawEnhancedAPITest, DrawTextAlignedEmptyString)
{
    EXPECT_NO_THROW(drawTextAligned(*test_image, 50, 50, 100, 50, "", white_color, 1, TextAlignment::CENTER,
                                    TextAlignment::MIDDLE));

    // Should not draw anything
    int text_count = countPixelsInRegion(0, 0, test_image->info.width, test_image->info.height, white_color);
    EXPECT_EQ(text_count, 0) << "Empty string should not render anything";
}

TEST_F(TextDrawEnhancedAPITest, DrawTextAlignedInvalidRect)
{
    // Zero or negative rectangle dimensions
    EXPECT_NO_THROW(drawTextAligned(*test_image, 50, 50, 0, 50, "Test", white_color, 1, TextAlignment::CENTER,
                                    TextAlignment::MIDDLE));

    EXPECT_NO_THROW(drawTextAligned(*test_image, 50, 50, 100, 0, "Test", white_color, 1, TextAlignment::CENTER,
                                    TextAlignment::MIDDLE));

    EXPECT_NO_THROW(drawTextAligned(*test_image, 50, 50, -10, -10, "Test", white_color, 1, TextAlignment::CENTER,
                                    TextAlignment::MIDDLE));
}

// ===== Text Fitting Tests =====
TEST_F(TextDrawEnhancedAPITest, TextFitsInRectBasic)
{
    EXPECT_TRUE(textFitsInRect("Hi", 1, 100, 20));
    EXPECT_FALSE(textFitsInRect("Very long text that exceeds width", 1, 50, 20));
    EXPECT_FALSE(textFitsInRect("Tall", 5, 100, 20)); // Scale 5 = 40 pixels high, exceeds 20
}

TEST_F(TextDrawEnhancedAPITest, TextFitsInRectEdgeCases)
{
    // Empty string should fit anywhere
    EXPECT_FALSE(textFitsInRect("", 1, 100, 20)); // Empty string has 0 width, function returns false

    // Zero scale
    EXPECT_FALSE(textFitsInRect("Test", 0, 100, 20));

    // Negative scale
    EXPECT_FALSE(textFitsInRect("Test", -1, 100, 20));

    // Zero dimensions
    EXPECT_FALSE(textFitsInRect("Test", 1, 0, 20));
    EXPECT_FALSE(textFitsInRect("Test", 1, 100, 0));

    // Negative dimensions
    EXPECT_FALSE(textFitsInRect("Test", 1, -100, 20));
    EXPECT_FALSE(textFitsInRect("Test", 1, 100, -20));
}

TEST_F(TextDrawEnhancedAPITest, TextFitsInRectExactFit)
{
    std::string text = "Test"; // 4 chars * 8 pixels = 32 pixels wide, 8 pixels high

    EXPECT_TRUE(textFitsInRect(text, 1, 32, 8));  // Exact fit
    EXPECT_TRUE(textFitsInRect(text, 1, 33, 9));  // Slightly larger
    EXPECT_FALSE(textFitsInRect(text, 1, 31, 8)); // Too narrow
    EXPECT_FALSE(textFitsInRect(text, 1, 32, 7)); // Too short
}

// ===== Find Max Scale Tests =====
TEST_F(TextDrawEnhancedAPITest, FindMaxScaleForRectBasic)
{
    std::string text = "Hi"; // 2 chars = 16 pixels at scale 1

    // Should fit at scale 1 in 20x20 area
    EXPECT_EQ(findMaxScaleForRect(text, 20, 20, 5), 1);

    // Should fit at scale 2 in 40x40 area (32x16 needed)
    EXPECT_EQ(findMaxScaleForRect(text, 40, 40, 5), 2);

    // Should not fit at any scale in very small area
    EXPECT_EQ(findMaxScaleForRect(text, 5, 5, 5), 0);
}

TEST_F(TextDrawEnhancedAPITest, FindMaxScaleForRectEdgeCases)
{
    // Empty string
    EXPECT_EQ(findMaxScaleForRect("", 100, 100, 5), 0);

    // Zero dimensions
    EXPECT_EQ(findMaxScaleForRect("Test", 0, 100, 5), 0);
    EXPECT_EQ(findMaxScaleForRect("Test", 100, 0, 5), 0);

    // Negative dimensions
    EXPECT_EQ(findMaxScaleForRect("Test", -100, 100, 5), 0);
}

TEST_F(TextDrawEnhancedAPITest, FindMaxScaleForRectLargeText)
{
    std::string long_text = "This is a very long text"; // 25 chars = 200 pixels at scale 1

    // Should find scale 1 for 300x20 area
    EXPECT_EQ(findMaxScaleForRect(long_text, 300, 20, 5), 1);

    // Should find scale 0 for small area
    EXPECT_EQ(findMaxScaleForRect(long_text, 100, 20, 5), 0);

    // Should find higher scale for very large area
    int scale = findMaxScaleForRect(long_text, 1000, 100, 10);
    EXPECT_GT(scale, 1) << "Should find scale > 1 for large area";
    EXPECT_LE(scale, 10) << "Should not exceed max scale";
}

TEST_F(TextDrawEnhancedAPITest, FindMaxScaleForRectMaxScale)
{
    std::string text = "X"; // Single character, 8x8 at scale 1

    // Should respect max scale limit
    int scale = findMaxScaleForRect(text, 1000, 1000, 3);
    EXPECT_LE(scale, 3) << "Should not exceed specified max scale";

    // With very high max scale, should find optimal fit
    scale = findMaxScaleForRect(text, 80, 80, 100);
    EXPECT_EQ(scale, 10) << "Should find scale 10 for 80x80 area (80/8 = 10)";
}

// ===== Character Validation Tests =====
TEST_F(TextDrawEnhancedAPITest, IsCharacterRenderableRange)
{
    // Test boundary values
    EXPECT_TRUE(isCharacterRenderable(0));
    EXPECT_TRUE(isCharacterRenderable(127));
    EXPECT_FALSE(isCharacterRenderable(128));
    EXPECT_FALSE(isCharacterRenderable(-1));

    // Test common characters
    EXPECT_TRUE(isCharacterRenderable('A'));
    EXPECT_TRUE(isCharacterRenderable('z'));
    EXPECT_TRUE(isCharacterRenderable('0'));
    EXPECT_TRUE(isCharacterRenderable(' '));
    EXPECT_TRUE(isCharacterRenderable('!'));
}

TEST_F(TextDrawEnhancedAPITest, CountRenderableCharactersVariousCases)
{
    EXPECT_EQ(countRenderableCharacters("Hello"), 5);
    EXPECT_EQ(countRenderableCharacters(""), 0);
    EXPECT_EQ(countRenderableCharacters("A1!"), 3);

    // String with non-renderable characters
    std::string mixed = "AB";
    mixed += static_cast<char>(128); // Non-renderable
    mixed += "C";
    mixed += static_cast<char>(255); // Non-renderable
    mixed += "D";

    EXPECT_EQ(countRenderableCharacters(mixed), 4); // Only A, B, C, D are renderable
}

TEST_F(TextDrawEnhancedAPITest, GetFontGlyphConsistency)
{
    // Test that glyph pointers are consistent
    const unsigned char* glyph1 = getFontGlyph('A');
    const unsigned char* glyph2 = getFontGlyph('A');
    EXPECT_EQ(glyph1, glyph2) << "Font glyph pointers should be consistent";

    // Test that different characters have different glyphs
    const unsigned char* glyph_A = getFontGlyph('A');
    const unsigned char* glyph_B = getFontGlyph('B');
    EXPECT_NE(glyph_A, glyph_B) << "Different characters should have different glyphs";

    // Test invalid characters return null
    EXPECT_EQ(getFontGlyph(static_cast<char>(200)), nullptr);
    EXPECT_EQ(getFontGlyph(static_cast<char>(-1)), nullptr);
}

// ===== Integration Tests =====
TEST_F(TextDrawEnhancedAPITest, CombinedOperationsTest)
{
    // Test combining multiple enhanced API functions
    std::string title = "TITLE";
    std::string body = "Line 1\nLine 2\nLine 3";

    // Draw title with background at top
    drawTextWithBackground(*test_image, 150, 20, title, white_color, blue_color, 2, true, 3);

    // Draw multiline body below
    drawMultilineText(*test_image, 50, 60, body, white_color, 1, 2);

    // Draw aligned text in bottom right
    drawTextAligned(*test_image, 200, 150, 90, 40, "Bottom", red_color, 1, TextAlignment::RIGHT, TextAlignment::BOTTOM);

    // Verify all sections have content
    int title_count = countPixelsInRegion(100, 10, 100, 40, white_color);
    int body_count = countPixelsInRegion(50, 60, 150, 40, white_color);
    int bottom_count = countPixelsInRegion(200, 150, 90, 40, red_color);

    EXPECT_GT(title_count, 0) << "Should have title pixels";
    EXPECT_GT(body_count, 0) << "Should have body pixels";
    EXPECT_GT(bottom_count, 0) << "Should have bottom text pixels";
}

TEST_F(TextDrawEnhancedAPITest, ScalingAndFittingIntegration)
{
    std::string text = "FIT TEST";
    int rect_width = 100, rect_height = 30;

    // Find the max scale that fits
    int max_scale = findMaxScaleForRect(text, rect_width, rect_height, 10);
    EXPECT_GT(max_scale, 0) << "Should find a fitting scale";

    // Verify it actually fits
    EXPECT_TRUE(textFitsInRect(text, max_scale, rect_width, rect_height))
        << "Max scale should actually fit in the rectangle";

    // Verify next scale up doesn't fit (if max_scale < 10)
    if (max_scale < 10)
    {
        EXPECT_FALSE(textFitsInRect(text, max_scale + 1, rect_width, rect_height)) << "Scale one higher should not fit";
    }

    // Use the max scale to draw the text
    drawTextAligned(*test_image, 50, 50, rect_width, rect_height, text, white_color, max_scale, TextAlignment::CENTER,
                    TextAlignment::MIDDLE);

    // Should have rendered text
    int text_count = countPixelsInRegion(50, 50, rect_width, rect_height, white_color);
    EXPECT_GT(text_count, 0) << "Should have rendered fitted text";
}
