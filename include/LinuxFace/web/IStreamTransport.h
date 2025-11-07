#pragma once

#include <memory>
#include <vector>

#include "LinuxFace/Image/image.h"

namespace linuxface
{
namespace web
{

/**
 * Abstract interface for streaming transport mechanisms
 * Allows the application to support multiple streaming protocols (JPEG/WebSocket, WebRTC, etc.)
 */
class IStreamTransport
{
  public:
    virtual ~IStreamTransport() = default;

    /**
     * Start the transport (begin accepting connections, initialize encoder, etc.)
     */
    virtual bool start() = 0;

    /**
     * Stop the transport and cleanup resources
     */
    virtual void stop() = 0;

    /**
     * Check if transport is currently running
     */
    virtual bool isRunning() const = 0;

    /**
     * Submit a frame for encoding and transmission
     * Non-blocking - implementation decides how to handle backpressure
     */
    virtual void submitFrame(const std::unique_ptr<Image>& frame) = 0;

    /**
     * Check if there are active connections/peers
     */
    virtual bool hasActiveConnections() const = 0;

    /**
     * Get transport name for logging
     */
    virtual const char* getName() const = 0;

    /**
     * Statistics structure for monitoring
     */
    struct Stats
    {
        uint64_t framesSubmitted{0};
        uint64_t framesDropped{0};
        uint64_t framesEncoded{0};
        uint64_t framesSent{0};
        uint64_t encodingErrors{0};
    };

    /**
     * Get statistics for monitoring and debugging
     */
    virtual Stats getStats() const = 0;
};

} // namespace web
} // namespace linuxface
