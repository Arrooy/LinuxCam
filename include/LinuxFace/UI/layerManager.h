#ifndef LAYERMANAGER_H
#define LAYERMANAGER_H

#include "imgui.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "LinuxFace/Image/gif.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/text_renderer.h"
#include "LinuxFace/videoLoader.h"

namespace linuxface
{

// Layer type for compositing
enum class LayerType
{
    IMAGE,
    GIF,
    VIDEO,
    TEXT
};

struct Layer
{
    LayerType type;
    std::string name;
    bool selected{false};
    bool dirty{true};
    bool locked{false}; // Prevents layer from being dragged when true

    // Unique identifier for this layer instance
    size_t id = 0;

    // Camera-specific fields
    std::string cameraDevicePath; // Empty for non-camera layers
    float resizeScale{1.0f};      // For camera layers resize functionality

    // For image layers
    std::shared_ptr<Image> img{nullptr};

    // For GIF layers
    std::shared_ptr<Gif> gif{nullptr}; // Added for GIFs
    size_t gifFrameIndex{0};           // Track current frame for GIFs

    // For video layers
    std::shared_ptr<VideoLoader> video{nullptr}; // Added for videos
    std::shared_ptr<Image> currentVideoFrame{nullptr}; // Current video frame
    size_t videoFrameIndex{0};           // Track current frame for videos

    // For text layers - store minimal info for regeneration if needed
    std::string textContent; // Store text content for potential regeneration
    int layerNumber = 0;     // Layer number for text layers
    float x = 0.0f;
    float y = 0.0f;

    // Text overlay system - attached to every layer
    struct TextOverlay
    {
        bool enabled = true;  // Show/hide layer name overlay
        float offsetX = 5.0f; // Relative offset from layer position
        float offsetY = 5.0f;
        std::shared_ptr<Image> cachedImage; // Cached text overlay image
        bool needsRefresh = true;           // Flag to regenerate overlay

        // Get absolute position based on layer position
        float getAbsoluteX(float layerX) const { return layerX + offsetX; }
        float getAbsoluteY(float layerY) const { return layerY + offsetY; }
    } textOverlay;

    // Helper: get layer number (delegates to image/gif/video if present)
    int getLayerNumber() const
    {
        if (type == LayerType::IMAGE && img)
        {
            return img->info.layer;
        }
        if (type == LayerType::GIF && gif && !gif->frames().empty())
        {
            return gif->frames()[0]->info.layer;
        }
        if (type == LayerType::VIDEO && video && currentVideoFrame)
        {
            return currentVideoFrame->info.layer;
        }
        // For text layers, return layerNumber if set, otherwise default to 0 (0-based index)
        // For gifs/videos with no frames, also default to 0
        return layerNumber;
    }

    // Helper: set layer number (delegates to image/gif/video if present)
    void setLayerNumber(int n)
    {
        if (type == LayerType::IMAGE && img)
        {
            img->info.layer = n;
        }
        else if (type == LayerType::GIF && gif && !gif->frames().empty())
        {
            gif->frames()[0]->info.layer = n;
        }
        else if (type == LayerType::VIDEO && video && currentVideoFrame)
        {
            currentVideoFrame->info.layer = n;
        }
        else if (type == LayerType::TEXT)
        {
            layerNumber = n;
        }
    }

    // Helper: get layer name
    std::string getLayerName() const
    {
        if (type == LayerType::IMAGE && img)
        {
            return img->info.filename;
        }
        if (type == LayerType::GIF && gif && !gif->frames().empty())
        {
            return gif->frames()[0]->info.filename;
        }
        if (type == LayerType::GIF && gif && gif->frames().empty())
        {
            // If GIF failed to decode frames, fall back to the original filename stored in the Gif wrapper
            return gif->getFilename();
        }
        if (type == LayerType::VIDEO && video)
        {
            return video->getMetadata().filename;
        }
        if (type == LayerType::TEXT)
        {
            // When text content is empty, return a simple default name used by tests/UI
            return textContent.empty() ? std::string("Text Layer") : textContent;
        }
        return "Unknown Layer " + std::to_string(id);
    }

    // Update layer animation (for GIF and video layers)
    void updateAnimation()
    {
        if (type == LayerType::GIF && gif && !gif->frames().empty())
        {
            gifFrameIndex = (gifFrameIndex + 1) % gif->frames().size();
            dirty = true;
        }
        else if (type == LayerType::VIDEO && video)
        {
            // Get next frame from video
            std::unique_ptr<Image> nextFrame;
            if (video->getNextFrame(nextFrame))
            {
                currentVideoFrame = std::move(std::shared_ptr<Image>(nextFrame.release()));
                videoFrameIndex++;
                dirty = true;
            }
            else
            {
                // End of video, loop back to beginning
                video->reset();
                videoFrameIndex = 0;
                // Try to get the first frame again
                if (video->getNextFrame(nextFrame))
                {
                    // TODO: This is duplicated.
                    currentVideoFrame = std::move(std::shared_ptr<Image>(nextFrame.release()));
                    videoFrameIndex++;
                    dirty = true;
                }
            }
        }
    }

    // Text overlay management
    void setPosition(float newX, float newY)
    {
        x = newX;
        y = newY;
        dirty = true;
        textOverlay.needsRefresh = true; // Invalidate overlay when position changes
    }

    void moveBy(float deltaX, float deltaY)
    {
        x += deltaX;
        y += deltaY;
        dirty = true;
        textOverlay.needsRefresh = true; // Invalidate overlay when position changes
    }

    void invalidateTextOverlay() { textOverlay.needsRefresh = true; }

    void setTextOverlayEnabled(bool enabled)
    {
        if (textOverlay.enabled != enabled)
        {
            textOverlay.enabled = enabled;
            textOverlay.needsRefresh = true;
        }
    }

    void setSelected(bool isSelected)
    {
        if (selected != isSelected)
        {
            selected = isSelected;
            textOverlay.needsRefresh = true; // Refresh text overlay for opacity change
        }
    }

    // Helper: get textureId (delegates to image/gif if present)
    unsigned int getTextureId() const
    {
        if ((type == LayerType::IMAGE || type == LayerType::TEXT) && img)
        {
            return img->info.textureId;
        }
        if (type == LayerType::GIF && gif && !gif->frames().empty())
        {
            return gif->frames()[gifFrameIndex % gif->frames().size()]->info.textureId;
        }
        return 0;
    }

    size_t getSize() const
    {
        if ((type == LayerType::IMAGE || type == LayerType::TEXT) && img)
        {
            return img->size();
        }
        if (type == LayerType::GIF && gif)
        {
            return gif->getSize();
        }
        return 0;
    }

    unsigned long getWidth() const
    {
        if (type == LayerType::IMAGE && img)
        {
            return img->info.width;
        }
        if (type == LayerType::GIF && gif && !gif->frames().empty())
        {
            return gif->frames()[0]->info.width;
        }
        if (type == LayerType::TEXT && img)
        {
            // Text layers store their rendered image in img
            return img->info.width;
        }
        return 0;
    }

    unsigned long getHeight() const
    {
        if (type == LayerType::IMAGE && img)
        {
            return img->info.height;
        }
        if (type == LayerType::GIF && gif && !gif->frames().empty())
        {
            return gif->frames()[0]->info.height;
        }
        if (type == LayerType::TEXT && img)
        {
            // Text layers store their rendered image in img
            return img->info.height;
        }
        return 0;
    }
    ImageFormat getFormat() const
    {
        if (type == LayerType::IMAGE && img)
        {
            return img->info.format;
        }
        if (type == LayerType::GIF && gif && !gif->frames().empty())
        {
            return gif->frames()[0]->info.format;
        }
        if (type == LayerType::TEXT && img)
        {
            // Text layers store their rendered image in img
            return img->info.format;
        }
        return ImageFormat::UNKNOWN;
    }

    // Factory method: Create a text image using text_draw.h
    static std::shared_ptr<Image>
    createTextImage(const std::string& text, ImU32 textColor = IM_COL32_WHITE, int textScale = 1,
                    bool useBackground = false, ImU32 backgroundColor = IM_COL32_BLACK, bool centerText = false,
                    bool multilineText = false,
                    int textHAlignment = 1, // 0=LEFT, 1=CENTER, 2=RIGHT
                    int textVAlignment = 1, // 0=TOP, 1=MIDDLE, 2=BOTTOM
                    int textPadding = 2,
                    int boundingWidth = 0, // auto-size
                    int boundingHeight = 0 // auto-size
    );

    // Thread-safe ID generator
    static size_t getNextId()
    {
        static std::atomic<size_t> nextId{0};
        return nextId.fetch_add(1, std::memory_order_relaxed);
    }
};

class LayerManager
{
  public:
    LayerManager();
    ~LayerManager() = default;

    // Layer management
    void addLayer(const Layer& layer);
    void removeLayer(size_t layerId);
    void clearLayers();
    std::vector<Layer>& getLayers();
    const std::vector<Layer>& getLayers() const;
    Layer* getLayerByNumber(int layerNumber);
    Layer* getLayerByName(const std::string& layerName);
    Layer* getLayerByCameraDevicePath(const std::string& devicePath);
    void sortLayers(); // Sort by layer number ascending

    // Overlay cache
    void markDirty();     // Marks all layers as dirty
    bool isDirty() const; // Returns true if any layer is dirty
    void setLayerDirty(int layerNumber, bool dirty);

    // Invalidate all textures
    void invalidateTextures();
  private:
    std::vector<Layer> layers_;
};

} // namespace linuxface

#endif // LAYERMANAGER_H
