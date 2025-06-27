#ifndef LAYERMANAGER_H
#define LAYERMANAGER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include "imgui.h"
#include "LinuxFace/Image/image.h" // For Image definition

// Forward declaration to avoid include issues
namespace linuxface { class Image; }

namespace linuxface {

// Layer type for compositing
enum class LayerType {
    Image,
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

    // For text layers
    std::string textContent;
    ImU32 textColor = IM_COL32_WHITE;
    float fontSize = 16.0f;
    float x = 0.0f;
    float y = 0.0f;

    // Helper: get layer number (delegates to image if present)
    int getLayerNumber() const {
        if (type == LayerType::Image && img) return static_cast<int>(img->info.layer);
        // For text, use text layer number
        return 1;
    }

    // Helper: set layer number (delegates to image if present)
    void setLayerNumber(int n) {
        if (type == LayerType::Image && img) img->info.layer = n;
        // For text, you can add a member if needed
    }

    // Helper: get layer name
    std::string getLayerName() const {
        if (type == LayerType::Image && img) return img->info.filename;
        // For text, use text layer number
        return textContent.empty() ? "Text Layer" : textContent;
    }

    // Helper: get textureId (delegates to image if present)
    unsigned int getTextureId() const {
        if (type == LayerType::Image && img) return img->info.textureId;
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
