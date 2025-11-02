#ifndef WEBSOCKET_INPUT_DEVICE_H
#define WEBSOCKET_INPUT_DEVICE_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/codec.h"
#include "LinuxFace/webcam.h"
#include "config.hpp"

namespace linuxface
{

// WebSocket input device that behaves like a camera source.
class wsInputDevice : public Webcam
{
  public:
    explicit wsInputDevice(const WebServerConfig& config);
    ~wsInputDevice() override;

    bool setupDevice() override;
    bool start() override;
    bool stop() override;
    bool isRunning() override;

    bool getImage(std::unique_ptr<Image>& outImage);

    // Called by videoStreamController to push new frames from the browser.
    void pushFrame(const std::vector<uint8_t>& jpegData);

  private:
    void processFrameQueue();
    bool ready_{false};

    WebServerConfig webServerConfig_;
    std::atomic<bool> running_{false};

    std::queue<std::vector<uint8_t>> frameQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    static constexpr size_t MAX_QUEUE_SIZE = 5;

    std::unique_ptr<Image> latestImage_;
    std::mutex imageMutex_;

    std::unique_ptr<Decoder> decoder_;
    std::thread processingThread_;
};

} // namespace linuxface

#endif // WEBSOCKET_INPUT_DEVICE_H
