#ifndef LAYERMANAGER_H
#define LAYERMANAGER_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "LinuxFace/Image/gif.h"
#include "LinuxFace/Image/image.h"
#include "imgui.h"

namespace linuxface
{

// Layer type for compositing
enum class LayerType
{
    Image,
    Gif,
    Text
};

struct Layer
{
    LayerType type;
    std::string name;
    bool selected{false};
    bool dirty{true};
    bool isBaseLayer{false};

    // Unique identifier for this layer instance
    size_t id = 0;
    static size_t next_id;

    // For image layers
    std::shared_ptr<Image> img{nullptr};

    // For GIF layers
    std::shared_ptr<Gif> gif{nullptr}; // Added for GIFs
    size_t gifFrameIndex{0};           // Track current frame for GIFs

    // For text layers
    std::string textContent;
    ImU32 textColor = IM_COL32_WHITE;
    float fontSize = 16.0f;
    float x = 0.0f;
    float y = 0.0f;

    // Helper: get layer number (delegates to image/gif if present)
    inline int getLayerNumber() const
    {
        if (type == LayerType::Image && img)
        {
            return static_cast<int>(img->info.layer);
        }
        if (type == LayerType::Gif && gif && !gif->frames().empty())
        {
            return static_cast<int>(gif->frames()[0]->info.layer);
        }
        // For text, use text layer number
        return 1;
    }

    // Helper: set layer number (delegates to image/gif if present)
    inline void setLayerNumber(int n)
    {
        if (type == LayerType::Image && img)
        {
            img->info.layer = n;
        }
        if (type == LayerType::Gif && gif && !gif->frames().empty())
        {
            gif->frames()[0]->info.layer = n;
        }
        // For text, you can add a member if needed
    }

    // Helper: get layer name
    inline std::string getLayerName() const
    {
        if (type == LayerType::Image && img)
        {
            return img->info.filename;
        }
        if (type == LayerType::Gif && gif)
        {
            return gif->getFilename();
        }
        // For text, use text layer number
        return textContent.empty() ? "Text Layer" : textContent;
    }

    // Helper: get textureId (delegates to image/gif if present)
    inline unsigned int getTextureId() const
    {
        if (type == LayerType::Image && img)
        {
            return img->info.textureId;
        }
        if (type == LayerType::Gif && gif && !gif->frames().empty())
        {
            return gif->frames()[gifFrameIndex % gif->frames().size()]->info.textureId;
        }
        return 0;
    }

    inline size_t getSize() const
    {
        if (type == LayerType::Image && img)
        {
            return img->size();
        }
        if (type == LayerType::Gif && gif)
        {
            return gif->getSize();
        }
        return 0;
    }

    inline unsigned long getWidth() const
    {
        if (type == LayerType::Image && img)
        {
            return img->info.width;
        }
        if (type == LayerType::Gif && gif && !gif->frames().empty())
        {
            return gif->frames()[0]->info.width;
        }
        return 0;
    }

    inline unsigned long getHeight() const
    {
        if (type == LayerType::Image && img)
        {
            return img->info.height;
        }
        if (type == LayerType::Gif && gif && !gif->frames().empty())
        {
            return gif->frames()[0]->info.height;
        }
        return 0;
    }
    inline ImageFormat getFormat() const
    {
        if (type == LayerType::Image && img)
        {
            return img->info.format;
        }
        if (type == LayerType::Gif && gif && !gif->frames().empty())
        {
            return gif->frames()[0]->info.format;
        }
        return ImageFormat::UNKNOWN;
    }
};
// Initialize static member
inline size_t Layer::next_id = 0;

class LayerManager
{
  public:
    LayerManager();
    ~LayerManager();

    // Layer management
    void addLayer(const Layer& layer);
    void removeLayer(int layerNumber);
    void clearLayers();
    std::vector<Layer>& getLayers();
    const std::vector<Layer>& getLayers() const;
    Layer* getLayerByNumber(int layerNumber);
    Layer* getLayerByName(const std::string& layerName);
    Layer* getBaseLayer();
    void sortLayers(); // Sort by layer number ascending

    // Overlay cache
    void markDirty();          // Marks all layers as dirty
    bool isDirty() const;      // Returns true if any layer is dirty
    void setDirty(bool dirty); // Sets all layers' dirty flag
    void setLayerDirty(int layerNumber, bool dirty);

  private:
    std::vector<Layer> layers_;
};

} // namespace linuxface

#endif // LAYERMANAGER_H
