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

// Forward declarations
class wsInputDevice;

namespace web
{

// Forward declaration for WebRTC transport
class WebRTCTransport;

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

    // Set WebRTC transport for signaling
    void setWebRTCTransport(std::shared_ptr<WebRTCTransport> transport);

    // Send processed frame to all connected clients (JPEG/WebSocket)
    void sendProcessedFrame(const std::vector<uint8_t>& jpegData);

    // Check if there are any active WebSocket connections
    bool hasActiveConnections();

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
    };

    void handleWebRTCSignaling(const drogon::WebSocketConnectionPtr& wsConnPtr, const std::string& message);

    std::mutex connectionsMutex_;
    std::unordered_map<drogon::WebSocketConnectionPtr, ClientState> connections_;

    std::shared_ptr<wsInputDevice> inputDevice_;
    std::mutex deviceMutex_;

    std::shared_ptr<WebRTCTransport> webrtcTransport_;
    std::mutex webrtcMutex_;

    TargetImageCallback onTargetImageReceived_;
    QualityChangedCallback onQualityChanged_;
};

} // namespace web
} // namespace linuxface
