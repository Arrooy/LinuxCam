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
    if (!selected)
    {
        ImGui::TextDisabled("No layer selected");
        return;
    }

    ImGui::Text("Layer (%zu): %s", selected->id, selected->name.c_str());
    ImGui::Text("File: %s", selected->getLayerName().c_str());
    ImGui::Text("Size: %s", common::format_size(selected->getSize()));
    ImGui::Text("Position (x, y): (%.1f, %.1f)", selected->x, selected->y);
    ImGui::Text("Layer number: %d", selected->getLayerNumber());
    ImGui::Text("Dimensions: %lu x %lu", selected->getWidth(), selected->getHeight());
    ImGui::Text("Format: %s", fromImageFormatToString(selected->getFormat()).c_str());

    if (selected->type == LayerType::Gif && selected->gif)
    {
        ImGui::Text("GIF Frames: %zu", selected->gif->frames().size());
        ImGui::Text("Current Frame Index: %zu", selected->gifFrameIndex);
    }
    else if (selected->type == LayerType::Text)
    {
        ImGui::Text("Content: %s", selected->textContent.c_str());
        ImGui::Text("Font size: %.1f", selected->fontSize);
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
    if (!layerManager_)
    {
        return;
    }
    auto& layers = layerManager_->getLayers();

    int removeIndex = -1;
    for (int i = 0; i < (int) layers.size(); ++i)
    {
        Layer& layer = layers[i];
        std::string label = layer.name + " #" + std::to_string(layer.getLayerNumber());
        ImGui::AlignTextToFramePadding();
        ImGui::PushID(i);
        ImGui::BeginGroup();
        bool isSelected = layer.selected;
        if (ImGui::Selectable(label.c_str(), isSelected, 0, ImVec2(120, 0)))
        {
            // Deselect all, select this one
            for (auto& l : layers)
            {
                l.selected = false;
            }
            layer.selected = true;
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
                // Keep selection on the moved layer
                if (layers[i].selected)
                {
                    layers[i - 1].selected = true;
                    layers[i].selected = false;
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
                // Keep selection on the moved layer
                if (layers[i].selected)
                {
                    layers[i + 1].selected = true;
                    layers[i].selected = false;
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
        bool wasSelected = layers[removeIndex].selected;
        layers.erase(layers.begin() + removeIndex);
        // Select first layer if none selected
        bool anySelected = false;
        for (auto& l : layers)
        {
            if (l.selected)
            {
                anySelected = true;
            }
        }
        if (!layers.empty() && (!anySelected || wasSelected))
        {
            for (auto& l : layers)
            {
                l.selected = false;
            }
            layers[0].selected = true;
        }
        if (layerManager_)
        {
            layerManager_->markDirty();
        }
    }
    for (int i = 0; i < (int) layers.size(); ++i)
    {
        if (layers[i].type == LayerType::Image && layers[i].img)
        {
            layers[i].img->info.layer = static_cast<unsigned int>(i);
        }
        layers[i].setLayerNumber(i);
    }
}

Layer* MediaBrowserUI::getSelectedLayer()
{
    auto& layers = layerManager_->getLayers();
    for (auto& layer : layers)
    {
        if (layer.selected)
        {
            return &layer;
        }
    }
    return nullptr;
}

} // namespace linuxface
