#ifndef MEDIABROWSERUI_H
#define MEDIABROWSERUI_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "LinuxFace/Image/mediaManager.h"
#include "imgui.h"

namespace linuxface
{
class MediaBrowserUI
{

  public:
    MediaBrowserUI(std::shared_ptr<MediaManager> manager);
    ~MediaBrowserUI();

    void render();

  private:
    void renderSidebar();
    void renderMainArea();
    void renderToolbar();
    void renderToolbarContent();
    void renderToolbarInfo();
    void renderPreview();
    void renderImagePreview(std::shared_ptr<Image> image);
    void renderGifPreview(std::shared_ptr<Gif> gif);

    // Helper method to render collapsing headers dynamically
    void renderCollapsingHeader(const std::string& headerName, const std::vector<std::string>& items, const std::string& type);

    ImVec2 calculatePreviewSize(float originalWidth, float originalHeight);
    float calculateFitScale(float originalWidth, float originalHeight);
    ImVec2 calculateCenterPosition(const ImVec2& previewSize);
    void resetPreviewControls();
    ImTextureID getOrCreateTexture(std::shared_ptr<Image> image);
    ImTextureID createTextureFromImage(std::shared_ptr<Image> image);
    void cleanupTextures();

    std::shared_ptr<MediaManager> mediaManager;

    // UI State
    bool showImages = true;
    bool showGifs = true;
    std::string selectedItem = "";
    std::string selectedType = ""; // "image" or "gif"

    // Preview controls
    float previewScale = 1.0f;
    bool fitToWindow = true;

    // Texture cache for images
    std::unordered_map<std::string, ImTextureID> textureCache;
};
} // namespace linuxface

#endif // MEDIABROWSERUI_H
