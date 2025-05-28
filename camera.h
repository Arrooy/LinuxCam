#ifndef CAMERA_H
#define CAMERA_H

#include <linux/videodev2.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>

#include "JPEGManager.h"
#include <thread>
#include "queue.hpp"

namespace funnyface
{
struct Buffer
{
    size_t length;
    void* start;
};

class CameraManager
{
  public:
    CameraManager() = default;
    ~CameraManager();

    bool initialize();

    bool update(std::function<void(Image&)> paint);
    bool record();

    // VIDEO_IN - VIDEO_WIDTH_IN - VIDEO_HEIGHT_IN
    bool configureInputCamera(const char* in_device, unsigned int width, unsigned int height);
    bool configureInputBuffers(unsigned int buffer_count);

    // VIDEO_OUT - VIDEO_WIDTH_OUT - VIDEO_HEIGHT_OUT
    bool configureVirtualOuputCamera(const char* out_device, unsigned int width, unsigned int height);
    
    // TODO: FIXME: Improve signal and shuting down mechanism. Run valgrind too. 
    bool is_alive() { return keepRunning_;}
    void shutdown() { keepRunning_ = false;}
  private:
    static void intHandler(int sig) { keepRunning_ = false; }
    static void cleanupAndExit(int sig)
    {
        // TODO: In case of a problem, close the file descriptors
        // Stop the streaming of the input device.

        exit(sig);
    }



    bool getCapabilities(int fd, v4l2_capability& cap);
    void logFormat(v4l2_format vid_format);

    void cleanupBuffers(unsigned int index);

    bool stopInputStreaming();

    Buffer* buffers_;
    struct v4l2_requestbuffers bufrequest_;

    std::shared_ptr<JPEGManager> jpegManager_{nullptr};

    int input_fd_;
    int output_fd_;
    static std::atomic<bool> keepRunning_;


    std::thread recordThread_;
    std::thread processingThread_;
    SafeQueue<Image> imageQueue_;
};

} // namespace funnyface
#endif // CAMERA_H
