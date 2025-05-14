/* -*- c++ -*- */

#ifndef CAMERA_H
#define CAMERA_H

#include <linux/videodev2.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>

#include "JPEGManager.h"


struct Buffer
{
    size_t length;
    void* start;
};

class CameraManager
{
  public:
    CameraManager();

    bool update(std::function<void(funnyface::Image&)> paint);

    // VIDEO_IN - VIDEO_WIDTH_IN - VIDEO_HEIGHT_IN
    bool configureInputCamera(const char* in_device, unsigned int width, unsigned int height);
    bool configureInputBuffers(unsigned int buffer_count);

    // VIDEO_OUT - VIDEO_WIDTH_OUT - VIDEO_HEIGHT_OUT
    bool configureVirtualOuputCamera(const char* out_device, unsigned int width, unsigned int height);

    inline void setJPEGManager(std::shared_ptr<funnyface::JPEGManager> jpegManager) { jpegManager_ = jpegManager; };
    inline int getOutputFd() const { return output_fd_; };

  private:
    static void intHandler(int sig) { keepRunning_ = false; }
    void cleanup_buffers(unsigned int index);

    Buffer* buffers_;
    struct v4l2_requestbuffers bufrequest_;

    std::shared_ptr<funnyface::JPEGManager> jpegManager_;

    int input_fd_;
    int output_fd_;
    static std::atomic<bool> keepRunning_;
};


#endif // CAMERA_H
