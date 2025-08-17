/**
 * Core Text Drawing Tests
 *
 * Tests for basic text drawing functionality:
 * - Basic character rendering
 * - Text size calculations
 * - Simple text drawing
 * - Color and scaling operations
 * - Font bitmap access
 */

#include <gtest/gtest.h>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/text_draw.h"

using namespace linuxface;

class TextDrawCoreTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create a standard test image
        test_image = std::make_unique<Image>(Pixel(0, 0, 0), 200, 100);
        white_color = Pixel(255, 255, 255);
        red_color = Pixel(255, 0, 0);
        blue_color = Pixel(0, 0, 255);
    }

    std::unique_ptr<Image> test_image;
    Pixel white_color;
    Pixel red_color;
    Pixel blue_color;
};

// ===== Basic Text Size Tests =====
TEST_F(TextDrawCoreTest, GetTextSizeEmptyString)
{
    TextSize size = getTextSize("", 1);
    EXPECT_EQ(size.width, 0);
    EXPECT_EQ(size.height, 8);
}

TEST_F(TextDrawCoreTest, GetTextSizeSingleCharacter)
{
    TextSize size = getTextSize("A", 1);
    EXPECT_EQ(size.width, 8);
    EXPECT_EQ(size.height, 8);
}

TEST_F(TextDrawCoreTest, GetTextSizeMultipleCharacters)
{
    TextSize size = getTextSize("Hello", 1);
    EXPECT_EQ(size.width, 40); // 5 chars * 8 pixels each
    EXPECT_EQ(size.height, 8);
}

TEST_F(TextDrawCoreTest, GetTextSizeWithScaling)
{
    TextSize size1 = getTextSize("AB", 1);
    TextSize size2 = getTextSize("AB", 2);
    TextSize size3 = getTextSize("AB", 3);

    EXPECT_EQ(size1.width, 16); // 2 chars * 8 pixels
    EXPECT_EQ(size1.height, 8);

    EXPECT_EQ(size2.width, 32);  // 2 chars * 8 pixels * 2
    EXPECT_EQ(size2.height, 16); // 8 pixels * 2

    EXPECT_EQ(size3.width, 48);  // 2 chars * 8 pixels * 3
    EXPECT_EQ(size3.height, 24); // 8 pixels * 3
}

TEST_F(TextDrawCoreTest, GetTextSizeZeroScale)
{
    TextSize size = getTextSize("Test", 0);
    EXPECT_EQ(size.width, 0);
    EXPECT_EQ(size.height, 0);
}

TEST_F(TextDrawCoreTest, GetTextSizeNegativeScale)
{
    TextSize size = getTextSize("Test", -1);
    EXPECT_EQ(size.width, -32); // Negative scale produces negative size
    EXPECT_EQ(size.height, -8);
}

// ===== Fill Block Tests =====
TEST_F(TextDrawCoreTest, FillBlockBasic)
{
    // Fill a 2x2 block at position (10, 10)
    fillBlockWithDDA(*test_image, 10, 10, 2, white_color);

    // Check the filled pixels
    EXPECT_EQ((*test_image)(10, 10), white_color);
    EXPECT_EQ((*test_image)(11, 10), white_color);
    EXPECT_EQ((*test_image)(10, 11), white_color);
    EXPECT_EQ((*test_image)(11, 11), white_color);

    // Check surrounding pixels remain unchanged
    EXPECT_EQ((*test_image)(9, 10), Pixel(0, 0, 0));
    EXPECT_EQ((*test_image)(12, 10), Pixel(0, 0, 0));
}

TEST_F(TextDrawCoreTest, FillBlockSinglePixel)
{
    fillBlockWithDDA(*test_image, 50, 50, 1, red_color);

    EXPECT_EQ((*test_image)(50, 50), red_color);
    // Check neighbors are unchanged
    EXPECT_EQ((*test_image)(49, 50), Pixel(0, 0, 0));
    EXPECT_EQ((*test_image)(51, 50), Pixel(0, 0, 0));
}

TEST_F(TextDrawCoreTest, FillBlockLargeSize)
{
    fillBlockWithDDA(*test_image, 5, 5, 10, blue_color);

    // Check corners and center
    EXPECT_EQ((*test_image)(5, 5), blue_color);
    EXPECT_EQ((*test_image)(14, 5), blue_color);
    EXPECT_EQ((*test_image)(5, 14), blue_color);
    EXPECT_EQ((*test_image)(14, 14), blue_color);
    EXPECT_EQ((*test_image)(10, 10), blue_color);
}

TEST_F(TextDrawCoreTest, FillBlockZeroSize)
{
    // Zero size should not crash and not affect the image
    fillBlockWithDDA(*test_image, 10, 10, 0, white_color);

    // No pixels should be changed
    EXPECT_EQ((*test_image)(10, 10), Pixel(0, 0, 0));
}

// ===== Character Drawing Tests =====
TEST_F(TextDrawCoreTest, DrawCharBasic)
{
    // Draw character 'A' (ASCII 65)
    drawCharDDA(*test_image, 10, 10, 'A', white_color, 1);

    // Character 'A' should have some pixels set
    // Check that some pixels in the character area are white
    bool found_white_pixel = false;
    for (int y = 10; y < 18; y++)
    {
        for (int x = 10; x < 18; x++)
        {
            if ((*test_image)(x, y) == white_color)
            {
                found_white_pixel = true;
                break;
            }
        }
        if (found_white_pixel)
        {
            break;
        }
    }
    EXPECT_TRUE(found_white_pixel) << "Character 'A' should render some white pixels";
}

TEST_F(TextDrawCoreTest, DrawCharSpace)
{
    // Draw space character (ASCII 32) - should not draw any pixels
    drawCharDDA(*test_image, 10, 10, ' ', white_color, 1);

    // Space character should not draw any pixels
    bool found_white_pixel = false;
    for (int y = 10; y < 18; y++)
    {
        for (int x = 10; x < 18; x++)
        {
            if ((*test_image)(x, y) == white_color)
            {
                found_white_pixel = true;
                break;
            }
        }
    }
    EXPECT_FALSE(found_white_pixel) << "Space character should not render any pixels";
}

TEST_F(TextDrawCoreTest, DrawCharInvalidCharacter)
{
    // Draw invalid character (outside 0-127 range)
    drawCharDDA(*test_image, 10, 10, static_cast<char>(200), white_color, 1);

    // Should not crash and not draw anything
    bool found_white_pixel = false;
    for (int y = 10; y < 18; y++)
    {
        for (int x = 10; x < 18; x++)
        {
            if ((*test_image)(x, y) == white_color)
            {
                found_white_pixel = true;
                break;
            }
        }
    }
    EXPECT_FALSE(found_white_pixel) << "Invalid character should not render any pixels";
}

TEST_F(TextDrawCoreTest, DrawCharWithScaling)
{
    // Draw 'A' with scale 2
    drawCharDDA(*test_image, 10, 10, 'A', white_color, 2);

    // Scaled character should be larger (16x16 instead of 8x8)
    bool found_white_pixels = false;
    int white_pixel_count = 0;

    for (int y = 10; y < 26; y++) // 16 pixels high
    {
        for (int x = 10; x < 26; x++) // 16 pixels wide
        {
            if ((*test_image)(x, y) == white_color)
            {
                found_white_pixels = true;
                white_pixel_count++;
            }
        }
    }

    EXPECT_TRUE(found_white_pixels) << "Scaled character should render pixels";
    EXPECT_GT(white_pixel_count, 0) << "Should have some white pixels for scaled 'A'";
}

TEST_F(TextDrawCoreTest, DrawCharNegativeCharacter)
{
    // Test negative character value
    drawCharDDA(*test_image, 10, 10, static_cast<char>(-1), white_color, 1);

    // Should not crash and not draw anything
    bool found_white_pixel = false;
    for (int y = 10; y < 18; y++)
    {
        for (int x = 10; x < 18; x++)
        {
            if ((*test_image)(x, y) == white_color)
            {
                found_white_pixel = true;
                break;
            }
        }
    }
    EXPECT_FALSE(found_white_pixel) << "Negative character should not render any pixels";
}

// ===== Basic Text Drawing Tests =====
TEST_F(TextDrawCoreTest, DrawTextSimple)
{
    drawText(*test_image, 10, 10, "Hi", white_color, 1, false);

    // Should have drawn something in the text area
    bool found_pixels = false;
    for (int y = 10; y < 18; y++)
    {
        for (int x = 10; x < 26; x++) // 2 chars * 8 pixels = 16 pixels wide
        {
            if ((*test_image)(x, y) == white_color)
            {
                found_pixels = true;
                break;
            }
        }
        if (found_pixels)
        {
            break;
        }
    }
    EXPECT_TRUE(found_pixels) << "Text 'Hi' should render some pixels";
}

TEST_F(TextDrawCoreTest, DrawTextEmpty)
{
    drawText(*test_image, 10, 10, "", white_color, 1, false);

    // Empty text should not draw anything but should not crash
    bool found_pixels = false;
    for (int y = 0; y < test_image->info.height; y++)
    {
        for (int x = 0; x < test_image->info.width; x++)
        {
            if ((*test_image)(x, y) == white_color)
            {
                found_pixels = true;
                break;
            }
        }
    }
    EXPECT_FALSE(found_pixels) << "Empty text should not render any pixels";
}

TEST_F(TextDrawCoreTest, DrawTextWithCentering)
{
    std::string text = "TEST";
    int centerX = 100, centerY = 50;

    drawText(*test_image, centerX, centerY, text, white_color, 1, true);

    // Text should be centered around the given point
    TextSize size = getTextSize(text, 1);
    int expectedX = centerX - size.width / 2;
    int expectedY = centerY - size.height / 2;

    // Check if pixels exist in the expected centered area
    bool found_pixels = false;
    for (int y = expectedY; y < expectedY + size.height; y++)
    {
        for (int x = expectedX; x < expectedX + size.width; x++)
        {
            if (x >= 0 && x < test_image->info.width && y >= 0 && y < test_image->info.height)
            {
                if ((*test_image)(x, y) == white_color)
                {
                    found_pixels = true;
                    break;
                }
            }
        }
        if (found_pixels)
        {
            break;
        }
    }
    EXPECT_TRUE(found_pixels) << "Centered text should render in expected area";
}

TEST_F(TextDrawCoreTest, DrawTextZeroScale)
{
    drawText(*test_image, 10, 10, "Test", white_color, 0, false);

    // Zero scale should not draw anything and not crash
    bool found_pixels = false;
    for (int y = 0; y < test_image->info.height; y++)
    {
        for (int x = 0; x < test_image->info.width; x++)
        {
            if ((*test_image)(x, y) == white_color)
            {
                found_pixels = true;
                break;
            }
        }
    }
    EXPECT_FALSE(found_pixels) << "Zero scale text should not render any pixels";
}

TEST_F(TextDrawCoreTest, DrawTextNegativeScale)
{
    drawText(*test_image, 10, 10, "Test", white_color, -1, false);

    // Negative scale should not draw anything and not crash
    bool found_pixels = false;
    for (int y = 0; y < test_image->info.height; y++)
    {
        for (int x = 0; x < test_image->info.width; x++)
        {
            if ((*test_image)(x, y) == white_color)
            {
                found_pixels = true;
                break;
            }
        }
    }
    EXPECT_FALSE(found_pixels) << "Negative scale text should not render any pixels";
}

// ===== Font Validation Tests =====
TEST_F(TextDrawCoreTest, IsCharacterRenderableValid)
{
    EXPECT_TRUE(isCharacterRenderable('A'));
    EXPECT_TRUE(isCharacterRenderable('a'));
    EXPECT_TRUE(isCharacterRenderable('0'));
    EXPECT_TRUE(isCharacterRenderable(' '));
    EXPECT_TRUE(isCharacterRenderable('!'));
    EXPECT_TRUE(isCharacterRenderable('~'));
    EXPECT_TRUE(isCharacterRenderable('\0'));
    EXPECT_TRUE(isCharacterRenderable(127));
}

TEST_F(TextDrawCoreTest, IsCharacterRenderableInvalid)
{
    EXPECT_FALSE(isCharacterRenderable(static_cast<char>(128)));
    EXPECT_FALSE(isCharacterRenderable(static_cast<char>(200)));
    EXPECT_FALSE(isCharacterRenderable(static_cast<char>(-1)));
    EXPECT_FALSE(isCharacterRenderable(static_cast<char>(255)));
}

TEST_F(TextDrawCoreTest, CountRenderableCharacters)
{
    EXPECT_EQ(countRenderableCharacters(""), 0);
    EXPECT_EQ(countRenderableCharacters("Hello"), 5);
    EXPECT_EQ(countRenderableCharacters("Test123"), 7);

    // String with some invalid characters
    std::string mixed = "ABC";
    mixed += static_cast<char>(200); // Invalid
    mixed += "DE";
    mixed += static_cast<char>(255); // Invalid
    mixed += "F";

    EXPECT_EQ(countRenderableCharacters(mixed), 6); // Only ABC, DE, F are valid
}

TEST_F(TextDrawCoreTest, GetFontGlyphValid)
{
    const unsigned char* glyph_A = getFontGlyph('A');
    EXPECT_NE(glyph_A, nullptr);

    const unsigned char* glyph_space = getFontGlyph(' ');
    EXPECT_NE(glyph_space, nullptr);

    const unsigned char* glyph_zero = getFontGlyph('0');
    EXPECT_NE(glyph_zero, nullptr);
}

TEST_F(TextDrawCoreTest, GetFontGlyphInvalid)
{
    const unsigned char* glyph_invalid = getFontGlyph(static_cast<char>(200));
    EXPECT_EQ(glyph_invalid, nullptr);

    const unsigned char* glyph_negative = getFontGlyph(static_cast<char>(-1));
    EXPECT_EQ(glyph_negative, nullptr);
}

TEST_F(TextDrawCoreTest, FontGlyphConsistency)
{
    // Verify that font glyph data is consistent
    const unsigned char* glyph_A1 = getFontGlyph('A');
    const unsigned char* glyph_A2 = getFontGlyph('A');
    EXPECT_EQ(glyph_A1, glyph_A2) << "Font glyph pointers should be consistent";

    // Different characters should have different glyph data
    const unsigned char* glyph_A = getFontGlyph('A');
    const unsigned char* glyph_B = getFontGlyph('B');
    EXPECT_NE(glyph_A, glyph_B) << "Different characters should have different glyph data";
}

// ===== All Printable Character Tests =====
TEST_F(TextDrawCoreTest, AllPrintableCharactersRender)
{
    // Test that all printable ASCII characters can be rendered without crashing
    for (char c = 32; c <= 126; c++) // Printable ASCII range
    {
        EXPECT_NO_THROW({ drawCharDDA(*test_image, 10, 10, c, white_color, 1); })
            << "Character '" << c << "' (ASCII " << static_cast<int>(c) << ") should render without throwing";
    }
}

TEST_F(TextDrawCoreTest, AllValidCharactersHaveGlyphs)
{
    // Test that all valid characters have non-null glyph data
    for (int c = 0; c <= 127; c++)
    {
        const unsigned char* glyph = getFontGlyph(static_cast<char>(c));
        EXPECT_NE(glyph, nullptr) << "Character " << c << " should have valid glyph data";
    }
}
