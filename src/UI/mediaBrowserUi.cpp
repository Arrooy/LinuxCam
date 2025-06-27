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
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 viewportPos = viewport->Pos;
    ImVec2 viewportSize = viewport->Size;

    // Get menu bar height (same as in UI.cpp)
    float menuBarHeight = ImGui::GetFrameHeight();

    // Adjust sidebar size and position to not overlap menu bar
    float sidebarY = viewportPos.y + menuBarHeight;
    float sidebarHeight = viewportSize.y - menuBarHeight;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoScrollbar;

    ImGui::SetNextWindowPos(ImVec2(viewportPos.x + viewportSize.x, sidebarY), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(0, sidebarHeight), ImGuiCond_Always);
    ImGui::Begin("RightSidebar", nullptr, flags);

    if (ImGui::CollapsingHeader("Image Data", ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderImageDataContent();
    }
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Image Operations"))
    {
        renderImageOperationsContent();
    }
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Preview Controls"))
    {
        renderPreviewControlsContent();
    }
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Scene composition", ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderSceneCompositor();
    }

    // Get the actual window width after rendering
    ImVec2 actualSidebarSize = ImGui::GetWindowSize();
    // Immediately reposition and resize the window to be flush right and clamped
    float sidebarX = viewportPos.x + viewportSize.x - actualSidebarSize.x;
    ImGui::SetWindowPos(ImVec2(sidebarX, sidebarY), ImGuiCond_Always);
    ImGui::End();
    return true;
}

void MediaBrowserUI::renderImageDataContent()
{
    Layer* selected = getSelectedLayer();
    if (selected)
    {
        if (selected->type == LayerType::Image && selected->img)
        {
            auto image = selected->img;
            ImGui::Text("Layer: %s", selected->name.c_str());
            ImGui::Text("File: %s", selected->name.c_str());
            ImGui::Text("Size: %s", common::format_size(image->size()));
            ImGui::Text("Dimensions:");
            ImGui::Text("  %lu x %lu", image->info.width, image->info.height);
            ImGui::Text("Display Size:");
            ImGui::Text("  %.0f x %.0f", image->info.width * previewScale, image->info.height * previewScale);
            ImGui::Text("Scale: %.1fx", previewScale);
        }
        else if (selected->type == LayerType::Text)
        {
            ImGui::Text("Layer: %s", selected->name.c_str());
            ImGui::Text("Type: Text");
            ImGui::Text("Content: %s", selected->textContent.c_str());
            ImGui::Text("Font size: %.1f", selected->fontSize);
        }
        else
        {
            ImGui::TextDisabled("No layer selected");
        }
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
        Layer* selected = getSelectedLayer();
        if (fitToWindow && selected)
        {
            float width = 800, height = 600;
            if (selected->type == LayerType::Image && selected->img)
            {
                width = static_cast<float>(selected->img->info.width);
                height = static_cast<float>(selected->img->info.height);
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
    Layer* selected = getSelectedLayer();
    if (selected && selected->type == LayerType::Image && selected->img)
    {
        auto image = selected->img;
        if (!image)
        {
            return;
        }

        static int newWidth = 0;
        static int newHeight = 0;

        if (ImGui::Button("Grayscale"))
        {
            image->toGrayscale();
            selected->dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip Horizontal"))
        {
            image->flipHorizontal();
            selected->dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip Vertical"))
        {
            image->flipVertical();
            selected->dirty = true;
        }

        ImGui::Text("Rotate:");
        ImGui::SameLine();
        if (ImGui::Button("90°"))
        {
            image->rotate90();
            selected->dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("180°"))
        {
            image->rotate180();
            selected->dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("270°"))
        {
            image->rotate270();
            selected->dirty = true;
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
                    selected->dirty = true;
                }
            }
        }
        ImGui::SameLine();
        ImGui::Spacing();
        if (ImGui::Button("Reset"))
        {
            if (!mediaManager->reloadImage(selected->name))
            {
                common::log_error("Failed to reload image: %s", selected->name.c_str());
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

    if (!layerManager_)
    {
        return;
    }
    auto& layers = layerManager_->getLayers();

    // Auto-select first layer if none selected
    if (!layers.empty() && selectedLayerIndex_ < 0)
    {
        selectedLayerIndex_ = 0;
    }
    // Remove invalid selection if out of bounds
    if (selectedLayerIndex_ >= (int) layers.size())
    {
        selectedLayerIndex_ = -1;
    }

    int removeIndex = -1;
    for (int i = 0; i < (int) layers.size(); ++i)
    {
        Layer& layer = layers[i];
        std::string label = layer.name + (layer.type == LayerType::Text ? " (T)" : " (I)") + " #" + std::to_string(layer.getLayerNumber());
        ImGui::AlignTextToFramePadding();
        ImGui::PushID(i);
        ImGui::BeginGroup();
        bool isSelected = (selectedLayerIndex_ == i);
        if (ImGui::Selectable(label.c_str(), isSelected, 0, ImVec2(120, 0)))
        {
            selectedLayerIndex_ = i;
        }
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::BeginGroup();
        if (i == 0)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Up"))
        {
            if (i > 0)
            {
                std::swap(layers[i], layers[i - 1]);
                if (selectedLayerIndex_ == i)
                {
                    selectedLayerIndex_ = i - 1;
                }
                else if (selectedLayerIndex_ == i - 1)
                {
                    selectedLayerIndex_ = i;
                }
            }
            if (layerManager_)
            {
                layerManager_->markDirty();
            }
        }
        if (i == 0)
        {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (i == static_cast<int>(layers.size()) - 1)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Down"))
        {
            if (i < (int) layers.size() - 1)
            {
                std::swap(layers[i], layers[i + 1]);
                if (selectedLayerIndex_ == i)
                {
                    selectedLayerIndex_ = i + 1;
                }
                else if (selectedLayerIndex_ == i + 1)
                {
                    selectedLayerIndex_ = i;
                }
            }
            if (layerManager_)
            {
                layerManager_->markDirty();
            }
        }
        if (i == static_cast<int>(layers.size()) - 1)
        {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (layer.isBaseLayer)
        {
            ImGui::BeginDisabled();
            ImGui::SmallButton("x");
            ImGui::EndDisabled();
        }
        else
        {
            if (ImGui::SmallButton("x"))
            {
                removeIndex = i;
            }
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (removeIndex >= 0 && removeIndex < (int) layers.size())
    {
        if (selectedLayerIndex_ == removeIndex)
        {
            selectedLayerIndex_ = -1;
        }
        else if (selectedLayerIndex_ > removeIndex)
        {
            selectedLayerIndex_--;
        }
        layers.erase(layers.begin() + removeIndex);
        if (!layers.empty() && selectedLayerIndex_ < 0)
        {
            selectedLayerIndex_ = 0;
        }
        if (layerManager_)
        {
            layerManager_->markDirty();
        }
    }
    for (int i = 0; i < (int) layers.size(); ++i)
    {
        layers[i].selected = (selectedLayerIndex_ == i);
        if (layers[i].type == LayerType::Image && layers[i].img)
        {
            layers[i].img->info.layer = static_cast<unsigned int>(i);
        }
        layers[i].setLayerNumber(i);
    }
}

void MediaBrowserUI::renderAddTextLayerUI()
{
    if (!layerManager_)
    {
        return;
    }
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
        selectedLayerIndex_ = layers.empty() ? -1 : (int) layers.size() - 1;
    }
}

Layer* MediaBrowserUI::getSelectedLayer()
{
    auto& layers = layerManager_->getLayers();
    if (selectedLayerIndex_ >= 0 && selectedLayerIndex_ < (int) layers.size())
    {
        return &layers[selectedLayerIndex_];
    }
    return nullptr;
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
} // namespace linuxface
