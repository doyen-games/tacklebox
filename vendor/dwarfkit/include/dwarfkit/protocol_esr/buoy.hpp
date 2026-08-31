// Port of greymass/buoy-client (@greymass/buoy 1.0.x): send over HTTP POST,
// receive over WebSocket, both through the transport interfaces. The
// EventEmitter Listener becomes a blocking receive loop that handles heartbeat
// frames and reconnects with the upstream backoff curve (BLUEPRINT.md 6.5).
#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <dwarfkit/core/cancel.hpp>
#include <dwarfkit/core/json.hpp>
#include <dwarfkit/core/result.hpp>
#include <dwarfkit/transport/fetch_provider.hpp>
#include <dwarfkit/transport/websocket_provider.hpp>

namespace dwarfkit::buoy {

// Result of a send call.
enum class SendResult {
    // Message was sent but not yet delivered.
    buffered,
    // Message was delivered to at least 1 listener on the channel.
    delivered,
};

// Options for the send method.
struct SendOptions {
    // The buoy channel to post to, minimum 10 chars, usually a uuid string.
    std::string channel;
    // The buoy service url, e.g. 'https://cb.anchor.link'.
    std::string service;
    // How long to wait for delivery. With requireDelivery the call errors if
    // the message is not delivered within this window.
    std::optional<std::chrono::milliseconds> timeout;
    // Whether to only return on a guaranteed delivery. Needs timeout.
    bool requireDelivery = false;
    // HTTP transport to use (upstream falls back to global fetch).
    FetchProvider* fetch = nullptr;
};

// Send a message to the channel. Overloads cover the upstream SendData union
// (string | Uint8Array | JSON value); the const char* one exists because a
// string literal converts equally well to string_view and json.
Result<SendResult> send(std::string_view message, const SendOptions& options);
Result<SendResult> send(std::span<const uint8_t> message, const SendOptions& options);
Result<SendResult> send(const json& message, const SendOptions& options);
inline Result<SendResult> send(const char* message, const SendOptions& options) {
    return send(std::string_view(message), options);
}

// Receive encoding for incoming messages (kept for name fidelity; the blocking
// API returns text and callers parse JSON themselves).
enum class ListenerEncoding {
    binary,
    text,
    json,
};

struct ListenerOptions {
    // The buoy channel to listen to.
    std::string channel;
    // The buoy service url.
    std::string service;
    // WebSocket transport to use (upstream falls back to global WebSocket).
    WebSocketProvider* webSocket = nullptr;
};

// Options for the receive method.
struct ReceiveOptions {
    std::string channel;
    std::string service;
    WebSocketProvider* webSocket = nullptr;
    // How long to wait before giving up.
    std::optional<std::chrono::milliseconds> timeout;
};

// A buoy channel listener. Instantiate one to receive multiple messages over
// the same channel; use receive() for a single message.
class Listener {
public:
    // Errors when service, channel or the websocket provider are missing.
    static Result<Listener> make(const ListenerOptions& options);

    // ws url of the channel: service (http -> ws, no trailing /) + /channel?v=2
    const std::string& url() const { return url_; }

    Result<void> connect();
    void disconnect();
    bool isConnected() const { return connected_; }

    // Block until one message arrives. Buoy heartbeat frames (42 42 01 seq)
    // are acked (42 42 02 seq) and skipped. Socket errors trigger a reconnect
    // with the upstream backoff curve; the last socket error is attached to a
    // timeout the way MessageError carries underlyingError upstream.
    Result<Bytes> receiveMessage(std::optional<std::chrono::milliseconds> timeout,
                                 CancelToken token = {});

private:
    Listener() = default;
    std::string url_;
    WebSocketProvider* webSocket_ = nullptr;
    bool connected_ = false;
};

// Receive a single message from a buoy channel as text.
Result<std::string> receive(const ReceiveOptions& options, CancelToken token = {});

// Exponential backoff that caps off at 5s after 10 tries (internal, exposed
// for tests).
std::chrono::milliseconds backoff(int tries);

}  // namespace dwarfkit::buoy
