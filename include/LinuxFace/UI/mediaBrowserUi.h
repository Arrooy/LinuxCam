#ifndef MEDIABROWSERUI_H
#define MEDIABROWSERUI_H

#include "imgui.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "LinuxFace/Image/mediaManager.h"
#include "LinuxFace/UI/layerManager.h"

namespace linuxface
{


class MediaBrowserUI
{

  public:
    MediaBrowserUI(std::shared_ptr<MediaManager> manager, std::shared_ptr<LayerManager> layerManager);
    ~MediaBrowserUI();

    bool render();

    Layer* getSelectedLayer();
  private:
    void renderLeftSidebar();
    void renderRightSidebar();
    void renderMainArea();
    void renderImageDataContent();
    void renderPreviewControlsContent();
    void renderSceneCompositor();
    void renderImagePreview(std::shared_ptr<Image> image);
    void renderGifPreview(std::shared_ptr<Gif> gif);
    void renderImageOperationsContent();

    ImVec2 calculatePreviewSize(float originalWidth, float originalHeight);
    float calculateFitScale(float originalWidth, float originalHeight);
    ImVec2 calculateCenterPosition(const ImVec2& previewSize);
    void resetPreviewControls();

    std::shared_ptr<MediaManager> mediaManager;
    std::shared_ptr<LayerManager> layerManager_;

    // UI State
    bool showImages = true;
    bool showGifs = true;

    // Preview controls
    float previewScale = 1.0f;
    bool fitToWindow = true;


    // Texture cache for images (now handled by LayerManager)
    // std::unordered_map<std::string, ImTextureID> textureCache;
};
} // namespace linuxface

#endif // MEDIABROWSERUI_H
