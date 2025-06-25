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
        renderLeftSidebar();
        ImGui::SameLine();
        renderMainArea();
        ImGui::SameLine();
        renderRightSidebar();
    }
    ImGui::End();
}

void MediaBrowserUI::renderLeftSidebar()
{
    // Calculate sidebar width (20% of window)
    ImVec2 windowSize = ImGui::GetWindowSize();
    float sidebarWidth = windowSize.x * 0.20f;

    ImGui::BeginChild("LeftSidebar", ImVec2(sidebarWidth, 0), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar);

    ImGui::Text("Categories");
    ImGui::Separator();
    ImGui::Spacing();

    renderCollapsingHeader("Images", mediaManager->getImageNames(), "image");
    renderCollapsingHeader("GIFs", mediaManager->getGifNames(), "gif");

    ImGui::EndChild();
}

void MediaBrowserUI::renderSidebar()
{
    // Legacy method - now redirects to left sidebar
    renderLeftSidebar();
}

void MediaBrowserUI::renderMainArea()
{
    // Calculate main area width (60% of window)
    ImVec2 windowSize = ImGui::GetWindowSize();
    float mainAreaWidth = windowSize.x * 0.60f;

    ImGui::BeginChild("MainArea", ImVec2(mainAreaWidth, 0), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar);

    if (!selectedItem.empty())
    {
        // Preview area fills the entire main area
        ImGui::BeginChild("PreviewArea", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

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

void MediaBrowserUI::renderRightSidebar()
{
    // Calculate right sidebar width (20% of window)
    ImVec2 windowSize = ImGui::GetWindowSize();
    float sidebarWidth = windowSize.x * 0.20f;

    ImGui::BeginChild("RightSidebar", ImVec2(sidebarWidth, 0), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar);

    // Image Data collapsing header
    if (ImGui::CollapsingHeader("Image Data", ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderImageDataContent();
    }

    ImGui::Spacing();

    // Preview Controls collapsing header
    if (ImGui::CollapsingHeader("Preview Controls", ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderPreviewControlsContent();
    }

    ImGui::Spacing();

    // Additional Info collapsing header
    if (ImGui::CollapsingHeader("Additional Info"))
    {
        renderAdditionalInfoContent();
    }

    ImGui::EndChild();
}

void MediaBrowserUI::renderImageDataContent()
{
    if (selectedType == "image" && !selectedItem.empty())
    {
        auto image = mediaManager->getImage(selectedItem);
        if (image)
        {
            ImGui::Text("File: %s", selectedItem.c_str());
            ImGui::Text("Size: %s", common::format_size(image->size()));
            ImGui::Text("Dimensions:");
            ImGui::Text("  %lu x %lu", image->info.width, image->info.height);
            ImGui::Text("Display Size:");
            ImGui::Text("  %.0f x %.0f", image->info.width * previewScale, image->info.height * previewScale);
            ImGui::Text("Scale: %.1fx", previewScale);
        }
    }
    else if (selectedType == "gif" && !selectedItem.empty())
    {
        auto gif = mediaManager->getGif(selectedItem);
        if (gif)
        {
            ImGui::Text("File: %s", selectedItem.c_str());
            ImGui::Text("Type: GIF Animation");
            // Add more GIF-specific data here
        }
    }
    else
    {
        ImGui::TextDisabled("No item selected");
    }
}

void MediaBrowserUI::renderPreviewControlsContent()
{
    ImGui::Text("Scale:");
    ImGui::SetNextItemWidth(-1);
    bool scaleChanged = ImGui::SliderFloat("##Scale", &previewScale, 0.1f, 5.0f, "%.1fx");

    if (ImGui::Checkbox("Fit to Window", &fitToWindow))
    {
        if (fitToWindow && !selectedItem.empty())
        {
            float width = 800, height = 600;
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
                    width = 800;
                    height = 600;
                }
            }
            previewScale = calculateFitScale(width, height);
        }
    }
    if (scaleChanged)
    {
        fitToWindow = false;
    }
}

void MediaBrowserUI::renderAdditionalInfoContent()
{
    ImGui::TextWrapped("This section displays additional metadata and quick actions for the selected media item.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Quick Actions:");
    if (ImGui::Button("Copy Path", ImVec2(-1, 0)))
    {
        // TODO: Implement copy to clipboard functionality
    }

    if (ImGui::Button("Open Location", ImVec2(-1, 0)))
    {
        // TODO: Implement open in file explorer functionality
    }
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

void MediaBrowserUI::renderImagePreview(std::shared_ptr<Image> image)
{
    if (!image || !image->data())
    {
        return;
    }

    float originalWidth = static_cast<float>(image->info.width);
    float originalHeight = static_cast<float>(image->info.height);

    ImVec2 previewSize = calculatePreviewSize(originalWidth, originalHeight);

    ImVec2 startPos = ImGui::GetCursorPos();
    ImVec2 contentMax = ImGui::GetContentRegionMax();
    ImVec2 fullSize = ImVec2(contentMax.x - startPos.x, contentMax.y - startPos.y);

    ImVec2 centerPos = ImVec2((fullSize.x - previewSize.x) * 0.5f, (fullSize.y - previewSize.y) * 0.5f);

    // Ensure positive positioning
    centerPos.x = std::max(0.0f, centerPos.x);
    centerPos.y = std::max(0.0f, centerPos.y);

    ImGui::SetCursorPos(ImVec2(startPos.x + centerPos.x, startPos.y + centerPos.y));

    ImTextureID textureID = getOrCreateTexture(image);

    if (textureID)
    {
        ImGui::Image(textureID, previewSize);
    }
    else
    {
        ImGui::GetWindowDrawList()->AddRect(
            ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + previewSize.x, ImGui::GetCursorScreenPos().y + previewSize.y),
            IM_COL32(100, 100, 100, 255));
        ImGui::SetCursorPos(ImVec2(centerPos.x, centerPos.y + previewSize.y / 2));
        ImGui::Text("Failed to load texture");
    }
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
