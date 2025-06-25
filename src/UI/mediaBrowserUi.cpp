#include "LinuxFace/UI/mediaBrowserUi.h"

#include <GL/gl.h>

namespace linuxface
{
MediaBrowserUI::MediaBrowserUI(std::shared_ptr<MediaManager> manager) : mediaManager(manager)
{
    if (!mediaManager)
    {
        common::log_error("MediaManager is not initialized in MediaBrowserUI");
    }
}

MediaBrowserUI::~MediaBrowserUI()
{
    cleanupTextures();
}

void MediaBrowserUI::render()
{
    // Center the window in the screen and set its size to 90% of the viewport size
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 size = ImVec2(viewport->Size.x * 0.9f, viewport->Size.y * 0.9f);
    ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), ImGuiCond_FirstUseEver);

    std::string windowTitle = "Media Browser";
    if (!selectedItem.empty())
    {
        windowTitle += " - Viewing: " + selectedItem;
    }

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin(windowTitle.c_str(), nullptr, windowFlags))
    {
        renderSidebar();
        ImGui::SameLine();
        renderMainArea();
    }
    ImGui::End();
}

void MediaBrowserUI::renderSidebar()
{
    ImGui::BeginChild("Sidebar", ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeX,
                      ImGuiWindowFlags_NoScrollbar);

    ImGui::Text("Categories");
    ImGui::Separator();
    ImGui::Spacing();

    renderCollapsingHeader("Images", mediaManager->getImageNames(), "image");
    renderCollapsingHeader("GIFs", mediaManager->getGifNames(), "gif");

    ImGui::EndChild();
}

void MediaBrowserUI::renderCollapsingHeader(const std::string& headerName, const std::vector<std::string>& items,
                                            const std::string& type)
{
    if (items.empty())
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::CollapsingHeader(headerName.c_str()))
    {
        for (const auto& item : items)
        {
            bool isSelected = (selectedItem == item && selectedType == type);
            if (ImGui::Selectable(item.c_str(), isSelected))
            {
                selectedItem = item;
                selectedType = type;
                resetPreviewControls();
            }
        }
    }
    if (items.empty())
    {
        ImGui::EndDisabled();
    }
}

void MediaBrowserUI::renderMainArea()
{
    ImGui::BeginChild("MainArea", ImVec2(0, 0), ImGuiChildFlags_None);

    if (!selectedItem.empty())
    {
        // Calculate available space for preview and toolbar
        ImVec2 availableSize = ImGui::GetContentRegionAvail();

        // Calculate toolbar height based on content
        float toolbarHeight = ImGui::GetTextLineHeightWithSpacing() * 3 + ImGui::GetStyle().WindowPadding.y * 2;
        float previewHeight = availableSize.y - toolbarHeight - 10.0f; // 10px spacing

        // Render preview area at the top
        ImGui::BeginChild("PreviewArea", ImVec2(availableSize.x, previewHeight), ImGuiChildFlags_Border,
                          ImGuiWindowFlags_HorizontalScrollbar);

        if (selectedType == "image")
        {
            auto image = mediaManager->getImage(selectedItem);
            if (image)
            {
                renderImagePreview(image);
            }
            else
            {
                ImGui::Text("Failed to load image preview.");
            }
        }
        else if (selectedType == "gif")
        {
            auto gif = mediaManager->getGif(selectedItem);
            if (gif)
            {
                renderGifPreview(gif);
            }
            else
            {
                ImGui::Text("Failed to load GIF preview.");
            }
        }

        ImGui::EndChild();

        // Add some spacing
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Render toolbar at the bottom without scroll bars - auto-size height
        ImGui::BeginChild("ToolbarArea", ImVec2(availableSize.x, 0),
                          ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
        renderToolbarContent();
        ImGui::EndChild();
    }
    else
    {
        // Show placeholder when nothing is selected
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        ImVec2 textSize = ImGui::CalcTextSize("Select an item to preview");
        ImGui::SetCursorPos(ImVec2((contentSize.x - textSize.x) * 0.5f, (contentSize.y - textSize.y) * 0.5f));
        ImGui::TextDisabled("Select an item to preview");
    }

    ImGui::EndChild();
}

void MediaBrowserUI::renderToolbar()
{
    // This method is now just a wrapper for the toolbar content
    renderToolbarContent();
}

void MediaBrowserUI::renderToolbarContent()
{
    // Scale controls
    ImGui::Text("Scale:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50);
    if (ImGui::SliderFloat("##Scale", &previewScale, 0.1f, 5.0f, "%.1fx"))
    {
        fitToWindow = false;
    }
    if (ImGui::Checkbox("Fit to Window", &fitToWindow))
    {
        if (fitToWindow && !selectedItem.empty())
        {
            // Get dimensions based on selected item type
            float width = 800, height = 600; // defaults

            if (selectedType == "image")
            {
                auto image = mediaManager->getImage(selectedItem);
                if (image)
                {
                    width = static_cast<float>(image->info.width);
                    height = static_cast<float>(image->info.height);
                }
            }
            else if (selectedType == "gif")
            {
                auto gif = mediaManager->getGif(selectedItem);
                if (gif)
                {
                    width = 800;  // placeholder - adapt to your Gif class
                    height = 600; // placeholder - adapt to your Gif class
                }
            }

            previewScale = calculateFitScale(width, height);
        }
    }

    if (ImGui::Button("Reset##Scale"))
    {
        previewScale = 1.0f;
        fitToWindow = false;
    }
}

void MediaBrowserUI::renderImagePreview(std::shared_ptr<Image> image)
{
    if (!image || !image->data())
    {
        return;
    }

    // Get actual image dimensions
    float originalWidth = static_cast<float>(image->info.width);
    float originalHeight = static_cast<float>(image->info.height);

    ImVec2 previewSize = calculatePreviewSize(originalWidth, originalHeight);
    ImVec2 centerPos = calculateCenterPosition(previewSize);

    ImGui::SetCursorPos(centerPos);

    // Create or get cached texture for this image
    ImTextureID textureID = getOrCreateTexture(image);

    if (textureID)
    {
        ImGui::Image(textureID, previewSize);
    }
    else
    {
        // Fallback: draw placeholder rectangle if texture creation fails
        ImGui::GetWindowDrawList()->AddRect(
            ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + previewSize.x, ImGui::GetCursorScreenPos().y + previewSize.y),
            IM_COL32(100, 100, 100, 255));
        ImGui::SetCursorPos(ImVec2(centerPos.x, centerPos.y + previewSize.y / 2));
        ImGui::Text("Failed to load texture");
    }

    // Display image info
    ImGui::SetCursorPos(ImVec2(centerPos.x, centerPos.y + previewSize.y + 10));
    ImGui::Text("Image: %s - Size %s", selectedItem.c_str(), common::format_size(image->size()));
    ImGui::Text("Dimensions: %lux%lu", image->info.width, image->info.height);
    ImGui::Text("Display Size: %.0fx%.0f (Scale: %.1fx)", previewSize.x, previewSize.y, previewScale);
}

void MediaBrowserUI::renderGifPreview(std::shared_ptr<Gif> gif)
{
    // Similar to renderImagePreview but for GIFs
    // You'll need to handle frame updates for animation

    ImVec2 previewSize = calculatePreviewSize(800, 600); // Placeholder dimensions
    ImVec2 centerPos = calculateCenterPosition(previewSize);

    ImGui::SetCursorPos(centerPos);

    // Placeholder rectangle - replace with actual GIF rendering
    ImGui::GetWindowDrawList()->AddRect(
        ImGui::GetCursorScreenPos(),
        ImVec2(ImGui::GetCursorScreenPos().x + previewSize.x, ImGui::GetCursorScreenPos().y + previewSize.y),
        IM_COL32(100, 150, 100, 255));

    // Display GIF info
    ImGui::SetCursorPos(ImVec2(centerPos.x, centerPos.y + previewSize.y + 10));
    ImGui::Text("GIF: %s", selectedItem.c_str());
    ImGui::Text("Size: %.0fx%.0f (Scale: %.1fx)", previewSize.x, previewSize.y, previewScale);
}

ImVec2 MediaBrowserUI::calculatePreviewSize(float originalWidth, float originalHeight)
{
    float scale = previewScale;

    if (fitToWindow)
    {
        scale = calculateFitScale(originalWidth, originalHeight);
        previewScale = scale; // Update the scale slider
    }

    return ImVec2(originalWidth * scale, originalHeight * scale);
}

float MediaBrowserUI::calculateFitScale(float originalWidth, float originalHeight)
{
    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    availableSize.y -= 80; // Reserve space for info text

    float scaleX = availableSize.x / originalWidth;
    float scaleY = availableSize.y / originalHeight;

    return std::min(scaleX, scaleY);
}

ImVec2 MediaBrowserUI::calculateCenterPosition(const ImVec2& previewSize)
{
    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    ImVec2 centerPos = ImVec2((availableSize.x - previewSize.x) * 0.5f, (availableSize.y - previewSize.y) * 0.5f);

    return centerPos;
}

void MediaBrowserUI::resetPreviewControls()
{
    previewScale = 1.0f;
    fitToWindow = true;
}

// Texture management functions
ImTextureID MediaBrowserUI::getOrCreateTexture(std::shared_ptr<Image> image)
{
    std::string cacheKey = selectedItem; // Use filename as cache key

    auto it = textureCache.find(cacheKey);
    if (it != textureCache.end())
    {
        return it->second;
    }

    // Create new texture from image data
    ImTextureID textureID = createTextureFromImage(image);
    if (textureID)
    {
        textureCache[cacheKey] = textureID;
    }

    return textureID;
}

ImTextureID MediaBrowserUI::createTextureFromImage(std::shared_ptr<Image> image)
{
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image->info.width, image->info.height, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 image->data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return static_cast<ImTextureID>(textureID);
}

void MediaBrowserUI::cleanupTextures()
{
    // Clean up OpenGL textures when needed
    // This should be called in destructor or when changing folders
    for (auto& pair : textureCache)
    {
        // Example OpenGL cleanup:
        GLuint textureID = static_cast<GLuint>(pair.second);
        glDeleteTextures(1, &textureID);
    }
    textureCache.clear();
}
} // namespace linuxface
