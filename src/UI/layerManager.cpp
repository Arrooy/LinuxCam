#include "LinuxFace/UI/layerManager.h"

#include <algorithm>
#include <boost/range/algorithm/remove_if.hpp>
#include <boost/range/algorithm/sort.hpp>

namespace linuxface
{

LayerManager::LayerManager() = default;
LayerManager::~LayerManager() = default;

void LayerManager::addLayer(const Layer& layer)
{
    layers_.push_back(layer);
}

void LayerManager::removeLayer(size_t layer_id)
{
    layers_.erase(boost::range::remove_if(layers_,, [layer_id](const Layer& l) { return l.id == layer_id; }),
                  layers_.end());
}

void LayerManager::clearLayers()
{
    layers_.clear();
}

std::vector<Layer>& LayerManager::getLayers()
{
    return layers_;
}
const std::vector<Layer>& LayerManager::getLayers() const
{
    return layers_;
}

Layer* LayerManager::getLayerByNumber(int layer_number)
{
    for (auto& l : layers_)
    {
        if (l.getLayerNumber() == layer_number)
        {
            return &l;
        }
    }
    return nullptr;
}
Layer* LayerManager::getLayerByName(const std::string& layer_name)
{
    for (auto& l : layers_)
    {
        if (l.getLayerName() == layer_name)
        {
            return &l;
        }
    }
    return nullptr;
}

Layer* LayerManager::getLayerByCameraDevicePath(const std::string& device_path)
{
    for (auto& l : layers_)
    {
        if (l.cameraDevicePath == device_path)
        {
            return &l;
        }
    }
    return nullptr;
}

void LayerManager::sortLayers()
{
    boost::range::sort(layers_,,
              [](const Layer& a, const Layer& b) { return a.getLayerNumber() < b.getLayerNumber(); });
}

void LayerManager::markDirty()
{
    for (auto& l : layers_)
    {
        l.dirty = true;
    }
}

bool LayerManager::isDirty() const
{
    for (const auto& l : layers_)
    {
        if (l.dirty)
        {
            return true;
        }
    }
    return false;
}

// Invalidate all textures (mark as dirty)
void LayerManager::invalidateTextures()
{
    for (auto& l : layers_)
    {
        l.dirty = true;
        l.invalidateTextOverlay(); // Also invalidate text overlays on window resize
    }
}

void LayerManager::setLayerDirty(int layer_number, bool dirty)
{
    for (auto& l : layers_)
    {
        if (l.getLayerNumber() == layer_number)
        {
            l.dirty = dirty;
            break;
        }
    }
}

// Static method implementation for creating text images
std::shared_ptr<Image>
Layer::createTextImage(const std::string& text, ImU32 text_color, int text_scale, bool use_background,
                       ImU32 background_color, bool center_text, bool multiline_text, int text_h_alignment,
                       int text_v_alignment, int text_padding, int bounding_width, int bounding_height)
{
    if (text.empty())
    {
        return nullptr;
    }

    // Helper to convert ImU32 to Pixel
    auto to_pixel = [](ImU32 color) -> Pixel
    {
        Pixel pixel{};
        pixel.r = static_cast<unsigned char>((color >> 0) & 0xFF);  // Red
        pixel.g = static_cast<unsigned char>((color >> 8) & 0xFF);  // Green
        pixel.b = static_cast<unsigned char>((color >> 16) & 0xFF); // Blue
        pixel.a = static_cast<unsigned char>((color >> 24) & 0xFF); // Alpha
        return pixel;
    };

    // Create TextRenderConfig from legacy parameters
    TextRenderConfig config(text, to_pixel(text_color), text_scale);

    // Set background options
    config.useBackground = use_background;
    config.backgroundColor = to_pixel(background_color);
    config.padding = text_padding;

    // Set wrap mode and dimensions
    if (bounding_width > 0 && bounding_height > 0)
    {
        // Use bounding box with specific alignment
        config.wrapMode = TextWrapMode::AUTO_WIDTH;
        config.maxWidth = bounding_width;
        config.canvasWidth = bounding_width;
        config.canvasHeight = bounding_height;
        config.horizontalAlign = static_cast<TextAlignment>(text_h_alignment);
        config.verticalAlign = static_cast<TextAlignment>(text_v_alignment + 3); // Offset for vertical alignment
    }
    else if (multiline_text)
    {
        // Legacy multiline mode - use manual line breaks
        config.wrapMode = TextWrapMode::AUTO_CANVAS;
    }
    else
    {
        // Single line mode
        config.wrapMode = center_text ? TextWrapMode::AUTO_CANVAS : TextWrapMode::NONE;
        if (center_text)
        {
            config.horizontalAlign = TextAlignment::CENTER;
            config.verticalAlign = TextAlignment::MIDDLE;
        }
    }

    // Use the new comprehensive text renderer
    return TextRenderer::renderText(config);
}

} // namespace linuxface
