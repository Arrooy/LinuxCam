#include "LinuxFace/web/videoStreamController.h"

#include <json/json.h>

#include "LinuxFace/common.h"
#include "LinuxFace/web/webrtcTransport.h"
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

void videoStreamController::setWebRTCTransport(std::shared_ptr<WebRTCTransport> transport)
{
    std::lock_guard<std::mutex> lock(webrtcMutex_);
    webrtcTransport_ = transport;
}

void videoStreamController::handleNewMessage(const drogon::WebSocketConnectionPtr& wsConnPtr, std::string&& message,
                                             const drogon::WebSocketMessageType& type)
{
    // Binary messages: frame data or target images
    if (type == drogon::WebSocketMessageType::Binary)
    {
        // Fast path: most binary messages are regular frames
        // Check first character to avoid string comparison overhead
        if (message.size() > 13 && message[0] == 'T' && message.compare(0, 13, "TARGET_IMAGE:") == 0)
        {
            // Extract image data after prefix
            std::vector<uint8_t> imageData(message.begin() + 13, message.end());
            common::logInfo("videoStreamController - Received target image: %zu bytes", imageData.size());

            if (onTargetImageReceived_)
            {
                onTargetImageReceived_(imageData);
            }
            return;
        }

        // Regular frame data (most common case)
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
        return;
    }

    // Text messages: commands and signaling
    // Early exit for empty messages
    if (message.empty())
    {
        return;
    }

    // Use first character to quickly route to appropriate handler
    switch (message[0])
    {
        case 'W': // WEBRTC:
            if (message.size() > 8 && message.compare(0, 8, "WEBRTC:") == 0)
            {
                handleWebRTCSignaling(wsConnPtr, message.substr(8));
                return;
            }
            break;

        case 'Q': // QUALITY:
            if (message.size() > 8 && message.compare(0, 8, "QUALITY:") == 0)
            {
                try
                {
                    int quality = std::stoi(message.substr(8));
                    common::logInfo("videoStreamController - Received quality setting: %d", quality);

                    if (onQualityChanged_)
                    {
                        onQualityChanged_(quality);
                    }
                }
                catch (const std::exception& e)
                {
                    common::logError("videoStreamController - Failed to parse quality value: %s", e.what());
                }
                return;
            }
            break;

        case 'R': // RESOLUTION_CHANGE
            if (message == "RESOLUTION_CHANGE")
            {
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
                return;
            }
            break;

        default:
            break;
    }

    common::logInfo("videoStreamController - Received unhandled text message: %s", message.c_str());
}

void videoStreamController::handleNewConnection(const drogon::HttpRequestPtr& req,
                                                const drogon::WebSocketConnectionPtr& wsConnPtr)
{
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    ClientState state;
    state.clientId = "client_" + std::to_string(connections_.size() + 1);
    connections_[wsConnPtr] = std::move(state);

    common::logInfo("videoStreamController - New WebSocket connection from: %s", req->peerAddr().toIpPort().c_str());
}

void videoStreamController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& wsConnPtr)
{
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(wsConnPtr);
    if (it != connections_.end())
    {
        common::logInfo("videoStreamController - Connection closed: %s", it->second.clientId.c_str());
        
        // Clean up WebRTC peer connection if exists
        std::shared_ptr<WebRTCTransport> transport;
        {
            std::lock_guard<std::mutex> webrtcLock(webrtcMutex_);
            transport = webrtcTransport_;
        }
        
        if (transport)
        {
            transport->removePeerConnection(it->second.clientId);
        }
        
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
            common::logError("videoStreamController - Failed to send frame to %s: %s", state.clientId.c_str(),
                             e.what());
        }
    }
}

bool videoStreamController::hasActiveConnections()
{
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    for (auto& [conn, state] : connections_)
    {
        if (conn->connected())
        {
            return true;
        }
    }
    
    return false;
}

void videoStreamController::handleWebRTCSignaling(const drogon::WebSocketConnectionPtr& wsConnPtr,
                                                   const std::string& message)
{
    std::shared_ptr<WebRTCTransport> transport;
    {
        std::lock_guard<std::mutex> lock(webrtcMutex_);
        transport = webrtcTransport_;
    }

    if (!transport)
    {
        common::logError("videoStreamController - WebRTC transport not configured");
        return;
    }

    try
    {
        // Parse JSON message
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        std::istringstream stream(message);
        if (!Json::parseFromStream(builder, stream, &root, &errs))
        {
            common::logError("videoStreamController - Failed to parse WebRTC signaling message: %s", errs.c_str());
            return;
        }

        std::string type = root["type"].asString();
        std::string peerId = root["peerId"].asString();

        if (peerId.empty())
        {
            common::logError("videoStreamController - Missing peerId in WebRTC signaling");
            return;
        }

        Json::Value response;
        response["type"] = type + "_response";
        response["peerId"] = peerId;

        if (type == "offer_request")
        {
            // Client requesting an offer
            std::string localSdp = transport->createPeerConnection(peerId);
            if (localSdp.empty())
            {
                response["success"] = false;
                response["error"] = "Failed to create peer connection";
            }
            else
            {
                response["success"] = true;
                response["sdp"] = localSdp;
            }
        }
        else if (type == "answer")
        {
            // Client sending answer
            std::string sdp = root["sdp"].asString();
            bool success = transport->processAnswer(peerId, sdp);
            response["success"] = success;
            if (!success)
            {
                response["error"] = "Failed to process answer";
            }
        }
        else if (type == "ice_candidate")
        {
            // Client sending ICE candidate
            std::string candidate = root["candidate"].asString();
            std::string mid = root["mid"].asString();
            bool success = transport->processIceCandidate(peerId, candidate, mid);
            response["success"] = success;
            if (!success)
            {
                response["error"] = "Failed to process ICE candidate";
            }
        }
        else
        {
            common::logWarn("videoStreamController - Unknown WebRTC signaling type: %s", type.c_str());
            response["success"] = false;
            response["error"] = "Unknown signaling type";
        }

        // Send response
        Json::StreamWriterBuilder writerBuilder;
        std::string responseStr = "WEBRTC:" + Json::writeString(writerBuilder, response);
        wsConnPtr->send(responseStr);
    }
    catch (const std::exception& e)
    {
        common::logError("videoStreamController - Exception in WebRTC signaling: %s", e.what());
    }
}

} // namespace web
} // namespace linuxface
