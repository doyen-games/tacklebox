// Default WebSocketProvider over libcurl's WebSocket API (curl 7.86+,
// CONNECT_ONLY=2). Part of the optional dwarfkit_curl target.
#pragma once

#include <dwarfkit/transport/websocket_provider.hpp>

namespace dwarfkit {

class CurlWebSocketProvider final : public WebSocketProvider {
public:
    CurlWebSocketProvider() = default;
    CurlWebSocketProvider(const CurlWebSocketProvider&) = delete;
    CurlWebSocketProvider& operator=(const CurlWebSocketProvider&) = delete;
    ~CurlWebSocketProvider() override;

    Result<void> connect(std::string_view url) override;
    Result<Bytes> receive(std::chrono::milliseconds timeout, CancelToken token) override;
    Result<void> send(std::span<const uint8_t> data) override;
    void close() override;

private:
    void* curl_ = nullptr;  // CURL*
};

}  // namespace dwarfkit
