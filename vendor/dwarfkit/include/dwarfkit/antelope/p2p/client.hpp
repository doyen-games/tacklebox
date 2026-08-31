// Port of antelope src/p2p/client.ts. Listener identity: on()/once() return a
// listener id and removeListener takes it (std::function has no equality).
// The heartbeat timer is embedder-supplied via setTimeoutImpl; there is no
// default event loop (see DIVERGENCES.md).
#pragma once

#include <dwarfkit/antelope/p2p/provider.hpp>
#include <dwarfkit/antelope/p2p/types.hpp>

namespace dwarfkit::p2p {

using P2PMessageHandler = std::function<void(const NetMessage& message)>;
// Schedule handler to run once after timeoutMs (the upstream setTimeout).
using SetTimeoutImpl = std::function<void(std::function<void()> handler, int timeoutMs)>;

struct P2PClientOptions {
    // P2P provider to use.
    P2PProvider* provider = nullptr;
    // Heartbeat timeout in milliseconds; unset for no heartbeat.
    std::optional<int> heartbeatTimoutMs;
    // Timer implementation driving the heartbeat (required with a heartbeat).
    SetTimeoutImpl setTimeoutImpl;
};

class P2PClient {
public:
    explicit P2PClient(const P2PClientOptions& options);

    P2PProvider* provider = nullptr;

    // Wrap the message in the net_message variant, encode and write it.
    Result<void> send(const NetMessage& message, P2PHandler done = {});
    void end(P2PHandler cb = {});
    void destroy(const std::optional<Error>& error = {});

    size_t onMessage(P2PMessageHandler handler) { return addMessage(std::move(handler), false); }
    size_t onceMessage(P2PMessageHandler handler) { return addMessage(std::move(handler), true); }
    size_t onError(P2PErrorHandler handler);
    size_t onClose(P2PHandler handler);
    void removeListener(size_t id);

private:
    template <class Handler>
    struct Listener {
        size_t id;
        bool once;
        Handler handler;
    };

    size_t addMessage(P2PMessageHandler handler, bool once);
    void handleData(std::span<const uint8_t> data);
    void emitError(const Error& error);
    void resetHeartbeat();
    void handleHeartbeat();

    std::optional<int> heartbeatTimoutMs_;
    SetTimeoutImpl setTimeoutImpl_;
    size_t nextListenerId_ = 1;
    std::vector<Listener<P2PMessageHandler>> messageListeners_;
    std::vector<Listener<P2PErrorHandler>> errorListeners_;
    std::vector<Listener<P2PHandler>> closeListeners_;
};

}  // namespace dwarfkit::p2p
