// Port of actionstream src/client.ts (Roborovski action stream). The
// event-driven WebSocket client becomes a blocking pull client: all socket
// processing (heartbeats, callbacks, reconnects with backoff) happens inside
// next()/nextWithTimeout(); the browser-side action queue and its overflow
// path do not exist because unread frames stay in the transport (see
// DIVERGENCES.md).
#pragma once

#include <functional>

#include <dwarfkit/actionstream/types.hpp>

namespace dwarfkit {

class ActionStreamClient {
public:
    ActionStreamClient(std::string url, ActionStreamFilter filter,
                       const ActionStreamOptions& options = {});

    // Callbacks fire while next()/nextWithTimeout() pumps the socket.
    std::function<void(const StreamState&)> onHeartbeat;
    std::function<void(const StreamState&)> onCatchupComplete;
    std::function<void(int code, const std::string& message)> onError;
    std::function<void()> onConnect;
    std::function<void()> onDisconnect;
    std::function<void(const StreamGap&)> onGap;

    uint64_t headSeq() const { return headSeq_; }
    uint64_t libSeq() const { return libSeq_; }
    bool connected() const { return connected_; }
    bool catchupComplete() const { return catchupComplete_; }

    // Dial and subscribe. next() also connects lazily.
    Result<void> connect();
    void close();

    // Block until the next action arrives.
    Result<StreamAction> next(CancelToken token = {});
    // nullopt on timeout, like the TS null result.
    Result<std::optional<StreamAction>> nextWithTimeout(std::chrono::milliseconds timeout,
                                                        CancelToken token = {});

private:
    Result<void> dial();
    void sendSubscribe();
    void teardownSocket();
    // One protocol message: nullopt when the message was not an action.
    Result<std::optional<StreamAction>> handleMessage(const std::string& data);
    Result<std::optional<StreamAction>> pumpOnce(std::optional<std::chrono::milliseconds> slice,
                                                 CancelToken token);
    void acceptSeq(const StreamAction& action);
    void ackSeq(const StreamAction& action);
    bool wasHealthy() const;

    std::string url_;
    ActionStreamFilter filter_;
    bool decode_ = true;
    std::chrono::milliseconds reconnectDelay_;
    std::chrono::milliseconds reconnectMaxDelay_;
    int64_t ackInterval_ = 1000;
    std::chrono::milliseconds healthyThreshold_;
    bool startAtHead_ = false;

    WebSocketProvider* ws_ = nullptr;
    bool socketLive_ = false;
    uint64_t currentSeq_ = 0;
    uint64_t lastAcked_ = 0;
    bool hasAcked_ = false;
    bool closed_ = false;
    bool connected_ = false;
    bool catchupComplete_ = false;
    uint64_t headSeq_ = 0;
    uint64_t libSeq_ = 0;
    std::optional<std::chrono::steady_clock::time_point> catchupCompleteAt_;
    std::chrono::milliseconds currentBackoff_;
    std::optional<int64_t> expectedSubSeq_ = 1;
};

}  // namespace dwarfkit
