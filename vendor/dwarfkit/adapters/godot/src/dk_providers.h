// Dwarfkit transports/storage over Godot subsystems (BLUEPRINT.md 8.2),
// polled from the kit worker thread, never the main thread.
#pragma once

#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/web_socket_peer.hpp>

#include <dwarfkit/session.hpp>
#include <dwarfkit/transport/fetch_provider.hpp>
#include <dwarfkit/transport/websocket_provider.hpp>

namespace dwarfkit_godot {

// HTTPClient request driven by polling from the worker thread.
class DkGodotFetchProvider final : public dwarfkit::FetchProvider {
public:
    dwarfkit::Result<dwarfkit::FetchResponse> fetch(
        const dwarfkit::FetchRequest& request) override;
};

// WebSocketPeer polled from the worker thread.
class DkGodotWebSocketProvider final : public dwarfkit::WebSocketProvider {
public:
    dwarfkit::Result<void> connect(std::string_view url) override;
    dwarfkit::Result<dwarfkit::Bytes> receive(std::chrono::milliseconds timeout,
                                              dwarfkit::CancelToken token) override;
    dwarfkit::Result<void> send(std::span<const uint8_t> data) override;
    void close() override;

private:
    godot::Ref<godot::WebSocketPeer> peer_;
};

// FileSessionStorage under user://dwarfkit/ (resolved to the OS path).
std::shared_ptr<dwarfkit::SessionStorage> MakeGodotStorage();

}  // namespace dwarfkit_godot
