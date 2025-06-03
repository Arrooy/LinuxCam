#ifndef CAMERA_H
#define CAMERA_H

#include <linux/videodev2.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>

#include "InputDeviceContext.h"
#include "JPEGManager.h"
#include "profiler.h"
#include "queue.hpp"
namespace funnyface
{

class CameraManager
{
  public:
    CameraManager();
    ~CameraManager();

    void configureInputDevice(const char* in_device, unsigned int width = 640, unsigned int height = 480,
                              unsigned int buffer_count = 2);
    void configureOutputDevice(const char* out_device, unsigned int width = 640, unsigned int height = 480);

    inline void setInputDevice(const CapturingDevice& device) { inputDeviceContext_.getDevice() = device; }
    inline void setOutputDevice(const CapturingDevice& device) { outputDevice_ = device; }
    CapturingDevice& getInputDevice() { return inputDeviceContext_.getDevice(); }
    CapturingDevice& getOutputDevice() { return outputDevice_; }


    bool initialize();
    bool update(std::function<void(Image&)> paint);
    bool record();
    bool getCameraCapabilities(const CapturingDevice& device, CameraCapabilities& outCaps);

    void reconfigureInputCamera();
    void reconfigureOutputCamera();

    // TODO: FIXME: Improve signal and shuting down mechanism. Run valgrind too.
    bool is_alive() { return keepRunning_; }
    void shutdown() { keepRunning_ = false; }
  private:
    bool configureVirtualOuputCamera();

    static void intHandler(int sig) { keepRunning_ = false; }
    static void cleanupAndExit(int sig)
    {
        // TODO: In case of a problem, close the file descriptors
        // Stop the streaming of the input device.

        exit(sig);
    }


    bool getCapabilities(int fd, v4l2_capability& cap);
    void logFormat(v4l2_format vid_format);

    std::shared_ptr<JPEGManager> jpegManager_{nullptr};
    static std::atomic<bool> keepRunning_;

    InputDeviceContext inputDeviceContext_;
    CapturingDevice outputDevice_;

    std::thread processingThread_;
    SafeQueue<Image> imageQueue_; // TODO: delete.

    Profiler& profiler_;
};

} // namespace funnyface
#endif // CAMERA_H
