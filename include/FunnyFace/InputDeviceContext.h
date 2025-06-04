#ifndef INPUT_DEVICE_CONTEXT_H
#define INPUT_DEVICE_CONTEXT_H

#include <linux/videodev2.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "FunnyFace/JPEGManager.h"
#include "FunnyFace/image.h"
#include "FunnyFace/profiler.h"

namespace funnyface
{

struct Buffer
{
    size_t length;
    void* start;
};

struct FrameSize
{
    unsigned int width;
    unsigned int height;
};

struct Format
{
    std::string description;
    unsigned int pixelformat;
    std::vector<FrameSize> sizes;
};

struct CameraCapabilities
{
    std::string driver;
    std::string card;
    std::string bus_info;
    std::vector<Format> formats;
};

struct CapturingDevice
{
    std::string name;
    std::string device_path;
    int fd = -1;
    unsigned int width = 0u;
    unsigned int height = 0u;
    unsigned int buffer_count = 0u;
    TJSAMP subsampling = TJSAMP_420; // Default subsampling
    CameraCapabilities caps;
};

class InputDeviceContext
{
  public:
    InputDeviceContext();
    ~InputDeviceContext();

    // Device configuration
    bool setupDevice(const CapturingDevice& device);
    bool reconfigureDevice(const CapturingDevice& device);

    // Camera operations
    bool openCamera();
    bool configureCameraFormat();
    bool configureBuffers();
    bool startStreaming();
    bool stopStreaming();
    void cleanup();

    // Recording control
    bool startRecording(std::shared_ptr<JPEGManager> jpegManager, Profiler& profiler);
    void stopRecording();
    bool isActivelyRecording() const { return isRecording.load(); }

    // Image access
    Image* getCurrentImage();
    bool isImageReady() const { return currentImage.beingUsed_; }

    // Device information
    const CapturingDevice& getDevice() const { return device_; }
    CapturingDevice& getDevice() { return device_; }

  private:
    bool getCameraCapabilities();

    void recordingLoop(std::shared_ptr<JPEGManager> jpegManager, Profiler& profiler);
    bool requeueFrame(struct v4l2_buffer& buf);
    void cleanupBuffers();


    void logFormat(v4l2_format vid_format);

    // Device state
    CapturingDevice device_;

    Buffer* buffers = nullptr;
    struct v4l2_requestbuffers bufrequest;

    // Threading
    std::thread recordThread;
    std::atomic<bool> isRecording{false};

    // Image state
    Image currentImage;
    bool readJPEGHeader{true};
    unsigned int decodingFailureCount{0u};
    TJImageDescription cameraInputInfo;
};

} // namespace funnyface

#endif // INPUT_DEVICE_CONTEXT_H
