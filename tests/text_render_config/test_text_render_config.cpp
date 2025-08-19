#include <gtest/gtest.h>
#include "LinuxFace/Image/text_renderer.h"
#include "LinuxFace/Image/image.h"

using namespace linuxface;

class TextRenderConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        white = {255, 255, 255, 255};
        black = {0, 0, 0, 255};
        red = {255, 0, 0, 255};
        blue = {0, 0, 255, 255};
    }

    Pixel white, black, red, blue;
};

TEST_F(TextRenderConfigTest, BasicTextRendering) {
    TextRenderConfig config("Hello World", white, 1);
    auto image = TextRenderer::renderText(config);
    
    ASSERT_NE(image, nullptr);
    EXPECT_GT(image->info.width, 0);
    EXPECT_GT(image->info.height, 0);
    EXPECT_EQ(image->info.format, ImageFormat::RGBA);
}

TEST_F(TextRenderConfigTest, TextWithBackground) {
    TextRenderConfig config("Test", white, 2);
    config.useBackground = true;
    config.backgroundColor = blue;
    config.padding = 5;
    
    auto image = TextRenderer::renderText(config);
    
    ASSERT_NE(image, nullptr);
    EXPECT_GT(image->info.width, 0);
    EXPECT_GT(image->info.height, 0);
}

TEST_F(TextRenderConfigTest, ForcesSingleLineMode) {
    TextRenderConfig config("This is a very long text that should be truncated", white, 1);
    config.wrapMode = TextWrapMode::NONE;
    config.maxWidth = 100; // Force truncation
    
    auto image = TextRenderer::renderText(config);
    
    ASSERT_NE(image, nullptr);
    EXPECT_LE(static_cast<int>(image->info.width), 100); // Should be truncated
}

TEST_F(TextRenderConfigTest, TextWrappingMode) {
    TextRenderConfig config("This is a long text that should wrap to multiple lines", white, 1);
    config.wrapMode = TextWrapMode::AUTO_WIDTH;
    config.maxWidth = 150; // Force wrapping
    
    auto image = TextRenderer::renderText(config);
    
    ASSERT_NE(image, nullptr);
    EXPECT_LE(static_cast<int>(image->info.width), 150);
    EXPECT_GT(image->info.height, 8); // Should be taller due to multiple lines
}

TEST_F(TextRenderConfigTest, AutoCanvasMode) {
    TextRenderConfig config("Line 1\nLine 2\nLine 3", white, 1);
    config.wrapMode = TextWrapMode::AUTO_CANVAS;
    
    auto image = TextRenderer::renderText(config);
    
    ASSERT_NE(image, nullptr);
    EXPECT_GT(image->info.height, 24); // Should accommodate 3 lines
}

TEST_F(TextRenderConfigTest, CenterAlignment) {
    TextRenderConfig config("Center", white, 1);
    config.canvasWidth = 200;
    config.canvasHeight = 50;
    config.horizontalAlign = TextAlignment::CENTER;
    config.verticalAlign = TextAlignment::MIDDLE;
    
    auto image = TextRenderer::renderText(config);
    
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->info.width, 200);
    EXPECT_EQ(image->info.height, 50);
}

TEST_F(TextRenderConfigTest, BackgroundWithAlignment) {
    TextRenderConfig config("BG+Align", white, 2);
    config.useBackground = true;
    config.backgroundColor = red;
    config.padding = 3;
    config.canvasWidth = 150;
    config.canvasHeight = 50;
    config.horizontalAlign = TextAlignment::RIGHT;
    config.verticalAlign = TextAlignment::BOTTOM;
    
    auto image = TextRenderer::renderText(config);
    
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->info.width, 150);
    EXPECT_EQ(image->info.height, 50);
}

TEST_F(TextRenderConfigTest, MultilineWithWrapping) {
    TextRenderConfig config("This long text should wrap and also have manual\nline breaks here", white, 1);
    config.wrapMode = TextWrapMode::AUTO_WIDTH;
    config.maxWidth = 100;
    config.useBackground = true;
    config.backgroundColor = black;
    config.padding = 2;
    
    auto image = TextRenderer::renderText(config);
    
    ASSERT_NE(image, nullptr);
    EXPECT_LE(static_cast<int>(image->info.width), 104); // maxWidth + 2*padding
    EXPECT_GT(image->info.height, 20); // Should be tall due to wrapping + manual breaks
}

TEST_F(TextRenderConfigTest, EmptyTextHandling) {
    TextRenderConfig config("", white, 1);
    auto image = TextRenderer::renderText(config);
    
    EXPECT_EQ(image, nullptr); // Should return null for empty text
}

TEST_F(TextRenderConfigTest, LargeScaleWithWrapping) {
    TextRenderConfig config("Big Text Wrapped", white, 5);
    config.wrapMode = TextWrapMode::AUTO_WIDTH;
    config.maxWidth = 200;
    
    auto image = TextRenderer::renderText(config);
    
    ASSERT_NE(image, nullptr);
    EXPECT_LE(static_cast<int>(image->info.width), 200);
    EXPECT_GT(image->info.height, 40); // Large scale should create tall text
}

// Test the helper constructor
TEST_F(TextRenderConfigTest, HelperConstructorForWrapping) {
    TextRenderConfig config("Wrapped text", 100, white, 2);
    
    EXPECT_EQ(config.text, "Wrapped text");
    EXPECT_EQ(config.maxWidth, 100);
    EXPECT_EQ(config.wrapMode, TextWrapMode::AUTO_WIDTH);
    EXPECT_EQ(config.scale, 2);
    
    auto image = TextRenderer::renderText(config);
    ASSERT_NE(image, nullptr);
    EXPECT_LE(static_cast<int>(image->info.width), 100);
}

// Test integration with text wrapping utility functions
TEST_F(TextRenderConfigTest, TextWrappingUtilityIntegration) {
    std::string longText = "This is a very long sentence that should definitely wrap around when rendered with a limited width constraint applied to it.";
    
    auto lines = TextRenderer::wrapText(longText, 150, 1);
    EXPECT_GT(lines.size(), 1); // Should wrap to multiple lines
    
    for (const auto& line : lines) {
        auto lineSize = TextRenderer::getTextSize(line, 1);
        EXPECT_LE(lineSize.width, 150); // Each line should fit within maxWidth
    }
    
    auto totalSize = TextRenderer::calculateWrappedTextSize(longText, 150, 1, 2);
    EXPECT_LE(totalSize.width, 150);
    EXPECT_GT(totalSize.height, 8); // Should be taller than single line
}
