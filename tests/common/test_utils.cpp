/**
 * TEST UTILITIES IMPLEMENTATION
 *
 * Common utility functions for integration tests
 * Provides environment variable handling, test configuration helpers, and path resolution
 */

#include "test_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>

// Include LinuxFace components for grid visualization
#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/text_draw.h"

// Include the generated test configuration
#include "test_config.h"

using namespace linuxface;

namespace TestUtils
{

// ========== Environment Variable Utilities ==========

int getMaxSamples(int upper_limit)
{
    int samples = getEnvVarInt("WFLW_MAX_SAMPLES", 5);
    return std::max(1, std::min(samples, upper_limit));
}

int getMaxFacesPerImage()
{
    int faces = getEnvVarInt("WFLW_MAX_FACES_PER_IMAGE", 5);
    return std::max(1, faces);
}

int getEnvVarInt(const char* var_name, int default_value)
{
    const char* env_value = std::getenv(var_name);
    if (env_value == nullptr)
    {
        return default_value;
    }

    try
    {
        return std::stoi(env_value);
    }
    catch (const std::exception& e)
    {
        std::cout << "Warning: Invalid " << var_name << " value '" << env_value << "', using default (" << default_value
                  << ")" << std::endl;
        return default_value;
    }
}

bool getEnvVarBool(const char* var_name)
{
    const char* env_value = std::getenv(var_name);
    if (env_value == nullptr)
    {
        return false;
    }

    std::string value(env_value);
    // Convert to lowercase for case-insensitive comparison
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    
    return value == "1" || value == "true" || value == "yes" || value == "on" || value == "enabled";
}

// ========== Path Resolution Utilities ==========

std::string getTestDataDir()
{
    return ::getTestDataDir();  // Delegate to the generated function
}

std::string getTestImagePath(const std::string& filename)
{
    return ::getTestImagePath(filename);  // Delegate to the generated function
}

std::string getTestResultsDir(const std::string& test_suite_name)
{
    return ::getSourceDir() + "/tests/" + test_suite_name + "/results";
}

std::string getTestResultPath(const std::string& test_suite_name, const std::string& filename)
{
    return getTestResultsDir(test_suite_name) + "/" + filename;
}

std::string getConfigPath()
{
    return ::getConfigFile();  // Delegate to the generated function
}

std::string getModelsDir()
{
    return ::getModelsDirectory();  // Delegate to the generated function
}

std::string getModelPath(const std::string& model_name)
{
    return getModelsDir() + "/" + model_name;
}

// ========== Model Discovery Utilities ==========

std::vector<std::string> getEmbeddingModelFiles()
{
    std::vector<std::string> embedding_models;
    std::string models_dir = getModelsDir();

    // Define patterns for embedding models
    std::vector<std::regex> patterns = {
        std::regex("focal-arcface.*\\.onnx"), 
        std::regex("arcface.*\\.onnx"),
        std::regex("cavaface.*\\.onnx"),      
        std::regex("CurricularFace\\.onnx"),
        std::regex("glint.*\\.onnx"),         
        std::regex("ms1mv3_arcface.*\\.onnx")
    };

    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(models_dir))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();

                // Check if filename matches any embedding model pattern
                for (const auto& pattern : patterns)
                {
                    if (std::regex_match(filename, pattern))
                    {
                        embedding_models.push_back(filename);
                        break;
                    }
                }
            }
        }
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::cerr << "Error reading models directory: " << e.what() << std::endl;
    }

    // Sort for consistent ordering
    std::sort(embedding_models.begin(), embedding_models.end());
    return embedding_models;
}

// ========== Grid Visualization Utilities ==========

TestUtils::GridCell::GridCell() : highlight_color(255, 255, 255) {}

std::unique_ptr<linuxface::Image> createGridVisualization(
    const std::vector<TestUtils::GridCell>& cells,
    int grid_rows,
    int grid_cols,
    int cell_spacing,
    const linuxface::Pixel& background_color,
    const std::string& title)
{
    if (cells.empty())
    {
        std::cerr << "Error: No cells provided for grid visualization" << std::endl;
        return nullptr;
    }

    // Auto-calculate grid dimensions if not provided
    if (grid_rows <= 0 && grid_cols <= 0)
    {
        // Aim for roughly square grid
        grid_cols = static_cast<int>(std::ceil(std::sqrt(cells.size())));
        grid_rows = static_cast<int>(std::ceil(static_cast<double>(cells.size()) / grid_cols));
    }
    else if (grid_rows <= 0)
    {
        grid_rows = static_cast<int>(std::ceil(static_cast<double>(cells.size()) / grid_cols));
    }
    else if (grid_cols <= 0)
    {
        grid_cols = static_cast<int>(std::ceil(static_cast<double>(cells.size()) / grid_rows));
    }

    // Find the maximum dimensions for uniformity
    int max_width = 0;
    int max_height = 0;
    for (const auto& cell : cells)
    {
        if (cell.image)
        {
            max_width = std::max(max_width, static_cast<int>(cell.image->info.width));
            max_height = std::max(max_height, static_cast<int>(cell.image->info.height));
        }
    }

    if (max_width <= 0 || max_height <= 0)
    {
        std::cerr << "Error: No valid images found in grid cells" << std::endl;
        return nullptr;
    }

    // Add space for text labels at the bottom
    const int text_height = 30;  // Space for label text
    const int cell_width = max_width;
    const int cell_height = max_height + text_height;

    // Calculate title space
    const int title_height = title.empty() ? 0 : 50;

    // Calculate total grid image size
    const int grid_width = grid_cols * cell_width + (grid_cols + 1) * cell_spacing;
    const int grid_height = grid_rows * cell_height + (grid_rows + 1) * cell_spacing + title_height;

    // Create the grid image with background color
    auto grid_image = std::make_unique<linuxface::Image>(background_color, grid_width, grid_height);
    grid_image->info.format = linuxface::ImageFormat::RGB;
    grid_image->info.pixelSizeBytes = 3;

    // Add title if provided
    if (!title.empty())
    {
        drawTextWithBackground(*grid_image, grid_width / 2, 25, title, 
                               linuxface::Pixel(0, 0, 0),     // Black text
                               linuxface::Pixel(255, 255, 255), // White background
                               3,                   // Scale
                               true,                // Center
                               4);                  // Padding
    }

    // Place each cell in the grid
    for (size_t i = 0; i < cells.size() && i < static_cast<size_t>(grid_rows * grid_cols); ++i)
    {
        int row = static_cast<int>(i) / grid_cols;
        int col = static_cast<int>(i) % grid_cols;

        int cell_x = cell_spacing + col * (cell_width + cell_spacing);
        int cell_y = cell_spacing + row * (cell_height + cell_spacing) + title_height;

        const auto& cell = cells[i];

        if (!cell.image)
        {
            std::cerr << "Warning: Null image in grid cell " << i << ", skipping" << std::endl;
            continue;
        }

        // Center the image within the cell
        int image_x = cell_x + (cell_width - static_cast<int>(cell.image->info.width)) / 2;
        int image_y = cell_y + (cell_height - text_height - static_cast<int>(cell.image->info.height)) / 2;

        // Add highlight border if requested
        std::unique_ptr<Image> image_to_paste = cell.image->deepCopy();
        if (cell.highlight)
        {
            const int border_thickness = 3;
            image_to_paste->drawBorder(cell.highlight_color, border_thickness);
        }

        // Paste the image
        grid_image->pasteAt(*image_to_paste, image_x, image_y, false);

        // Add label text if provided
        if (!cell.label.empty())
        {
            // Process label for better readability
            std::string display_label = cell.label;
            
            // Remove file extensions
            if (display_label.length() >= 5 && display_label.substr(display_label.length() - 5) == ".onnx")
            {
                display_label = display_label.substr(0, display_label.length() - 5);
            }
            if (display_label.length() >= 4 && display_label.substr(display_label.length() - 4) == ".ppm")
            {
                display_label = display_label.substr(0, display_label.length() - 4);
            }

            // Replace underscores and hyphens with spaces for better readability
            std::replace(display_label.begin(), display_label.end(), '_', ' ');
            std::replace(display_label.begin(), display_label.end(), '-', ' ');

            // Truncate if too long
            if (display_label.length() > 20)
            {
                display_label = display_label.substr(0, 17) + "...";
            }

            // Calculate text position (centered at bottom of cell)
            int text_x = cell_x + cell_width / 2;
            int text_y = cell_y + cell_height - text_height + 10;

            // Draw text with background for better visibility
            drawTextWithBackground(*grid_image, text_x, text_y, display_label, 
                                   linuxface::Pixel(0, 0, 0),     // Black text
                                   linuxface::Pixel(255, 255, 255), // White background
                                   2,                   // Scale
                                   true,                // Center
                                   2);                  // Padding
        }
    }

    return grid_image;
}

} // namespace TestUtils
