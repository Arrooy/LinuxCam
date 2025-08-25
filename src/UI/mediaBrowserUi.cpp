#include "LinuxFace/UI/mediaBrowserUi.h"

#include <GL/gl.h>
#include <algorithm>
#include <utility>

namespace linuxface
{
MediaBrowserUI::MediaBrowserUI(std::shared_ptr<MediaManager> manager, std::shared_ptr<LayerManager> layerManager)
    : mediaManager(std::move(manager)), layerManager_(std::move(layerManager))
{
    if (!mediaManager)
    {
        common::logError("MediaManager is not initialized in MediaBrowserUI");
    }
}

MediaBrowserUI::~MediaBrowserUI() = default;

bool MediaBrowserUI::render()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 viewportPos = viewport->Pos;
    const ImVec2 viewportSize = viewport->Size;

    // Get menu bar height (same as in UI.cpp)
    const float menuBarHeight = ImGui::GetFrameHeight();

    // Adjust sidebar size and position to not overlap menu bar
    const float sidebarY = viewportPos.y + menuBarHeight;
    const float sidebarHeight = viewportSize.y - menuBarHeight;

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
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
    const ImVec2 actualSidebarSize = ImGui::GetWindowSize();
    // Immediately reposition and resize the window to be flush right and clamped
    const float sidebarX = viewportPos.x + viewportSize.x - actualSidebarSize.x;
    ImGui::SetWindowPos(ImVec2(sidebarX, sidebarY), ImGuiCond_Always);
    ImGui::End();
    return true;
}

void MediaBrowserUI::renderImageDataContent()
{
    Layer* selected = getSelectedLayer();
    if (selected == nullptr)
    {
        ImGui::TextDisabled("No layer selected");
        return;
    }

    ImGui::Text("Layer (%zu): %s", selected->id, selected->name.c_str());
    ImGui::Text("File: %s", selected->getLayerName().c_str());
        ImGui::Text("Size: %s", common::formatSize(selected->getSize()));
    ImGui::Text("Position (x, y): (%.1f, %.1f)", selected->x, selected->y);
    ImGui::Text("Layer number: %d", selected->getLayerNumber());
        ImGui::Text("Dimensions: %lu x %lu", selected->getWidth(), selected->getHeight());
        ImGui::Text("Format: %s", fromImageFormatToString(selected->getFormat()).c_str());

    // Layer lock checkbox
    ImGui::Separator();
    const bool lockChanged = ImGui::Checkbox("Lock Position", &selected->locked);
    if (lockChanged)
    {
        selected->dirty = true;
    }
    if (selected->locked)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(Layer cannot be dragged)");
    }

    // Output camera overlay indicator
    if (!selected->cameraDevicePath.empty() && selected->cameraDevicePath.compare(0, 7, "output:") == 0)
    {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255)); // Red text
        ImGui::Text("📹 Output Camera Overlay");
        ImGui::PopStyleColor();
        ImGui::TextWrapped("This shows the recording region for the output camera. Move this layer to adjust what area "
                           "gets recorded.");

        if (ImGui::Button("Center Recording Region"))
        {
            selected->x = 0.0f;
            selected->y = 0.0f;
            selected->dirty = true;
        }
    }

    if (selected->type == LayerType::GIF && selected->gif)
    {
        ImGui::Text("GIF Frames: %zu", selected->gif->frames().size());
        ImGui::Text("Current Frame Index: %zu", selected->gifFrameIndex);
    }
    else if (selected->type == LayerType::TEXT)
    {
        ImGui::Text("Content: %s", selected->textContent.c_str());
        if (selected->img)
        {
            ImGui::Text("Rendered as %lu x %lu image", selected->img->info.width, selected->img->info.height);
        }
    }
}


void MediaBrowserUI::renderImageOperationsContent()
{
    Layer* selected = getSelectedLayer();
    if ((selected != nullptr) && selected->type == LayerType::IMAGE && selected->img)
    {
        // Only show scale slider for webcam/streaming layers
        if (!selected->cameraDevicePath.empty())
        {
            ImGui::Text("Camera Layer Scale (applied each frame):");
            const bool scaleChanged = ImGui::SliderFloat("Scale", &selected->resizeScale, 0.1f, 3.0f, "%.2f");
            if (scaleChanged)
            {
                selected->dirty = true;
            }
            if (ImGui::Button("Reset Scale"))
            {
                selected->resizeScale = 1.0f;
                selected->dirty = true;
            }
            ImGui::Text("Current Scale: %.2f", selected->resizeScale);
                ImGui::Text("Original Size: %ldx%ld", selected->getWidth(), selected->getHeight());
                if (selected->resizeScale != 1.0f)
                {
                    const int scaledWidth = static_cast<int>(selected->getWidth() * selected->resizeScale);
                    const int scaledHeight = static_cast<int>(selected->getHeight() * selected->resizeScale);
                    ImGui::Text("Scaled Size: %dx%d", scaledWidth, scaledHeight);
                }
        }
        // Only show image operations for static image layers
        else
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
                    image->scaleToInPlace(static_cast<size_t>(newWidth), static_cast<size_t>(newHeight));
                    selected->dirty = true;
                }
            }

            // Slider-based resize for static images
            ImGui::Text("Resize with Slider:");

            static bool sliderActive = false;
            static float resizeScale = 1.0f;

            const bool sliderChanged = ImGui::SliderFloat("Scale", &resizeScale, 0.1f, 3.0f, "%.2f");

            if (sliderChanged && !sliderActive)
            {
                sliderActive = true;
            }

            if (sliderActive && !ImGui::IsItemActive())
            {
                // User released the slider, apply the resize
                sliderActive = false;
                const int targetWidth = static_cast<int>(image->info.width * resizeScale);
                const int targetHeight = static_cast<int>(image->info.height * resizeScale);

                if (targetWidth > 0 && targetHeight > 0)
                {
                    image->scaleToInPlace(static_cast<size_t>(targetWidth), static_cast<size_t>(targetHeight));
                    selected->dirty = true;
                    // Update input fields to match new size
                    newWidth = image->info.width;
                    newHeight = image->info.height;
                }
            }

            // Show preview dimensions while dragging
            if (sliderActive || ImGui::IsItemActive())
            {
                const int previewWidth = static_cast<int>(image->info.width * resizeScale);
                const int previewHeight = static_cast<int>(image->info.height * resizeScale);
                ImGui::Text("Preview: %dx%d", previewWidth, previewHeight);
            }

            ImGui::Spacing();
            // Only show Reset for static images
            if (ImGui::Button("Reset"))
            {
                if (!mediaManager->reloadImage(selected->name))
                {
                    common::logError("Failed to reload image: %s", selected->name.c_str());
                }
                else
                {
                    // Force recreation of texture and geometry by marking layer as dirty
                    selected->dirty = true;
                    // Also reset the image pointer to the reloaded one
                    selected->img = mediaManager->getImage(selected->name);
                }
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
    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
    {
        Layer& layer = layers[i];
        const std::string label = layer.name + " #" + std::to_string(layer.getLayerNumber());
        ImGui::AlignTextToFramePadding();
        ImGui::PushID(i);
        ImGui::BeginGroup();
        const bool isSelected = layer.selected;
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
            if (i < static_cast<int>(layers.size()) - 1)
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
        if (ImGui::SmallButton("x"))
        {
            removeIndex = i;
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (removeIndex >= 0 && removeIndex < static_cast<int>(layers.size()))
    {
        const bool wasSelected = layers[removeIndex].selected;
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
    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
    {
        if (layers[i].type == LayerType::IMAGE && layers[i].img)
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
