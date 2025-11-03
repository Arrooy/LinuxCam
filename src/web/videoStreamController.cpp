#include "LinuxFace/web/videoStreamController.h"

#include "LinuxFace/common.h"
#include "LinuxFace/web/wsInputDevice.h"

namespace linuxface
{
namespace web
{

void videoStreamController::setInputDevice(std::shared_ptr<wsInputDevice> device)
{
    std::lock_guard<std::mutex> lock(deviceMutex_);
    inputDevice_ = device;
}

void videoStreamController::handleNewMessage(const drogon::WebSocketConnectionPtr& wsConnPtr, std::string&& message,
                                             const drogon::WebSocketMessageType& type)
{
    if (type == drogon::WebSocketMessageType::Binary)
    {
        // Check if this is a target image (prefixed with "TARGET_IMAGE:")
        const std::string targetPrefix = "TARGET_IMAGE:";
        if (message.size() > targetPrefix.length() && 
            message.compare(0, targetPrefix.length(), targetPrefix) == 0)
        {
            // Extract image data after prefix
            std::vector<uint8_t> imageData(message.begin() + targetPrefix.length(), message.end());
            
            common::logInfo("videoStreamController - Received target image: %zu bytes", imageData.size());
            
            // Notify application to update target image
            if (onTargetImageReceived_)
            {
                onTargetImageReceived_(imageData);
            }
            
            wsConnPtr->send("Target image received");
            return;
        }

        // Regular frame data
        std::shared_ptr<wsInputDevice> device;
        {
            std::lock_guard<std::mutex> lock(deviceMutex_);
            device = inputDevice_;
        }

        if (!device)
        {
            common::logError("videoStreamController - No WebSocket input device configured; dropping frame");
            return;
        }
        std::vector<uint8_t> frameData(message.begin(), message.end());
        device->pushFrame(frameData);
    }
    else
    {
        // Text message - check for commands
        const std::string qualityPrefix = "QUALITY:";
        const std::string resolutionChangePrefix = "RESOLUTION_CHANGE";
        
        if (message.size() > qualityPrefix.length() && 
            message.compare(0, qualityPrefix.length(), qualityPrefix) == 0)
        {
            // Parse quality value
            try
            {
                int quality = std::stoi(message.substr(qualityPrefix.length()));
                common::logInfo("videoStreamController - Received quality setting: %d", quality);
                
                // Notify application to update JPEG quality
                if (onQualityChanged_)
                {
                    onQualityChanged_(quality);
                }
                
                wsConnPtr->send("Quality updated to " + std::to_string(quality));
            }
            catch (const std::exception& e)
            {
                common::logError("videoStreamController - Failed to parse quality value: %s", e.what());
                wsConnPtr->send("Error: Invalid quality value");
            }
            return;
        }
        
        if (message == resolutionChangePrefix)
        {
            // Client signaling resolution change
            common::logInfo("videoStreamController - Resolution change signaled by client");
            
            std::shared_ptr<wsInputDevice> device;
            {
                std::lock_guard<std::mutex> lock(deviceMutex_);
                device = inputDevice_;
            }
            
            if (device)
            {
                device->signalResolutionChange();
            }
            
            wsConnPtr->send("Resolution change acknowledged");
            return;
        }
        
        common::logInfo("videoStreamController - Received text message: %s", message.c_str());
        wsConnPtr->send("Server received: " + message);
    }
}

void videoStreamController::handleNewConnection(const drogon::HttpRequestPtr& req,
                                                const drogon::WebSocketConnectionPtr& wsConnPtr)
{
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    ClientState state;
    state.clientId = "client_" + std::to_string(connections_.size() + 1);
    connections_[wsConnPtr] = std::move(state);

    common::logInfo("videoStreamController - New WebSocket connection from: %s", req->peerAddr().toIpPort().c_str());

    wsConnPtr->send("Connected to LinuxFace video streaming server");
}

void videoStreamController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& wsConnPtr)
{
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(wsConnPtr);
    if (it != connections_.end())
    {
        common::logInfo("videoStreamController - Connection closed: %s", it->second.clientId.c_str());
        connections_.erase(it);
    }
}

void videoStreamController::sendProcessedFrame(const std::vector<uint8_t>& jpegData)
{
    if (jpegData.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(connectionsMutex_);

    if (connections_.empty())
    {
        return;
    }

    std::string_view binaryMessage(reinterpret_cast<const char*>(jpegData.data()), jpegData.size());

    for (auto& [conn, state] : connections_)
    {
        if (!conn->connected())
        {
            continue;
        }

        try
        {
            conn->send(binaryMessage, drogon::WebSocketMessageType::Binary);
        }
        catch (const std::exception& e)
        {
            common::logError("videoStreamController - Failed to send frame to %s: %s", 
                           state.clientId.c_str(), e.what());
        }
    }
}

} // namespace web
} // namespace linuxface
