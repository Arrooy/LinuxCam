#ifndef IMAGE_TEXT_RENDERER_H
#define IMAGE_TEXT_RENDERER_H

#include <string>
#include <vector>
#include "LinuxFace/Image/image.h"

namespace linuxface {

struct TextSize {
    int width;
    int height;
};

enum class TextAlignment {
    LEFT,
    CENTER,
    RIGHT,
    TOP,
    MIDDLE,
    BOTTOM
};

enum class TextWrapMode {
    NONE,           // Force single line (truncate if needed)
    AUTO_WIDTH,     // Wrap based on specified width
    AUTO_CANVAS     // Size canvas to fit text naturally
};

/**
 * Configuration structure for text rendering parameters.
 * Encapsulates all styling and layout options for text rendering.
 */
struct TextRenderConfig {
    // Text content and basic styling
    std::string text;
    Pixel textColor = {255, 255, 255, 255};  // White by default
    int scale = 1;
    
    // Background options
    bool useBackground = false;
    Pixel backgroundColor = {0, 0, 0, 255};  // Black by default
    int padding = 2;
    
    // Layout and wrapping
    TextWrapMode wrapMode = TextWrapMode::AUTO_CANVAS;
    int maxWidth = 0;   // For AUTO_WIDTH wrap mode
    int lineSpacing = 2;
    
    // Alignment (only applies when canvas is larger than text)
    TextAlignment horizontalAlign = TextAlignment::LEFT;
    TextAlignment verticalAlign = TextAlignment::TOP;
    
    // Canvas size (0 means auto-size)
    int canvasWidth = 0;
    int canvasHeight = 0;
    
    // Helper constructor for simple text
    TextRenderConfig(const std::string& text, const Pixel& color = {255, 255, 255, 255}, int scale = 1)
        : text(text), textColor(color), scale(scale) {}
        
    // Helper constructor for bounded text with wrapping
    TextRenderConfig(const std::string& text, int maxWidth, const Pixel& color = {255, 255, 255, 255}, int scale = 1)
        : text(text), textColor(color), scale(scale), wrapMode(TextWrapMode::AUTO_WIDTH), maxWidth(maxWidth) {}
};

/**
 * Text rendering utility class providing bitmap font rendering capabilities.
 * 
 * This class encapsulates font data and rendering logic, providing a clean
 * interface for text rendering operations on Image objects.
 */
class TextRenderer {
public:
    // Main comprehensive rendering method
    static std::shared_ptr<Image> renderText(const TextRenderConfig& config);
    
    // Legacy methods for backwards compatibility
    static void drawText(Image& img, int x, int y, const std::string& text, 
                        const Pixel& color, int scale = 1, bool center = false);
    
    static void drawChar(Image& img, int x, int y, char c, const Pixel& color, int scale = 1);
    
    static void drawTextWithBackground(Image& img, int x, int y, const std::string& text,
                                     const Pixel& textColor, const Pixel& bgColor,
                                     int scale = 1, bool center = false, int padding = 2);
    
    static void drawMultilineText(Image& img, int x, int y, const std::string& text,
                                const Pixel& color, int scale = 1, int lineSpacing = 2);
    
    static void drawTextAligned(Image& img, int rectX, int rectY, int rectWidth, int rectHeight,
                              const std::string& text, const Pixel& color, int scale = 1,
                              TextAlignment hAlign = TextAlignment::CENTER,
                              TextAlignment vAlign = TextAlignment::MIDDLE);
    
    // Utility methods
    static TextSize getTextSize(const std::string& text, int scale = 1);
    static TextSize getMultilineTextSize(const std::string& text, int scale = 1, int lineSpacing = 2);
    static TextSize calculateWrappedTextSize(const std::string& text, int maxWidth, int scale = 1, int lineSpacing = 2);
    static std::vector<std::string> wrapText(const std::string& text, int maxWidth, int scale = 1);
    static bool textFitsInRect(const std::string& text, int scale, int maxWidth, int maxHeight);
    static int findMaxScaleForRect(const std::string& text, int maxWidth, int maxHeight, int maxScale = 10);
    
    // Validation methods
    static bool isCharacterRenderable(char c);
    static size_t countRenderableCharacters(const std::string& text);

private:
    // Font data and internal methods
    static const unsigned char* getFontGlyph(char c);
    static void renderGlyph(Image& img, int x, int y, const unsigned char* glyph, 
                           const Pixel& color, int scale);
    
    // Font data - encapsulated within the class
    static const unsigned char FONT_8X8_BASIC[128][8];
    
    // Constants
    static constexpr int CHAR_WIDTH = 8;
    static constexpr int CHAR_HEIGHT = 8;
    static constexpr int MIN_PRINTABLE_CHAR = 32;
    static constexpr int MAX_PRINTABLE_CHAR = 126;
};

} // namespace linuxface

#endif // IMAGE_TEXT_RENDERER_H
