/**
 * Text Drawing Visual Validation Tests
 *
 * Tests that generate visual outputs for manual validation:
 * - Font rendering accuracy
 * - Scaling quality validation
 * - Color rendering verification
 * - Layout and alignment validation
 * - Visual regression testing
 */

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/text_draw.h"
#include "../test_utils.h"

using namespace linuxface;

class TextDrawVisualValidationTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Create test image with neutral background
        test_image = std::make_unique<Image>(Pixel(64, 64, 64), 800, 600);

        // Define color palette
        white_color = Pixel(255, 255, 255);
        black_color = Pixel(0, 0, 0);
        red_color = Pixel(255, 0, 0);
        green_color = Pixel(0, 255, 0);
        blue_color = Pixel(0, 0, 255);
        yellow_color = Pixel(255, 255, 0);
        cyan_color = Pixel(0, 255, 255);
        magenta_color = Pixel(255, 0, 255);

        // Test counter for unique filenames
        test_counter = 0;
    }

    void TearDown() override
    {
        // Optionally save the final test image if SAVE_IMAGES is set
        if (test_image && TestUtils::getEnvVarBool("SAVE_IMAGES"))
        {
            std::string output_dir = createTestOutputDirectory();
            std::string filename = "text_draw_visual_test_final.ppm";

            // Combine with output directory
            if (!output_dir.empty())
            {
                filename = output_dir + "/" + filename;
            }

            test_image->saveToDisk(filename);
        }
    }

    std::unique_ptr<Image> test_image;
    Pixel white_color, black_color, red_color, green_color, blue_color;
    Pixel yellow_color, cyan_color, magenta_color;
    int test_counter;

    // Helper method to create test output directories
    std::string createTestOutputDirectory() const
    {
        std::filesystem::path output_dir = "testing";
        output_dir /= "text_draw";
        output_dir /= "visual_validation";

        try
        {
            std::filesystem::create_directories(output_dir);
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            std::cerr << "Failed to create output directory " << output_dir << ": " << e.what() << std::endl;
            return "";
        }

        return output_dir.string();
    }

    // Helper to save test images with unique names
    void saveTestImage(const std::string& test_name)
    {
        // Only save images if SAVE_IMAGES environment variable is set
        if (!TestUtils::getEnvVarBool("SAVE_IMAGES"))
        {
            return;
        }

        std::string output_dir = createTestOutputDirectory();
        std::string filename = "text_draw_visual_" + test_name + "_" + std::to_string(test_counter++) + ".ppm";

        // Combine with output directory
        if (!output_dir.empty())
        {
            filename = output_dir + "/" + filename;
        }

        test_image->saveToDisk(filename);
        std::cout << "Saved visual test image: " << filename << std::endl;
    }

    // Helper to clear the test image
    void clearImage(const Pixel& bg_color = Pixel(64, 64, 64))
    {
        test_image = std::make_unique<Image>(bg_color, 800, 600);
    }
};

// ===== Font Rendering Tests =====
TEST_F(TextDrawVisualValidationTest, AllPrintableCharactersDisplay)
{
    clearImage(white_color);

    // Display all printable ASCII characters in a grid
    int chars_per_row = 16;
    int char_width = 12;
    int char_height = 16;
    int start_x = 20, start_y = 20;

    for (int c = 32; c <= 126; c++) // Printable ASCII range
    {
        int row = (c - 32) / chars_per_row;
        int col = (c - 32) % chars_per_row;

        int x = start_x + col * char_width;
        int y = start_y + row * char_height;

        drawCharDDA(*test_image, x, y, static_cast<char>(c), black_color, 1);

        // Add character code below each character
        std::string code = std::to_string(c);
        drawText(*test_image, x, y + 10, code, Pixel(128, 128, 128), 1, false);
    }

    // Add title
    drawText(*test_image, 20, 5, "All Printable ASCII Characters (32-126)", black_color, 1, false);

    saveTestImage("all_ascii_characters");

    // Verify we can render all printable characters without crashing
    EXPECT_TRUE(true) << "All printable ASCII characters rendered successfully";
}

TEST_F(TextDrawVisualValidationTest, FontScalingDisplay)
{
    clearImage(white_color);

    std::string test_text = "Scale Test Ag";
    std::vector<int> scales = {1, 2, 3, 4, 5, 8, 10};

    int y_offset = 20;

    for (int scale : scales)
    {
        // Draw the text at different scales
        drawText(*test_image, 20, y_offset, test_text, black_color, scale, false);

        // Add scale label
        std::string label = "Scale " + std::to_string(scale) + ":";
        drawText(*test_image, 450, y_offset, label, red_color, 1, false);

        // Calculate next position
        TextSize size = getTextSize(test_text, scale);
        y_offset += size.height + 10;

        // Break if we're running out of space
        if (y_offset > 550)
        {
            break;
        }
    }

    saveTestImage("font_scaling");

    EXPECT_TRUE(true) << "Font scaling test completed";
}

TEST_F(TextDrawVisualValidationTest, ColorPaletteDisplay)
{
    clearImage(Pixel(32, 32, 32));

    std::vector<std::pair<Pixel, std::string>> colors = {
        {white_color, "White"},
        {red_color, "Red"},
        {green_color, "Green"},
        {blue_color, "Blue"},
        {yellow_color, "Yellow"},
        {cyan_color, "Cyan"},
        {magenta_color, "Magenta"},
        {Pixel(255, 128, 0), "Orange"},
        {Pixel(128, 0, 255), "Purple"},
        {Pixel(128, 128, 128), "Gray"}
    };

    int y_offset = 30;

    for (const auto& color_pair : colors)
    {
        // Draw color name in the color
        drawText(*test_image, 50, y_offset, color_pair.second, color_pair.first, 2, false);

        // Draw with background for visibility
        drawTextWithBackground(*test_image, 300, y_offset, color_pair.second + " BG", white_color, color_pair.first, 2,
                               false, 3);

        y_offset += 40;
    }

    drawText(*test_image, 50, 5, "Color Palette Test", white_color, 2, false);

    saveTestImage("color_palette");

    EXPECT_TRUE(true) << "Color palette test completed";
}

// ===== Enhanced API Visual Tests =====
TEST_F(TextDrawVisualValidationTest, BackgroundTextDisplay)
{
    clearImage(Pixel(100, 150, 200)); // Light blue background

    // Test different background configurations
    drawTextWithBackground(*test_image, 50, 50, "Default Padding", white_color, red_color, 2, false, 2);

    drawTextWithBackground(*test_image, 50, 100, "Large Padding", white_color, green_color, 2, false, 8);

    drawTextWithBackground(*test_image, 50, 150, "No Padding", white_color, blue_color, 2, false, 0);

    // Centered background text
    drawTextWithBackground(*test_image, 400, 200, "Centered", yellow_color, magenta_color, 3, true, 5);

    // Different scales
    drawTextWithBackground(*test_image, 50, 250, "Scale 1", white_color, black_color, 1, false, 3);
    drawTextWithBackground(*test_image, 50, 300, "Scale 4", white_color, black_color, 4, false, 3);

    saveTestImage("background_text");

    EXPECT_TRUE(true) << "Background text test completed";
}

TEST_F(TextDrawVisualValidationTest, MultilineTextDisplay)
{
    clearImage(white_color);

    // Test various multiline configurations
    std::string multiline1 = "Line 1\nLine 2\nLine 3\nLine 4";
    drawMultilineText(*test_image, 50, 50, multiline1, black_color, 1, 2);

    std::string multiline2 = "Larger\nScale\nText";
    drawMultilineText(*test_image, 50, 150, multiline2, red_color, 2, 5);

    std::string multiline3 = "Different\nLine\nSpacing";
    drawMultilineText(*test_image, 300, 50, multiline3, blue_color, 1, 8);

    // Mixed content
    std::string mixed = "Numbers: 123\nSymbols: !@#$%\nMixed: Aa1!";
    drawMultilineText(*test_image, 300, 200, mixed, green_color, 1, 3);

    // Empty lines test
    std::string with_empty = "Line 1\n\nLine 3\n\n\nLine 6";
    drawMultilineText(*test_image, 500, 50, with_empty, magenta_color, 1, 2);

    saveTestImage("multiline_text");

    EXPECT_TRUE(true) << "Multiline text test completed";
}

TEST_F(TextDrawVisualValidationTest, TextAlignmentDisplay)
{
    clearImage(Pixel(240, 240, 240));

    // Draw alignment rectangles for visualization
    int rect_width = 200, rect_height = 80;
    std::string test_text = "Aligned Text";

    // Helper to draw a rectangle outline
    auto drawRect = [&](int x, int y, int w, int h, const Pixel& color)
    {
        // Top and bottom lines
        for (int i = 0; i < w; i++)
        {
            (*test_image)(x + i, y) = color;
            (*test_image)(x + i, y + h - 1) = color;
        }
        // Left and right lines
        for (int i = 0; i < h; i++)
        {
            (*test_image)(x, y + i) = color;
            (*test_image)(x + w - 1, y + i) = color;
        }
    };

    // Test all alignment combinations
    std::vector<std::pair<TextAlignment, std::string>> h_aligns = {
        {TextAlignment::LEFT,   "LEFT"  },
        {TextAlignment::CENTER, "CENTER"},
        {TextAlignment::RIGHT,  "RIGHT" }
    };

    std::vector<std::pair<TextAlignment, std::string>> v_aligns = {
        {TextAlignment::TOP,    "TOP"   },
        {TextAlignment::MIDDLE, "MIDDLE"},
        {TextAlignment::BOTTOM, "BOTTOM"}
    };

    int grid_x = 50, grid_y = 50;
    int cell_width = 220, cell_height = 120;

    for (size_t v = 0; v < v_aligns.size(); v++)
    {
        for (size_t h = 0; h < h_aligns.size(); h++)
        {
            int cell_x = grid_x + h * cell_width;
            int cell_y = grid_y + v * cell_height;

            // Draw rectangle outline
            drawRect(cell_x, cell_y, rect_width, rect_height, Pixel(128, 128, 128));

            // Draw aligned text
            drawTextAligned(*test_image, cell_x, cell_y, rect_width, rect_height, test_text, red_color, 1,
                            h_aligns[h].first, v_aligns[v].first);

            // Add alignment labels
            std::string label = h_aligns[h].second + " " + v_aligns[v].second;
            drawText(*test_image, cell_x, cell_y - 15, label, black_color, 1, false);
        }
    }

    saveTestImage("text_alignment");

    EXPECT_TRUE(true) << "Text alignment test completed";
}

// ===== Complex Layout Tests =====
TEST_F(TextDrawVisualValidationTest, ComplexLayoutDisplay)
{
    clearImage(white_color);

    // Create a complex layout with various text elements

    // Title with background
    drawTextWithBackground(*test_image, 400, 30, "COMPLEX LAYOUT TEST", white_color, blue_color, 3, true, 5);

    // Subtitle
    drawTextAligned(*test_image, 0, 80, 800, 20, "Demonstrating various text rendering capabilities", Pixel(64, 64, 64),
                    1, TextAlignment::CENTER, TextAlignment::MIDDLE);

    // Left column - Different scales
    drawText(*test_image, 50, 120, "Scale Progression:", black_color, 1, false);
    for (int scale = 1; scale <= 4; scale++)
    {
        std::string text = "Scale " + std::to_string(scale);
        drawText(*test_image, 50, 120 + scale * 25, text, red_color, scale, false);
    }

    // Center column - Multiline with background
    std::string center_text = "Multiline Text\nWith Background\nAnd Formatting";
    drawTextWithBackground(*test_image, 350, 150, center_text, white_color, Pixel(0, 128, 0), 1, true, 4);

    // Right column - Aligned text samples
    drawText(*test_image, 550, 120, "Alignment Examples:", black_color, 1, false);

    drawTextAligned(*test_image, 550, 140, 200, 30, "Left Aligned", magenta_color, 1, TextAlignment::LEFT,
                    TextAlignment::TOP);

    drawTextAligned(*test_image, 550, 180, 200, 30, "Right Aligned", cyan_color, 1, TextAlignment::RIGHT,
                    TextAlignment::TOP);

    drawTextAligned(*test_image, 550, 220, 200, 30, "Centered", blue_color, 1, TextAlignment::CENTER,
                    TextAlignment::TOP);

    // Bottom section - Color demonstration
    drawText(*test_image, 50, 350, "Color Samples:", black_color, 2, false);

    std::vector<Pixel> colors = {red_color, green_color, blue_color, yellow_color, magenta_color, cyan_color};
    std::vector<std::string> color_names = {"Red", "Green", "Blue", "Yellow", "Magenta", "Cyan"};

    for (size_t i = 0; i < colors.size(); i++)
    {
        int x = 50 + (i % 3) * 120;
        int y = 390 + (i / 3) * 30;
        drawText(*test_image, x, y, color_names[i], colors[i], 2, false);
    }

    // Footer
    drawTextAligned(*test_image, 0, 550, 800, 40, "Visual validation complete - Check output for rendering quality",
                    Pixel(96, 96, 96), 1, TextAlignment::CENTER, TextAlignment::MIDDLE);

    saveTestImage("complex_layout");

    EXPECT_TRUE(true) << "Complex layout test completed";
}

// ===== Edge Case Visual Tests =====
TEST_F(TextDrawVisualValidationTest, EdgeCaseDisplay)
{
    clearImage(Pixel(200, 200, 200));

    // Test edge cases visually

    // Very small scale (should be visible but tiny)
    drawText(*test_image, 50, 50, "Tiny text", black_color, 1, false);

    // Very large scale (should be big and blocky)
    drawText(*test_image, 50, 100, "HUGE", red_color, 8, false);

    // Text at image boundaries
    drawText(*test_image, 780, 10, "Edge", blue_color, 1, false);
    drawText(*test_image, 10, 580, "Bottom", green_color, 1, false);

    // Centered text at various positions
    drawText(*test_image, 400, 200, "Center 1", magenta_color, 2, true);
    drawText(*test_image, 200, 300, "Center 2", cyan_color, 3, true);

    // Special characters
    std::string special = "!@#$%^&*()_+-=[]{}|;':\",./<>?";
    drawText(*test_image, 50, 400, special, black_color, 1, false);

    // Numbers and mixed case
    drawText(*test_image, 50, 450, "ABCabc123!@#", Pixel(128, 0, 128), 2, false);

    saveTestImage("edge_cases");

    EXPECT_TRUE(true) << "Edge case visual test completed";
}

// ===== Quality Assurance Test =====
TEST_F(TextDrawVisualValidationTest, QualityAssuranceGrid)
{
    clearImage(white_color);

    // Create a comprehensive quality assurance grid

    // Title
    drawTextWithBackground(*test_image, 400, 20, "TEXT RENDERING QA GRID", white_color, black_color, 2, true, 3);

    // Grid parameters
    int grid_cols = 4;
    int grid_rows = 3;
    int cell_width = 180;
    int cell_height = 150;
    int start_x = 50;
    int start_y = 80;

    std::vector<std::string> test_cases = {
        "Basic Text",  "Scaled x2",  "With BG",  "Multiline\nTest",           "UPPERCASE", "lowercase", "Numbers 123",
        "Symbols !@#", "Mixed Aa1!", "Centered", "Long text that might wrap", "Final Test"};

    std::vector<Pixel> test_colors = {black_color,        red_color,         green_color,        blue_color,
                                      magenta_color,      cyan_color,        Pixel(128, 128, 0), Pixel(128, 0, 128),
                                      Pixel(0, 128, 128), Pixel(64, 64, 64), Pixel(192, 64, 64), Pixel(64, 192, 64)};

    for (int row = 0; row < grid_rows; row++)
    {
        for (int col = 0; col < grid_cols; col++)
        {
            int idx = row * grid_cols + col;
            if (idx >= test_cases.size())
            {
                break;
            }

            int cell_x = start_x + col * cell_width;
            int cell_y = start_y + row * cell_height;

            // Draw cell border
            std::vector<math_utils::Point<long>> border_points;
            for (int i = 0; i < cell_width; i++)
            {
                border_points.emplace_back(cell_x + i, cell_y);
                border_points.emplace_back(cell_x + i, cell_y + cell_height - 1);
            }
            for (int i = 0; i < cell_height; i++)
            {
                border_points.emplace_back(cell_x, cell_y + i);
                border_points.emplace_back(cell_x + cell_width - 1, cell_y + i);
            }
            test_image->paintPoints(border_points, Pixel(192, 192, 192));

            // Render test case
            Pixel color = test_colors[idx % test_colors.size()];

            if (test_cases[idx] == "Scaled x2")
            {
                drawText(*test_image, cell_x + 10, cell_y + 30, "Scale 2", color, 2, false);
            }
            else if (test_cases[idx] == "With BG")
            {
                drawTextWithBackground(*test_image, cell_x + 90, cell_y + 50, "Background", white_color, color, 1, true,
                                       2);
            }
            else if (test_cases[idx] == "Centered")
            {
                drawTextAligned(*test_image, cell_x, cell_y, cell_width, cell_height, "CENTER", color, 1,
                                TextAlignment::CENTER, TextAlignment::MIDDLE);
            }
            else if (test_cases[idx].find('\n') != std::string::npos)
            {
                drawMultilineText(*test_image, cell_x + 10, cell_y + 30, test_cases[idx], color, 1, 2);
            }
            else
            {
                drawText(*test_image, cell_x + 10, cell_y + 50, test_cases[idx], color, 1, false);
            }

            // Add cell number
            drawText(*test_image, cell_x + 5, cell_y + 5, std::to_string(idx + 1), Pixel(128, 128, 128), 1, false);
        }
    }

    saveTestImage("qa_grid");

    EXPECT_TRUE(true) << "Quality assurance grid completed";
}

// ===== Performance Visual Test =====
TEST_F(TextDrawVisualValidationTest, PerformanceVisualization)
{
    clearImage(Pixel(240, 240, 240));

    // Render a large amount of text to test performance visually
    auto start_time = std::chrono::high_resolution_clock::now();

    // Render grid of characters
    for (int y = 0; y < 20; y++)
    {
        for (int x = 0; x < 40; x++)
        {
            char c = 'A' + ((x + y) % 26);
            Pixel color((x * 6) % 256, (y * 12) % 256, ((x + y) * 8) % 256);

            drawCharDDA(*test_image, x * 20, y * 25, c, color, 1);
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Add performance info
    std::string perf_info = "Rendered 800 characters in " + std::to_string(duration.count()) + "ms";
    drawTextWithBackground(*test_image, 400, 550, perf_info, white_color, black_color, 1, true, 3);

    saveTestImage("performance_visualization");

    std::cout << "Performance visualization: " << perf_info << std::endl;

    EXPECT_LT(duration.count(), 1000) << "Should render 800 characters in under 1 second";
}
