#ifndef CAMERA_H
#define CAMERA_H

#include <linux/videodev2.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <unordered_map>

#include "FunnyFace/JPEGManager.h"
#include "FunnyFace/inputWebcam.h"
#include "FunnyFace/v4l2loopbackWritter.h"
#include "FunnyFace/profiler.h"
#include "FunnyFace/webcam.h"

namespace funnyface
{

class CameraManager
{
  public:
    CameraManager();
    ~CameraManager();


    bool addCamera(std::shared_ptr<Webcam> camera);
    bool removeCamera(std::shared_ptr<Webcam> camera);
    bool updateCamera(std::shared_ptr<Webcam> camera);

    bool updateInput(std::unique_ptr<Image>& image);
    bool updateOutput(std::unique_ptr<Image>& outputImage);

    // TODO: Move to cpp.
    std::vector<std::shared_ptr<Webcam>> getWebcams() const
    {
        std::vector<std::shared_ptr<Webcam>> result;
        result.reserve(inWebcam_.size() + outWebcam_.size()); // Optional, improves performance
        result.insert(result.end(), inWebcam_.begin(), inWebcam_.end());
        result.insert(result.end(), outWebcam_.begin(), outWebcam_.end());
        return result;
    }

    void shutdown();
  private:
    std::vector<std::shared_ptr<InputWebcam>> inWebcam_;
    std::vector<std::shared_ptr<V4L2LoopbackWriter>> outWebcam_;
    // std::unordered_map<int, int> connections_;
};


template <typename T>
bool addCameraImpl(std::vector<std::shared_ptr<T>>& container, std::shared_ptr<T> camera)
{
    const std::string& devicePath = camera->getDevicePath();

    auto it = std::find_if(container.begin(), container.end(),
                           [&devicePath](const std::shared_ptr<T>& cam) { return cam->getDevicePath() == devicePath; });

    if (it != container.end())
    {
        common::log_error("CameraManager::addCamera - Camera with device path %s already exists.", devicePath.c_str());
        return false; // Already exists
    }

    container.push_back(std::move(camera));
    return true;
}

template <typename T>
bool removeCameraImpl(std::vector<std::shared_ptr<T>>& container, const std::string& devicePath)
{
    auto it = std::remove_if(container.begin(), container.end(), [&devicePath](const std::shared_ptr<T>& cam)
                             { return cam->getDevicePath() == devicePath; });

    if (it != container.end())
    {
        container.erase(it, container.end());
        return true;
    }
    common::log_error("CameraManager::removeCamera - Camera with device path %s not found.", devicePath.c_str());
    return false;
}

template <typename T>
bool updateCameraImpl(std::vector<std::shared_ptr<T>>& container, std::shared_ptr<T> camera)
{
    const std::string& devicePath = camera->getDevicePath();

    auto it = std::find_if(container.begin(), container.end(),
                           [&devicePath](const std::shared_ptr<T>& cam) { return cam->getDevicePath() == devicePath; });

    if (it != container.end())
    {
        container.erase(it);

        if (!camera->setupDevice())
        {
            common::log_error("Failed to setup camera device: %s", devicePath.c_str());
            return false;
        }

        if (!camera->start())
        {
            common::log_error("Failed to start camera device: %s", devicePath.c_str());
            return false;
        }

        container.push_back(std::move(camera));
        return true;
    }

    return false;
}


} // namespace funnyface
#endif // CAMERA_H
