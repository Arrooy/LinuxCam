#ifndef MEDIABROWSERUI_H
#define MEDIABROWSERUI_H


#include <memory>
#include <string>
#include <vector>

#include "LinuxFace/Image/mediaManager.h"
#include "imgui.h"
namespace linuxface
{
class MediaBrowserUI
{
  private:
    std::shared_ptr<MediaManager> mediaManager;

    // UI State
    bool showImages = true;
    bool showGifs = true;
    std::string selectedItem = "";
    std::string selectedType = ""; // "image" or "gif"

    // Preview controls
    float previewScale = 1.0f;
    ImVec2 previewPosition = ImVec2(0, 0);
    bool fitToWindow = true;
    float rotation = 0.0f;

    // Window dimensions
    const float categorySidebarWidth = 250.0f;
    const float toolbarHeight = 60.0f;

    // Texture cache for images
    std::unordered_map<std::string, ImTextureID> textureCache;

  public:
    MediaBrowserUI(std::shared_ptr<MediaManager> manager) : mediaManager(manager) {}

    ~MediaBrowserUI() { cleanupTextures(); }

    void render()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                       | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("Media Browser", nullptr, windowFlags))
        {
            renderSidebar();
            ImGui::SameLine();
            renderMainArea();
        }
        ImGui::End();
    }

  private:
    void renderSidebar()
    {
        ImGui::BeginChild("Sidebar", ImVec2(categorySidebarWidth, 0), true, ImGuiWindowFlags_NoScrollbar);

        ImGui::Text("Categories");
        ImGui::Separator();
        ImGui::Spacing();

        // Images category
        if (ImGui::CollapsingHeader("Images", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            ImGui::Checkbox("Show Images", &showImages);

            if (showImages)
            {
                auto imageNames = mediaManager->getImageNames();
                for (const auto& imageName : imageNames)
                {
                    bool isSelected = (selectedItem == imageName && selectedType == "image");
                    if (ImGui::Selectable(imageName.c_str(), isSelected))
                    {
                        selectedItem = imageName;
                        selectedType = "image";
                        resetPreviewControls();
                    }
                }
            }
            ImGui::Unindent();
        }

        // GIFs category
        if (ImGui::CollapsingHeader("GIFs", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            ImGui::Checkbox("Show GIFs", &showGifs);

            if (showGifs)
            {
                auto gifNames = mediaManager->getGifNames();
                for (const auto& gifName : gifNames)
                {
                    bool isSelected = (selectedItem == gifName && selectedType == "gif");
                    if (ImGui::Selectable(gifName.c_str(), isSelected))
                    {
                        selectedItem = gifName;
                        selectedType = "gif";
                        resetPreviewControls();
                    }
                }
            }
            ImGui::Unindent();
        }

        ImGui::EndChild();
    }

    void renderMainArea()
    {
        ImGui::BeginChild("MainArea", ImVec2(0, 0), false);

        if (!selectedItem.empty())
        {
            renderToolbar();
            ImGui::Separator();
            renderPreview();
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

    void renderToolbar()
    {
        ImGui::BeginChild("Toolbar", ImVec2(0, toolbarHeight), true);

        // Scale controls
        ImGui::Text("Scale:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        if (ImGui::SliderFloat("##Scale", &previewScale, 0.1f, 5.0f, "%.1fx"))
        {
            fitToWindow = false;
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset##Scale"))
        {
            previewScale = 1.0f;
            fitToWindow = false;
        }

        ImGui::SameLine();
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
                        // Assuming gif has similar width/height properties
                        // You may need to adjust this based on your Gif class
                        width = 800;  // placeholder - adapt to your Gif class
                        height = 600; // placeholder - adapt to your Gif class
                    }
                }

                previewScale = calculateFitScale(width, height);
            }
        }

        // Position controls
        ImGui::SameLine();
        ImGui::Text("Position:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::DragFloat("##PosX", &previewPosition.x, 1.0f, -1000.0f, 1000.0f, "X:%.0f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::DragFloat("##PosY", &previewPosition.y, 1.0f, -1000.0f, 1000.0f, "Y:%.0f");

        ImGui::SameLine();
        if (ImGui::Button("Center"))
        {
            previewPosition = ImVec2(0, 0);
        }

        // Rotation control
        ImGui::SameLine();
        ImGui::Text("Rotation:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("##Rotation", &rotation, 0.0f, 360.0f, "%.0f°");

        ImGui::EndChild();
    }

    void renderPreview()
    {
        ImVec2 previewAreaSize = ImGui::GetContentRegionAvail();
        ImVec2 previewAreaPos = ImGui::GetCursorScreenPos();

        // Create a child window for the preview area with scrolling
        ImGui::BeginChild("PreviewArea", previewAreaSize, true, ImGuiWindowFlags_HorizontalScrollbar);

        if (selectedType == "image")
        {
            auto image = mediaManager->getImage(selectedItem);
            if (image)
            {
                renderImagePreview(image);
            }
        }
        else if (selectedType == "gif")
        {
            auto gif = mediaManager->getGif(selectedItem);
            if (gif)
            {
                renderGifPreview(gif);
            }
        }

        ImGui::EndChild();
    }

    void renderImagePreview(std::shared_ptr<Image> image)
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
            // Apply rotation if needed
            // if (rotation != 0.0f)
            // {
            //     renderRotatedImage(textureID, previewSize, rotation);
            // }
            // else
            {
                ImGui::Image(textureID, previewSize);
            }
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
        ImGui::Text("Image: %s", selectedItem.c_str());
        ImGui::Text("Dimensions: %lux%lu", image->info.width, image->info.height);
        ImGui::Text("Display Size: %.0fx%.0f (Scale: %.1fx)", previewSize.x, previewSize.y, previewScale);
        ImGui::Text("File Size: %zu bytes", image->size());
    }

    void renderGifPreview(std::shared_ptr<Gif> gif)
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

    ImVec2 calculatePreviewSize(float originalWidth, float originalHeight)
    {
        float scale = previewScale;

        if (fitToWindow)
        {
            scale = calculateFitScale(originalWidth, originalHeight);
            previewScale = scale; // Update the scale slider
        }

        return ImVec2(originalWidth * scale, originalHeight * scale);
    }

    float calculateFitScale(float originalWidth, float originalHeight)
    {
        ImVec2 availableSize = ImGui::GetContentRegionAvail();
        availableSize.y -= 80; // Reserve space for info text

        float scaleX = availableSize.x / originalWidth;
        float scaleY = availableSize.y / originalHeight;

        return std::min(scaleX, scaleY);
    }

    ImVec2 calculateCenterPosition(const ImVec2& previewSize)
    {
        ImVec2 availableSize = ImGui::GetContentRegionAvail();
        ImVec2 centerPos = ImVec2((availableSize.x - previewSize.x) * 0.5f + previewPosition.x,
                                  (availableSize.y - previewSize.y) * 0.5f + previewPosition.y);

        return centerPos;
    }

    void resetPreviewControls()
    {
        previewScale = 1.0f;
        previewPosition = ImVec2(0, 0);
        fitToWindow = true;
        rotation = 0.0f;
    }

    // Texture management functions
    ImTextureID getOrCreateTexture(std::shared_ptr<Image> image)
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

    ImTextureID createTextureFromImage(std::shared_ptr<Image> image)
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

    // void renderRotatedImage(ImTextureID textureID, const ImVec2& size, float angleDegrees)
    // {
    //     ImDrawList* drawList = ImGui::GetWindowDrawList();
    //     ImVec2 center =
    //         ImVec2(ImGui::GetCursorScreenPos().x + size.x * 0.5f, ImGui::GetCursorScreenPos().y + size.y * 0.5f);

    //     float angleRadians = angleDegrees * 3.14159f / 180.0f;
    //     float cos_a = cosf(angleRadians);
    //     float sin_a = sinf(angleRadians);

    //     ImVec2 pos[4] = {
    //         center + ImVec2(-size.x * 0.5f, -size.y * 0.5f), center + ImVec2(+size.x * 0.5f, -size.y * 0.5f),
    //         center + ImVec2(+size.x * 0.5f, +size.y * 0.5f), center + ImVec2(-size.x * 0.5f, +size.y * 0.5f)};

    //     ImVec2 uv[4] = {ImVec2(0.0f, 0.0f), ImVec2(1.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec2(0.0f, 1.0f)};

    //     for (int i = 0; i < 4; i++)
    //     {
    //         ImVec2 rel_pos = ImVec2(pos[i].x - center.x, pos[i].y - center.y);
    //         pos[i] = ImVec2(center.x + cos_a * rel_pos.x - sin_a * rel_pos.y,
    //                         center.y + sin_a * rel_pos.x + cos_a * rel_pos.y);
    //     }

    //     drawList->AddImageQuad(textureID, pos[0], pos[1], pos[2], pos[3], uv[0], uv[1], uv[2], uv[3]);

    //     // Advance cursor
    //     ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x, ImGui::GetCursorPos().y + size.y));
    // }

    void cleanupTextures()
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
};
} // namespace linuxface
#endif // MEDIABROWSERUI_H
