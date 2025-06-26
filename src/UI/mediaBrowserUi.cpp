#include "LinuxFace/UI/mediaBrowserUi.h"

#include <GL/gl.h>
#include <algorithm>

namespace linuxface
{
MediaBrowserUI::MediaBrowserUI(std::shared_ptr<MediaManager> manager, std::shared_ptr<LayerManager> layerManager)
    : mediaManager(manager), layerManager_(layerManager)
{
    if (!mediaManager)
    {
        common::log_error("MediaManager is not initialized in MediaBrowserUI");
    }
}

MediaBrowserUI::~MediaBrowserUI()
{
}

bool MediaBrowserUI::render()
{
    // Center the window in the screen and set its size to 90% of the viewport size
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 size = ImVec2(viewport->Size.x * 0.9f, viewport->Size.y * 0.9f);
    ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), ImGuiCond_FirstUseEver);

    std::string windowTitle = "Media Browser";
    if (selectedLayer_)
    {
        windowTitle += " - Viewing: " + selectedLayer_->name;
    }

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    bool show_window{true};
    if (ImGui::Begin(windowTitle.c_str(), &show_window, windowFlags))
    {
        renderRightSidebar();
    }
    ImGui::End();
    handleLayerDragging();
    if (!show_window)
    {
        selectedLayer_ = nullptr;
    }

    return show_window;
}

void MediaBrowserUI::renderRightSidebar()
{
    // Calculate right sidebar width (20% of window)
    ImVec2 windowSize = ImGui::GetWindowSize();
    float totalSpacing = ImGui::GetStyle().ItemSpacing.x * 2;
    float totalPadding = ImGui::GetStyle().WindowPadding.x * 2;
    float availableWidth = windowSize.x - totalSpacing - totalPadding;
    float sidebarWidth = availableWidth * 0.20f;

    ImGui::BeginChild("RightSidebar", ImVec2(sidebarWidth, 0), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar);

    // Image Data collapsing header
    if (ImGui::CollapsingHeader("Image Data", ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderImageDataContent();
    }

    ImGui::Spacing();

    // Image Operations collapsing header
    if (ImGui::CollapsingHeader("Image Operations"))
    {
        renderImageOperationsContent();
    }

    ImGui::Spacing();

    // Preview Controls collapsing header
    if (ImGui::CollapsingHeader("Preview Controls"))
    {
        renderPreviewControlsContent();
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Scene composition", ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderSceneCompositor();
    }

    ImGui::EndChild();
}

void MediaBrowserUI::renderImageDataContent()
{
    if (selectedLayer_ && selectedLayer_->type == LayerType::Image && selectedLayer_->img)
    {
        auto image = selectedLayer_->img;
        ImGui::Text("Layer: %s", selectedLayer_->name.c_str());
        ImGui::Text("File: %s", selectedLayer_->name.c_str());
        ImGui::Text("Size: %s", common::format_size(image->size()));
        ImGui::Text("Dimensions:");
        ImGui::Text("  %lu x %lu", image->info.width, image->info.height);
        ImGui::Text("Display Size:");
        ImGui::Text("  %.0f x %.0f", image->info.width * previewScale, image->info.height * previewScale);
        ImGui::Text("Scale: %.1fx", previewScale);
    }
    else if (selectedLayer_ && selectedLayer_->type == LayerType::Text)
    {
        ImGui::Text("Layer: %s", selectedLayer_->name.c_str());
        ImGui::Text("Type: Text");
        ImGui::Text("Content: %s", selectedLayer_->textContent.c_str());
        ImGui::Text("Font size: %.1f", selectedLayer_->fontSize);
    }
    else
    {
        ImGui::TextDisabled("No layer selected");
    }
}

void MediaBrowserUI::renderPreviewControlsContent()
{
    ImGui::Text("Scale:");
    bool scaleChanged = ImGui::SliderFloat("##Scale", &previewScale, 0.1f, 5.0f, "%.1fx");

    if (ImGui::Checkbox("Fit to Window", &fitToWindow))
    {
        if (fitToWindow && selectedLayer_)
        {
            float width = 800, height = 600;

            if (selectedLayer_->type == LayerType::Image && selectedLayer_->img)
            {
                width = static_cast<float>(selectedLayer_->img->info.width);
                height = static_cast<float>(selectedLayer_->img->info.height);
            }
            previewScale = calculateFitScale(width, height);
        }
    }
    if (scaleChanged)
    {
        fitToWindow = false;
    }
}

void MediaBrowserUI::renderImageOperationsContent()
{
    if (selectedLayer_ && selectedLayer_->type == LayerType::Image && selectedLayer_->img)
    {
        auto image = selectedLayer_->img;
        if (!image)
        {
            return;
        }

        static int newWidth = 0;
        static int newHeight = 0;

        if (ImGui::Button("Grayscale"))
        {
            image->toGrayscale();
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip Horizontal"))
        {
            image->flipHorizontal();
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip Vertical"))
        {
            image->flipVertical();
        }

        ImGui::Text("Rotate:");
        ImGui::SameLine();
        if (ImGui::Button("90°"))
        {
            image->rotate90();
        }
        ImGui::SameLine();
        if (ImGui::Button("180°"))
        {
            image->rotate180();
        }
        ImGui::SameLine();
        if (ImGui::Button("270°"))
        {
            image->rotate270();
        }

        if (newWidth == 0 || newHeight == 0)
        {
            newWidth = static_cast<int>(image->info.width);
            newHeight = static_cast<int>(image->info.height);
        }

        ImGui::Text("Resize Image:");
        ImGui::InputInt("Width", &newWidth);
        ImGui::InputInt("Height", &newHeight);

        if (ImGui::Button("Apply Resize"))
        {
            if (newWidth > 0 && newHeight > 0)
            {
                auto resized = image->scaleTo(static_cast<size_t>(newWidth), static_cast<size_t>(newHeight));
                if (resized)
                {
                    image->copyFrom(*resized);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Size"))
        {
            newWidth = static_cast<int>(image->info.width);
            newHeight = static_cast<int>(image->info.height);
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset"))
        {
            if(!mediaManager->reloadImage(selectedLayer_->name))
            {
                common::log_error("Failed to reload image: %s", selectedLayer_->name.c_str());
            }
        }
    }
    else
    {
        ImGui::TextDisabled("No image layer selected");
    }
}

void MediaBrowserUI::renderSceneCompositor()
{
    renderAddTextLayerUI();

    if (!layerManager_) return;
    auto& layers = layerManager_->getLayers();

    // Auto-select first layer if none selected
    if (!layers.empty() && !selectedLayer_)
    {
        selectedLayer_ = std::make_shared<Layer>(layers[0]);
    }

    // Remove invalid selection if out of bounds
    if (selectedLayer_)
    {
        auto it = std::find_if(layers.begin(), layers.end(), [this](const Layer& l)
                               { return selectedLayer_->name == l.name && selectedLayer_->getLayerNumber() == l.getLayerNumber(); });
        if (it == layers.end())
        {
            selectedLayer_ = nullptr;
        }
    }

    int removeIndex = -1;
    for (int i = 0; i < (int) layers.size(); ++i)
    {
        Layer& layer = layers[i];
        std::string label = layer.name + (layer.type == LayerType::Text ? " (T)" : " (I)");
        ImGui::AlignTextToFramePadding();
        ImGui::PushID(i);
        ImGui::BeginGroup();
        bool isSelected = selectedLayer_ && selectedLayer_->name == layer.name && selectedLayer_->getLayerNumber() == layer.getLayerNumber();
        if (ImGui::Selectable(label.c_str(), isSelected, 0, ImVec2(120, 0)))
        {
            selectedLayer_ = std::make_shared<Layer>(layer);
        }
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::BeginGroup();
        if (i == 0) ImGui::BeginDisabled();
        if (ImGui::Button("Up"))
        {
            if (i > 0) std::swap(layers[i], layers[i - 1]);
            if (isSelected) selectedLayer_ = std::make_shared<Layer>(layers[i > 0 ? i - 1 : i]);
            if (layerManager_) layerManager_->markDirty();
        }
        if (i == 0) ImGui::EndDisabled();
        ImGui::SameLine();
        if (i == static_cast<int>(layers.size()) - 1) ImGui::BeginDisabled();
        if (ImGui::Button("Down"))
        {
            if (i < (int)layers.size() - 1) std::swap(layers[i], layers[i + 1]);
            if (isSelected) selectedLayer_ = std::make_shared<Layer>(layers[i < (int)layers.size() - 1 ? i + 1 : i]);
            if (layerManager_) layerManager_->markDirty();
        }
        if (i == static_cast<int>(layers.size()) - 1) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
        {
            removeIndex = i;
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (removeIndex >= 0 && removeIndex < (int) layers.size())
    {
        if (selectedLayer_ && selectedLayer_->name == layers[removeIndex].name
            && selectedLayer_->getLayerNumber() == layers[removeIndex].getLayerNumber())
        {
            selectedLayer_ = nullptr;
        }
        layers.erase(layers.begin() + removeIndex);
        if (!layers.empty() && !selectedLayer_)
        {
            selectedLayer_ = std::make_shared<Layer>(layers[0]);
        }
        if (layerManager_) layerManager_->markDirty();
    }
    for (int i = 0; i < (int) layers.size(); ++i)
    {
        layers[i].selected = (selectedLayer_ && selectedLayer_->name == layers[i].name && selectedLayer_->getLayerNumber() == layers[i].getLayerNumber());
        if (layers[i].type == LayerType::Image && layers[i].img) layers[i].img->info.layer = i;
    }
}

void MediaBrowserUI::renderAddTextLayerUI()
{
    if (!layerManager_) return;
    static char textBuf[256] = "Write here";
    ImGui::InputText("Text", textBuf, IM_ARRAYSIZE(textBuf));
    ImGui::SameLine();
    if (ImGui::Button("New Text Layer"))
    {
        Layer newText;
        newText.type = LayerType::Text;
        newText.textContent = textBuf;
        newText.name = textBuf;
        newText.x = 10;
        newText.y = 10;
        layerManager_->addLayer(newText);
        auto& layers = layerManager_->getLayers();
        selectedLayer_ = std::make_shared<Layer>(layers.back());
    }
}


ImVec2 MediaBrowserUI::calculatePreviewSize(float originalWidth, float originalHeight)
{
    float scale = previewScale;

    if (fitToWindow)
    {
        scale = calculateFitScale(originalWidth, originalHeight);
        // Don't modify previewScale here to avoid affecting centering calculations
    }

    return ImVec2(originalWidth * scale, originalHeight * scale);
}

float MediaBrowserUI::calculateFitScale(float originalWidth, float originalHeight)
{
    ImVec2 availableSize = ImGui::GetContentRegionMax();

    float extra_padding = -2.5f;
    float scaleX = (availableSize.x + extra_padding) / originalWidth;
    float scaleY = (availableSize.y + extra_padding) / originalHeight;

    float fitScale = std::min(scaleX, scaleY);

    if (fitToWindow)
    {
        previewScale = fitScale;
    }

    return fitScale;
}

ImVec2 MediaBrowserUI::calculateCenterPosition(const ImVec2& previewSize)
{
    ImVec2 availableSize = ImGui::GetContentRegionMax();

    // Ensure we don't get negative positions
    float centerX = std::max(0.0f, (availableSize.x - previewSize.x) * 0.5f);
    float centerY = std::max(0.0f, (availableSize.y - previewSize.y) * 0.5f);

    return ImVec2(centerX, centerY);
}

void MediaBrowserUI::resetPreviewControls()
{
    previewScale = 1.0f;
    fitToWindow = true;
}

void MediaBrowserUI::handleLayerDragging()
{
    if (!layerManager_) return;
    if (!ImGui::IsMouseDown(0))
        return;

    ImVec2 delta = ImGui::GetIO().MouseDelta;
    if (!selectedLayer_)
        return;

    // Only allow dragging if mouse is dragging and a layer is selected
    if (ImGui::IsMouseDragging(0))
    {
        selectedLayer_->x += delta.x;
        selectedLayer_->y += delta.y;
        // Update the layer in the LayerManager as well
        auto& layers = layerManager_->getLayers();
        for (auto& layer : layers)
        {
            if (layer.name == selectedLayer_->name && layer.getLayerNumber() == selectedLayer_->getLayerNumber())
            {
                layer.x = selectedLayer_->x;
                layer.y = selectedLayer_->y;
                layerManager_->markDirty();
                break;
            }
        }
    }
}
} // namespace linuxface
