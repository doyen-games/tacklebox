// The WebSocket transport boundary (BLUEPRINT.md 5.3). Engines implement this;
// CurlWebSocketProvider is the default outside engines when DK_WITH_CURL is on.
#pragma once

#include <chrono>
#include <span>
#include <string_view>

#include <dwarfkit/antelope/chain/bytes.hpp>
#include <dwarfkit/core/cancel.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

// receive() blocks until a message arrives, the timeout elapses, the token is
// cancelled, or the socket fails. Error contract (kind, details["code"]):
//   timeout        Transport, "E_TIMEOUT"
//   cancelled      Canceled
//   socket failure Transport, "E_NETWORK" (reconnect by calling connect again)
struct WebSocketProvider {
    virtual Result<void> connect(std::string_view url) = 0;
    virtual Result<Bytes> receive(std::chrono::milliseconds timeout, CancelToken token) = 0;
    virtual Result<void> send(std::span<const uint8_t> data) = 0;
    virtual void close() = 0;
    virtual ~WebSocketProvider() = default;
};

}  // namespace dwarfkit
