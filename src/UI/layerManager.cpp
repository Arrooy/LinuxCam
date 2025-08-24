#include "LinuxFace/UI/layerManager.h"

#include <algorithm>

namespace linuxface
{

LayerManager::LayerManager() = default;

void LayerManager::addLayer(const Layer& layer)
{
    layers_.push_back(layer);
}

void LayerManager::removeLayer(size_t layerId)
{
    layers_.erase(std::remove_if(layers_.begin(), layers_.end(), [layerId](const Layer& l) { return l.id == layerId; }),
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

Layer* LayerManager::getLayerByNumber(int layerNumber)
{
    for (auto& l : layers_)
    {
        if (l.getLayerNumber() == layerNumber)
        {
            return &l;
        }
    }
    return nullptr;
}
Layer* LayerManager::getLayerByName(const std::string& layerName)
{
    for (auto& l : layers_)
    {
        if (l.getLayerName() == layerName)
        {
            return &l;
        }
    }
    return nullptr;
}

Layer* LayerManager::getLayerByCameraDevicePath(const std::string& devicePath)
{
    for (auto& l : layers_)
    {
        if (l.cameraDevicePath == devicePath)
        {
            return &l;
        }
    }
    return nullptr;
}

void LayerManager::sortLayers()
{
    std::sort(layers_.begin(), layers_.end(),
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

void LayerManager::setLayerDirty(int layerNumber, bool dirty)
{
    for (auto& l : layers_)
    {
        if (l.getLayerNumber() == layerNumber)
        {
            l.dirty = dirty;
            break;
        }
    }
}

// Static method implementation for creating text images
std::shared_ptr<Image>
Layer::createTextImage(const std::string& text, ImU32 textColor, int textScale, bool useBackground,
                       ImU32 backgroundColor, bool centerText, bool multilineText, int textHAlignment,
                       int textVAlignment, int textPadding, int boundingWidth, int boundingHeight)
{
    if (text.empty())
    {
        return nullptr;
    }

    // Helper to convert ImU32 to Pixel
    auto toPixel = [](ImU32 color) -> Pixel
    {
        Pixel pixel{};
        pixel.r = static_cast<unsigned char>((color >> 0) & 0xFF);  // Red
        pixel.g = static_cast<unsigned char>((color >> 8) & 0xFF);  // Green
        pixel.b = static_cast<unsigned char>((color >> 16) & 0xFF); // Blue
        pixel.a = static_cast<unsigned char>((color >> 24) & 0xFF); // Alpha
        return pixel;
    };

    // Create TextRenderConfig from legacy parameters
    TextRenderConfig config(text, toPixel(textColor), textScale);

    // Set background options
    config.useBackground = useBackground;
    config.backgroundColor = toPixel(backgroundColor);
    config.padding = textPadding;

    // Set wrap mode and dimensions
    if (boundingWidth > 0 && boundingHeight > 0)
    {
        // Use bounding box with specific alignment
        config.wrapMode = TextWrapMode::AUTO_WIDTH;
        config.maxWidth = boundingWidth;
        config.canvasWidth = boundingWidth;
        config.canvasHeight = boundingHeight;
        config.horizontalAlign = static_cast<TextAlignment>(textHAlignment);
        config.verticalAlign = static_cast<TextAlignment>(textVAlignment + 3); // Offset for vertical alignment
    }
    else if (multilineText)
    {
        // Legacy multiline mode - use manual line breaks
        config.wrapMode = TextWrapMode::AUTO_CANVAS;
    }
    else
    {
        // Single line mode
        config.wrapMode = centerText ? TextWrapMode::AUTO_CANVAS : TextWrapMode::NONE;
        if (centerText)
        {
            config.horizontalAlign = TextAlignment::CENTER;
            config.verticalAlign = TextAlignment::MIDDLE;
        }
    }

    // Use the new comprehensive text renderer
    return TextRenderer::renderText(config);
}

} // namespace linuxface
