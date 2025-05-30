#ifndef CAMERA_H
#define CAMERA_H

#include <linux/videodev2.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>

#include "JPEGManager.h"
#include "profiler.h"
#include "queue.hpp"
namespace funnyface
{

struct Buffer
{
    size_t length;
    void* start;
};

struct CapturingDevice
{
    const char* name = nullptr;
    const char* device_path = nullptr;
    int fd = -1;
    unsigned int width = 0u;
    unsigned int height = 0u;
    unsigned int buffer_count = -1;
    TJSAMP subsampling = TJSAMP_420; // Default subsampling
};

class CameraManager
{
  public:
    CameraManager();
    ~CameraManager();

    void configureInputDevice(const char* in_device, unsigned int width = 640, unsigned int height = 480,
                              unsigned int buffer_count = 2);
    void configureOutputDevice(const char* out_device, unsigned int width = 640, unsigned int height = 480);
    inline void setInputDevice(const CapturingDevice& device) { inputDevice_ = device; }
    inline void setOutputDevice(const CapturingDevice& device) { outputDevice_ = device; }

    bool initialize();
    bool update(std::function<void(Image&)> paint);
    bool record();


    // TODO: FIXME: Improve signal and shuting down mechanism. Run valgrind too.
    bool is_alive() { return keepRunning_; }
    void shutdown() { keepRunning_ = false; }
  private:
    bool configureInputCamera();
    bool configureVirtualOuputCamera();

    bool configureInputBuffers(unsigned int buffer_count);

    static void intHandler(int sig) { keepRunning_ = false; }
    static void cleanupAndExit(int sig)
    {
        // TODO: In case of a problem, close the file descriptors
        // Stop the streaming of the input device.

        exit(sig);
    }


    bool getCapabilities(int fd, v4l2_capability& cap);
    void logFormat(v4l2_format vid_format);
    void logSupportedResolutions(int fd, const char* device_name);

    void cleanupBuffers(unsigned int index);

    bool stopInputStreaming();
    bool requeueFrame(int fd, v4l2_buffer& buf);

    Buffer* buffers_;
    struct v4l2_requestbuffers bufrequest_;

    std::shared_ptr<JPEGManager> jpegManager_{nullptr};
    static std::atomic<bool> keepRunning_;

    CapturingDevice inputDevice_;
    CapturingDevice outputDevice_;

    std::thread recordThread_;
    std::thread processingThread_;
    SafeQueue<Image> imageQueue_; // TODO: delete.
    Image currentImage_;

    Profiler& profiler_;
};

} // namespace funnyface
#endif // CAMERA_H
