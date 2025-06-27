#ifndef LAYERMANAGER_H
#define LAYERMANAGER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include "imgui.h"
#include "LinuxFace/Image/image.h"
#include "LinuxFace/Image/gif.h"

namespace linuxface {

// Layer type for compositing
enum class LayerType {
    Image,
    Gif,
    Text
};

struct Layer {
    LayerType type;
    std::string name;
    bool selected{false};
    bool dirty{true};
    bool isBaseLayer{false};

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
    int getLayerNumber() const {
        if (type == LayerType::Image && img) return static_cast<int>(img->info.layer);
        if (type == LayerType::Gif && gif && !gif->frames().empty()) return static_cast<int>(gif->frames()[0]->info.layer);
        // For text, use text layer number
        return 1;
    }

    // Helper: set layer number (delegates to image/gif if present)
    void setLayerNumber(int n) {
        if (type == LayerType::Image && img) img->info.layer = n;
        if (type == LayerType::Gif && gif && !gif->frames().empty()) gif->frames()[0]->info.layer = n;
        // For text, you can add a member if needed
    }

    // Helper: get layer name
    std::string getLayerName() const {
        if (type == LayerType::Image && img) return img->info.filename;
        if (type == LayerType::Gif && gif) return gif->getFilename();
        // For text, use text layer number
        return textContent.empty() ? "Text Layer" : textContent;
    }

    // Helper: get textureId (delegates to image/gif if present)
    unsigned int getTextureId() const {
        if (type == LayerType::Image && img) return img->info.textureId;
        if (type == LayerType::Gif && gif && !gif->frames().empty()) return gif->frames()[gifFrameIndex % gif->frames().size()]->info.textureId;
        return 0;
    }
};

class LayerManager {
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
    void markDirty(); // Marks all layers as dirty
    bool isDirty() const; // Returns true if any layer is dirty
    void setDirty(bool dirty); // Sets all layers' dirty flag
    void setLayerDirty(int layerNumber, bool dirty);

private:
    std::vector<Layer> layers_;
};

} // namespace linuxface

#endif // LAYERMANAGER_H
