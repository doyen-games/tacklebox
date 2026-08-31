// Port of antelope src/p2p/provider.ts: the transport interface the client
// speaks and the 4-byte-length envelope framing provider. Handlers are
// std::function and fire synchronously when the lower layer feeds data.
#pragma once

#include <functional>
#include <vector>

#include <dwarfkit/core/result.hpp>

namespace dwarfkit::p2p {

using P2PDataHandler = std::function<void(std::span<const uint8_t> encodedMessage)>;
using P2PErrorHandler = std::function<void(const Error& error)>;
using P2PHandler = std::function<void()>;

// Provider interface for the P2P protocol, responsible for re-assembling full
// message payloads before delivering them upstream.
struct P2PProvider {
    virtual void write(std::span<const uint8_t> encodedMessage, P2PHandler done = {}) = 0;
    virtual void end(P2PHandler cb = {}) = 0;
    virtual void destroy(const std::optional<Error>& error = {}) = 0;

    virtual void onData(P2PDataHandler handler) = 0;
    virtual void onError(P2PErrorHandler handler) = 0;
    virtual void onClose(P2PHandler handler) = 0;
    virtual ~P2PProvider() = default;
};

// Wraps a raw stream provider with the nodeos envelope: each message is
// prefixed with a 4-byte little-endian length; incoming stream data is
// buffered and re-assembled.
class SimpleEnvelopeP2PProvider final : public P2PProvider {
public:
    static constexpr size_t maxReadLength = 8 * 1024 * 1024;

    explicit SimpleEnvelopeP2PProvider(P2PProvider* nextProvider);

    void write(std::span<const uint8_t> data, P2PHandler done = {}) override;
    void end(P2PHandler cb = {}) override;
    void destroy(const std::optional<Error>& error = {}) override;

    void onData(P2PDataHandler handler) override { dataHandlers_.push_back(std::move(handler)); }
    void onError(P2PErrorHandler handler) override {
        errorHandlers_.push_back(std::move(handler));
    }
    void onClose(P2PHandler handler) override;

private:
    void handleData(std::span<const uint8_t> data);
    void emitData(std::span<const uint8_t> message);
    void emitError(const Error& error);

    P2PProvider* nextProvider_;
    std::vector<uint8_t> remainingData_;
    std::vector<P2PDataHandler> dataHandlers_;
    std::vector<P2PErrorHandler> errorHandlers_;
};

}  // namespace dwarfkit::p2p
