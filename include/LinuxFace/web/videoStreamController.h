#pragma once

#include <drogon/HttpAppFramework.h>
#include <drogon/WebSocketController.h>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace linuxface
{

// Forward declaration
class wsInputDevice;

namespace web
{

class videoStreamController : public drogon::WebSocketController<videoStreamController>
{
  public:
    void handleNewMessage(const drogon::WebSocketConnectionPtr& wsConnPtr, std::string&& message,
                          const drogon::WebSocketMessageType& type) override;

    void
    handleNewConnection(const drogon::HttpRequestPtr& req, const drogon::WebSocketConnectionPtr& wsConnPtr) override;

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& wsConnPtr) override;

    // Set the input device to push frames to
    void setInputDevice(std::shared_ptr<wsInputDevice> device);

    // Send processed frame to all connected clients
    void sendProcessedFrame(const std::vector<uint8_t>& jpegData);

    // Set callback for when target image is received
    using TargetImageCallback = std::function<void(const std::vector<uint8_t>&)>;
    void setTargetImageCallback(TargetImageCallback callback) { onTargetImageReceived_ = callback; }

    // Set callback for when quality setting changes
    using QualityChangedCallback = std::function<void(int)>;
    void setQualityChangedCallback(QualityChangedCallback callback) { onQualityChanged_ = callback; }

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/video");
    WS_PATH_LIST_END

  private:
    struct ClientState
    {
        std::string clientId;
        int pendingFrames{0}; // Not atomic since we're always holding connectionsMutex_
    };

    std::mutex connectionsMutex_;
    std::unordered_map<drogon::WebSocketConnectionPtr, ClientState> connections_;

    std::shared_ptr<wsInputDevice> inputDevice_;
    std::mutex deviceMutex_;

    TargetImageCallback onTargetImageReceived_;
    QualityChangedCallback onQualityChanged_;

    static constexpr int MAX_PENDING_FRAMES_PER_CLIENT = 1; // Keep latency minimal
};

} // namespace web
} // namespace linuxface
