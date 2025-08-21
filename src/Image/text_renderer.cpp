#include "LinuxFace/Image/text_renderer.h"

#include <sstream>

namespace linuxface
{

// Font data definition - moved to implementation
const unsigned char TextRenderer::FONT_8X8_BASIC[128][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0000 (nul)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0001
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0002
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0003
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0004
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0005
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0006
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0007
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0008
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0009
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000A
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000B
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000C
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000D
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000E
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000F
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0010
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0011
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0012
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0013
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0014
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0015
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0016
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0017
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0018
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0019
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001A
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001B
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001C
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001D
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001E
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001F
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0020 (space)
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // U+0021 (!)
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0022 (")
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // U+0023 (#)
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // U+0024 ($)
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // U+0025 (%)
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // U+0026 (&)
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0027 (')
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // U+0028 (()
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // U+0029 ())
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // U+002A (*)
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // U+002B (+)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // U+002C (,)
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // U+002D (-)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // U+002E (.)
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // U+002F (/)
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // U+0030 (0)
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // U+0031 (1)
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // U+0032 (2)
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // U+0033 (3)
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // U+0034 (4)
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // U+0035 (5)
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // U+0036 (6)
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // U+0037 (7)
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // U+0038 (8)
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // U+0039 (9)
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // U+003A (:)
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // U+003B (;)
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // U+003C (<)
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // U+003D (=)
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // U+003E (>)
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // U+003F (?)
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // U+0040 (@)
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // U+0041 (A)
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, // U+0042 (B)
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, // U+0043 (C)
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, // U+0044 (D)
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, // U+0045 (E)
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, // U+0046 (F)
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, // U+0047 (G)
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // U+0048 (H)
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0049 (I)
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // U+004A (J)
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // U+004B (K)
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, // U+004C (L)
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, // U+004D (M)
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // U+004E (N)
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // U+004F (O)
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, // U+0050 (P)
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // U+0051 (Q)
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, // U+0052 (R)
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // U+0053 (S)
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0054 (T)
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, // U+0055 (U)
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // U+0056 (V)
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // U+0057 (W)
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // U+0058 (X)
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // U+0059 (Y)
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // U+005A (Z)
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // U+005B ([)
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // U+005C (\)
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // U+005D (])
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // U+005E (^)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // U+005F (_)
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0060 (`)
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // U+0061 (a)
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // U+0062 (b)
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // U+0063 (c)
    {0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00}, // U+0064 (d)
    {0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00}, // U+0065 (e)
    {0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00}, // U+0066 (f)
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // U+0067 (g)
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // U+0068 (h)
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0069 (i)
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, // U+006A (j)
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // U+006B (k)
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+006C (l)
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // U+006D (m)
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // U+006E (n)
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // U+006F (o)
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // U+0070 (p)
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // U+0071 (q)
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // U+0072 (r)
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // U+0073 (s)
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // U+0074 (t)
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // U+0075 (u)
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // U+0076 (v)
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // U+0077 (w)
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // U+0078 (x)
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // U+0079 (y)
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // U+007A (z)
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // U+007B ({)
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // U+007C (|)
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // U+007D (})
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+007E (~)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // U+007F
};

// Core rendering methods
void TextRenderer::drawText(Image& img, int x, int y, const std::string& text, const Pixel& color, int scale,
                            bool center)
{
    if (scale <= 0)
    {
        return;
    }

    int drawX = x;
    int drawY = y;

    if (center)
    {
        const TextSize ts = getTextSize(text, scale);
        drawX = x - ts.width / 2;
        drawY = y - ts.height / 2;
    }

    int cursorX = drawX;
    for (const char c : text)
    {
        drawChar(img, cursorX, drawY, c, color, scale);
        cursorX += CHAR_WIDTH * scale;
    }
}

void TextRenderer::drawChar(Image& img, int x, int y, char c, const Pixel& color, int scale)
{
    if (!isCharacterRenderable(c) || scale <= 0)
    {
        return;
    }

    const unsigned char* glyph = getFontGlyph(c);
    if (glyph != nullptr)
    {
        renderGlyph(img, x, y, glyph, color, scale);
    }
}

void TextRenderer::drawTextWithBackground(Image& img, int x, int y, const std::string& text, const Pixel& textColor,
                                          const Pixel& bgColor, int scale, bool center, int padding)
{
    if (scale <= 0 || text.empty())
    {
        return;
    }

    const TextSize ts = getTextSize(text, scale);
    int drawX = x;
    int drawY = y;

    if (center)
    {
        drawX = x - ts.width / 2;
        drawY = y - ts.height / 2;
    }

    // Draw background rectangle with padding
    const int bgX = drawX - padding;
    const int bgY = drawY - padding;
    const int bgWidth = ts.width + 2 * padding;
    const int bgHeight = ts.height + 2 * padding;

    img.fillRect(bgX, bgY, bgWidth, bgHeight, bgColor);
    drawText(img, drawX, drawY, text, textColor, scale, false);
}

void TextRenderer::drawMultilineText(Image& img, int x, int y, const std::string& text, const Pixel& color, int scale,
                                     int lineSpacing)
{
    if (scale <= 0 || text.empty())
    {
        return;
    }

    std::vector<std::string> lines;
    std::stringstream ss(text);
    std::string line;

    while (std::getline(ss, line, '\n'))
    {
        lines.push_back(line);
    }

    int currentY = y;
    int lineHeight = CHAR_HEIGHT * scale + lineSpacing;

    for (const auto& textLine : lines)
    {
        drawText(img, x, currentY, textLine, color, scale, false);
        currentY += lineHeight;
    }
}

void TextRenderer::drawTextAligned(Image& img, int rectX, int rectY, int rectWidth, int rectHeight,
                                   const std::string& text, const Pixel& color, int scale, TextAlignment hAlign,
                                   TextAlignment vAlign)
{
    if (scale <= 0 || text.empty() || rectWidth <= 0 || rectHeight <= 0)
    {
        return;
    }

    const TextSize ts = getTextSize(text, scale);

    int textX = rectX;
    switch (hAlign)
    {
        case TextAlignment::LEFT:
            textX = rectX;
            break;
        case TextAlignment::CENTER:
            textX = rectX + (rectWidth - ts.width) / 2;
            break;
        case TextAlignment::RIGHT:
            textX = rectX + rectWidth - ts.width;
            break;
        default:
            textX = rectX;
            break;
    }

    int textY = rectY;
    switch (vAlign)
    {
        case TextAlignment::TOP:
            textY = rectY;
            break;
        case TextAlignment::MIDDLE:
            textY = rectY + (rectHeight - ts.height) / 2;
            break;
        case TextAlignment::BOTTOM:
            textY = rectY + rectHeight - ts.height;
            break;
        default:
            textY = rectY;
            break;
    }

    drawText(img, textX, textY, text, color, scale, false);
}

// Utility methods
TextSize TextRenderer::getTextSize(const std::string& text, int scale)
{
    return {static_cast<int>(text.size()) * CHAR_WIDTH * scale, CHAR_HEIGHT * scale};
}

TextSize TextRenderer::getMultilineTextSize(const std::string& text, int scale, int lineSpacing)
{
    if (text.empty() || scale <= 0)
    {
        return {0, 0};
    }

    std::vector<std::string> lines;
    std::stringstream ss(text);
    std::string line;

    while (std::getline(ss, line, '\n'))
    {
        lines.push_back(line);
    }

    if (lines.empty())
    {
        return {0, 0};
    }

    size_t maxLineLength = 0;
    for (const auto& textLine : lines)
    {
        maxLineLength = std::max(maxLineLength, textLine.length());
    }

    int lineHeight = CHAR_HEIGHT * scale + lineSpacing;
    int width = static_cast<int>(maxLineLength) * CHAR_WIDTH * scale;
    const int height = static_cast<int>(lines.size()) * lineHeight - lineSpacing;

    return {width, height};
}

bool TextRenderer::textFitsInRect(const std::string& text, int scale, int maxWidth, int maxHeight)
{
    if (text.empty() || scale <= 0 || maxWidth <= 0 || maxHeight <= 0)
    {
        return false;
    }

    const TextSize ts = getTextSize(text, scale);
    return ts.width <= maxWidth && ts.height <= maxHeight;
}

int TextRenderer::findMaxScaleForRect(const std::string& text, int maxWidth, int maxHeight, int maxScale)
{
    if (text.empty() || maxWidth <= 0 || maxHeight <= 0)
    {
        return 0;
    }

    for (int scale = maxScale; scale >= 1; scale--)
    {
        if (textFitsInRect(text, scale, maxWidth, maxHeight))
        {
            return scale;
        }
    }
    return 0;
}

bool TextRenderer::isCharacterRenderable(char c)
{
    return c >= 0 && c <= 127;
}

size_t TextRenderer::countRenderableCharacters(const std::string& text)
{
    size_t count = 0;
    for (const char c : text)
    {
        if (isCharacterRenderable(c))
        {
            count++;
        }
    }
    return count;
}

// New comprehensive text rendering method
std::shared_ptr<Image> TextRenderer::renderText(const TextRenderConfig& config)
{
    if (config.text.empty())
    {
        return nullptr;
    }

    // Determine the text lines to render based on wrap mode
    std::vector<std::string> lines;
    TextSize textSize{};

    switch (config.wrapMode)
    {
        case TextWrapMode::NONE:
        {
            // Force single line, truncate if needed
            lines.push_back(config.text);
            if (config.maxWidth > 0)
            {
                // Truncate text if it exceeds maxWidth
                const TextSize singleLineSize = getTextSize(config.text, config.scale);
                if (singleLineSize.width > config.maxWidth)
                {
                    // Find the maximum number of characters that fit
                    std::string truncated;
                    for (const char c : config.text)
                    {
                        const std::string test = truncated + c;
                        if (getTextSize(test, config.scale).width > config.maxWidth)
                        {
                            break;
                        }
                        truncated += c;
                    }
                    lines[0] = truncated;
                }
            }
            textSize = getTextSize(lines[0], config.scale);
            break;
        }
        case TextWrapMode::AUTO_WIDTH:
        {
            // Wrap text based on specified width
            if (config.maxWidth <= 0)
            {
                // Fallback to single line if no width specified
                lines.push_back(config.text);
                textSize = getTextSize(config.text, config.scale);
            }
            else
            {
                lines = wrapText(config.text, config.maxWidth, config.scale);
                textSize = calculateWrappedTextSize(config.text, config.maxWidth, config.scale, config.lineSpacing);
            }
            break;
        }
        case TextWrapMode::AUTO_CANVAS:
        default:
        {
            // Check if text contains manual line breaks
            std::stringstream ss(config.text);
            std::string line;
            while (std::getline(ss, line, '\n'))
            {
                lines.push_back(line);
            }
            textSize = getMultilineTextSize(config.text, config.scale, config.lineSpacing);
            break;
        }
    }

    // Determine canvas size
    int canvasWidth = config.canvasWidth;
    int canvasHeight = config.canvasHeight;

    if (canvasWidth <= 0 || canvasHeight <= 0)
    {
        // Auto-size canvas
        const int padding = config.useBackground ? config.padding : 0;

        if (config.wrapMode == TextWrapMode::AUTO_WIDTH && config.maxWidth > 0)
        {
            // For text wrapping, respect the maxWidth constraint
            canvasWidth = canvasWidth > 0 ? canvasWidth : config.maxWidth + 2 * padding;
        }
        else
        {
            // For other modes, size to fit text
            canvasWidth = canvasWidth > 0 ? canvasWidth : textSize.width + 2 * padding;
        }

        canvasHeight = canvasHeight > 0 ? canvasHeight : textSize.height + 2 * padding;
    }

    // Create image with transparent background
    const Pixel transparent = {0, 0, 0, 0};
    auto image = std::make_shared<Image>(transparent, canvasWidth, canvasHeight);
    if (!image)
    {
        return nullptr;
    }

    // Calculate text position based on alignment
    int textStartX = 0;
    int textStartY = 0;

    switch (config.horizontalAlign)
    {
        case TextAlignment::LEFT:
            textStartX = config.useBackground ? config.padding : 0;
            break;
        case TextAlignment::CENTER:
            textStartX = (canvasWidth - textSize.width) / 2;
            break;
        case TextAlignment::RIGHT:
            textStartX = canvasWidth - textSize.width - (config.useBackground ? config.padding : 0);
            break;
        default:
            textStartX = 0;
            break;
    }

    switch (config.verticalAlign)
    {
        case TextAlignment::TOP:
            textStartY = config.useBackground ? config.padding : 0;
            break;
        case TextAlignment::MIDDLE:
            textStartY = (canvasHeight - textSize.height) / 2;
            break;
        case TextAlignment::BOTTOM:
            textStartY = canvasHeight - textSize.height - (config.useBackground ? config.padding : 0);
            break;
        default:
            textStartY = 0;
            break;
    }

    // Draw background if requested
    if (config.useBackground)
    {
        int bgX = textStartX - config.padding;
        int bgY = textStartY - config.padding;
        int bgWidth = textSize.width + 2 * config.padding;
        int bgHeight = textSize.height + 2 * config.padding;

        // Clamp background to canvas bounds
        bgX = std::max(0, bgX);
        bgY = std::max(0, bgY);
        bgWidth = std::min(bgWidth, canvasWidth - bgX);
        bgHeight = std::min(bgHeight, canvasHeight - bgY);

        image->fillRect(bgX, bgY, bgWidth, bgHeight, config.backgroundColor);
    }

    // Render each line of text
    int currentY = textStartY;
    int lineHeight = CHAR_HEIGHT * config.scale + config.lineSpacing;

    for (const auto& line : lines)
    {
        if (!line.empty())
        {
            int lineX = textStartX;
            // For multi-line text, each line can be aligned individually
            if (lines.size() > 1 && config.horizontalAlign == TextAlignment::CENTER)
            {
                const TextSize lineSize = getTextSize(line, config.scale);
                lineX = (canvasWidth - lineSize.width) / 2;
            }
            else if (lines.size() > 1 && config.horizontalAlign == TextAlignment::RIGHT)
            {
                const TextSize lineSize = getTextSize(line, config.scale);
                lineX = canvasWidth - lineSize.width - (config.useBackground ? config.padding : 0);
            }

            drawText(*image, lineX, currentY, line, config.textColor, config.scale, false);
        }
        currentY += lineHeight;
    }

    // Set image metadata
    image->info.width = canvasWidth;
    image->info.height = canvasHeight;
    image->info.format = ImageFormat::RGBA;
    image->info.filename = "text_" + config.text.substr(0, std::min(config.text.length(), static_cast<size_t>(20)));

    return image;
}

// New utility functions
std::vector<std::string> TextRenderer::wrapText(const std::string& text, int maxWidth, int scale)
{
    std::vector<std::string> lines;
    if (maxWidth <= 0 || scale <= 0)
    {
        lines.push_back(text);
        return lines;
    }

    std::stringstream ss(text);
    std::string word;
    std::string currentLine;

    while (ss >> word)
    {
        const std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
        const TextSize testSize = getTextSize(testLine, scale);

        if (testSize.width <= maxWidth)
        {
            currentLine = testLine;
        }
        else
        {
            if (!currentLine.empty())
            {
                lines.push_back(currentLine);
                currentLine = word;
            }
            else
            {
                // Single word is too long, split it
                for (const char c : word)
                {
                    const std::string testChar = currentLine + c;
                    if (getTextSize(testChar, scale).width <= maxWidth)
                    {
                        currentLine += c;
                    }
                    else
                    {
                        if (!currentLine.empty())
                        {
                            lines.push_back(currentLine);
                        }
                        currentLine = c;
                    }
                }
            }
        }
    }

    if (!currentLine.empty())
    {
        lines.push_back(currentLine);
    }

    if (lines.empty())
    {
        lines.emplace_back("");
    }

    return lines;
}

TextSize TextRenderer::calculateWrappedTextSize(const std::string& text, int maxWidth, int scale, int lineSpacing)
{
    auto lines = wrapText(text, maxWidth, scale);

    int totalWidth = 0;
    int totalHeight = 0;

    for (const auto& line : lines)
    {
        const TextSize lineSize = getTextSize(line, scale);
        totalWidth = std::max(totalWidth, lineSize.width);
        totalHeight += CHAR_HEIGHT * scale;
        if (totalHeight > CHAR_HEIGHT * scale)
        { // Add spacing between lines
            totalHeight += lineSpacing;
        }
    }

    return {totalWidth, totalHeight};
}

// Private methods
const unsigned char* TextRenderer::getFontGlyph(char c)
{
    if (!isCharacterRenderable(c))
    {
        return nullptr;
    }
    return FONT_8X8_BASIC[static_cast<int>(c)];
}

void TextRenderer::renderGlyph(Image& img, int x, int y, const unsigned char* glyph, const Pixel& color, int scale)
{
    for (int row = 0; row < CHAR_HEIGHT; row++)
    {
        const uint8_t bits = glyph[row];
        for (int col = 0; col < CHAR_WIDTH; col++)
        {
            if ((bits & (1 << col)) != 0)
            {
                img.fillRect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

} // namespace linuxface
