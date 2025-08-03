#include "LinuxFace/UI/layerManager.h"

#include <algorithm>

namespace linuxface
{

LayerManager::LayerManager() = default;
LayerManager::~LayerManager() = default;

void LayerManager::addLayer(const Layer& layer)
{
    layers_.push_back(layer);
}

void LayerManager::removeLayer(size_t layerId)
{
    layers_.erase(std::remove_if(layers_.begin(), layers_.end(), [layerId](const Layer& l) { return l.id == layerId; }),
                  layers_.end());
}

void LayerManager::clearLayers()
{
    layers_.clear();
}

std::vector<Layer>& LayerManager::getLayers()
{
    return layers_;
}
const std::vector<Layer>& LayerManager::getLayers() const
{
    return layers_;
}

Layer* LayerManager::getLayerByNumber(int layerNumber)
{
    for (auto& l : layers_)
    {
        if (l.getLayerNumber() == layerNumber)
        {
            return &l;
        }
    }
    return nullptr;
}
Layer* LayerManager::getLayerByName(const std::string& layerName)
{
    for (auto& l : layers_)
    {
        if (l.getLayerName() == layerName)
        {
            return &l;
        }
    }
    return nullptr;
}

Layer* LayerManager::getBaseLayer()
{
    for (auto& l : layers_)
    {
        if (l.isBaseLayer)
        {
            return &l;
        }
    }
    return nullptr;
}

void LayerManager::sortLayers()
{
    std::sort(layers_.begin(), layers_.end(),
              [](const Layer& a, const Layer& b) { return a.getLayerNumber() < b.getLayerNumber(); });
}

void LayerManager::markDirty()
{
    for (auto& l : layers_)
    {
        l.dirty = true;
    }
}

bool LayerManager::isDirty() const
{
    for (const auto& l : layers_)
    {
        if (l.dirty)
        {
            return true;
        }
    }
    return false;
}

// Invalidate all textures (mark as dirty)
void LayerManager::invalidateTextures()
{
    for (auto& l : layers_)
    {
        l.dirty = true;
    }
}

void LayerManager::setLayerDirty(int layerNumber, bool dirty)
{
    for (auto& l : layers_)
    {
        if (l.getLayerNumber() == layerNumber)
        {
            l.dirty = dirty;
            break;
        }
    }
}

} // namespace linuxface
