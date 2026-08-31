#include <dwarfkit/actionstream/client.hpp>

#include <thread>

namespace dwarfkit {

namespace {

// UInt64.max: subscribe past head
constexpr uint64_t startAtHeadSentinel = 0xffffffffffffffffull;

bool isTimeout(const Error& error) {
    return error.kind == ErrorKind::Transport && error.details.value("code", "") == "E_TIMEOUT";
}

}  // namespace

ActionStreamClient::ActionStreamClient(std::string url, ActionStreamFilter filter,
                                       const ActionStreamOptions& options)
    : url_(std::move(url)),
      filter_(std::move(filter)),
      decode_(options.decode),
      reconnectDelay_(options.reconnectDelay),
      reconnectMaxDelay_(options.reconnectMaxDelay),
      ackInterval_(options.ackInterval),
      healthyThreshold_(options.healthyThreshold),
      startAtHead_(options.startAtHead),
      ws_(options.webSocket),
      currentSeq_(options.startAtHead ? 0 : options.startSeq.value_or(0)),
      currentBackoff_(options.reconnectDelay) {}

Result<void> ActionStreamClient::connect() {
    if (closed_) {
        return err(ErrorKind::Invalid, "Client is closed");
    }
    return dial();
}

void ActionStreamClient::close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    const bool wasConnected = connected_;
    teardownSocket();
    if (wasConnected && onDisconnect) {
        onDisconnect();
    }
}

void ActionStreamClient::teardownSocket() {
    if (ws_ && socketLive_) {
        ws_->close();
    }
    socketLive_ = false;
    connected_ = false;
}

Result<void> ActionStreamClient::dial() {
    if (!ws_) {
        return err(ErrorKind::Invalid, "No WebSocket transport configured");
    }
    // resetConnectionState: fields whose lifetime is one socket
    catchupCompleteAt_ = std::nullopt;
    expectedSubSeq_ = 1;
    DK_CHECK(ws_->connect(url_));
    socketLive_ = true;
    sendSubscribe();
    return {};
}

void ActionStreamClient::sendSubscribe() {
    json msg = {{"type", "subscribe"}};
    if (!filter_.contracts.empty()) {
        json contracts = json::array();
        for (const auto& name : filter_.contracts) {
            contracts.push_back(name.toString());
        }
        msg["contracts"] = contracts;
    }
    if (!filter_.receivers.empty()) {
        json receivers = json::array();
        for (const auto& name : filter_.receivers) {
            receivers.push_back(name.toString());
        }
        msg["receivers"] = receivers;
    }
    if (!filter_.actions.empty()) {
        json actions = json::array();
        for (const auto& name : filter_.actions) {
            actions.push_back(name.toString());
        }
        msg["actions"] = actions;
    }
    if (currentSeq_ > 0) {
        msg["start_seq"] = std::to_string(currentSeq_);
    } else if (startAtHead_) {
        msg["start_seq"] = std::to_string(startAtHeadSentinel);
    }
    msg["decode"] = decode_;
    const std::string text = msg.dump();
    (void)ws_->send(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    connected_ = true;
    if (onConnect) {
        onConnect();
    }
}

Result<std::optional<StreamAction>> ActionStreamClient::handleMessage(const std::string& data) {
    const json msg = json::parse(data, nullptr, false);
    if (msg.is_discarded() || !msg.is_object()) {
        return std::optional<StreamAction>();
    }
    const std::string type = msg.value("type", "");
    if (type == "heartbeat") {
        headSeq_ = std::stoull(msg.value("head_seq", "0"));
        libSeq_ = std::stoull(msg.value("lib_seq", "0"));
        if (onHeartbeat) {
            onHeartbeat({headSeq_, libSeq_});
        }
        return std::optional<StreamAction>();
    }
    if (type == "catchup_complete") {
        headSeq_ = std::stoull(msg.value("head_seq", "0"));
        libSeq_ = std::stoull(msg.value("lib_seq", "0"));
        catchupComplete_ = true;
        catchupCompleteAt_ = std::chrono::steady_clock::now();
        if (onCatchupComplete) {
            onCatchupComplete({headSeq_, libSeq_});
        }
        return std::optional<StreamAction>();
    }
    if (type == "error") {
        if (onError) {
            onError(msg.value("code", 0), msg.value("message", ""));
        }
        return std::optional<StreamAction>();
    }
    if (type != "action") {
        return std::optional<StreamAction>();
    }

    // sub_seq gap detection: omitted disables checking for this socket
    if (!msg.contains("sub_seq")) {
        expectedSubSeq_ = std::nullopt;
    } else if (expectedSubSeq_) {
        const int64_t received = msg["sub_seq"].get<int64_t>();
        if (received != *expectedSubSeq_) {
            if (onGap) {
                onGap({*expectedSubSeq_, received, currentSeq_});
            }
            // resubscribe: drop this socket; the reconnect logic resumes from
            // currentSeq_
            teardownSocket();
            if (onDisconnect) {
                onDisconnect();
            }
            return std::optional<StreamAction>();
        }
        expectedSubSeq_ = received + 1;
    }

    if (!msg.contains("trx_id") || !msg["trx_id"].is_string() ||
        msg["trx_id"].get_ref<const std::string&>().empty()) {
        if (onError) {
            onError(static_cast<int>(StreamErrorCode::DataInconsistent),
                    "Action " + msg.value("global_seq", "") + " arrived without a trx_id");
        }
        return std::optional<StreamAction>();
    }

    StreamAction action;
    action.globalSeq = std::stoull(msg.value("global_seq", "0"));
    action.blockNum = msg.value("block_num", 0u);
    action.blockTime = msg.value("block_time", int64_t(0));
    action.contract = Name::from(msg.value("contract", ""));
    action.action = Name::from(msg.value("action", ""));
    action.receiver = Name::from(msg.value("receiver", ""));
    DK_TRY(trxId, Checksum256::from(msg["trx_id"].get<std::string>()));
    action.trxId = trxId;
    if (msg.contains("hex_data") && msg["hex_data"].is_string()) {
        action.hexData = msg["hex_data"].get<std::string>();
    }
    if (msg.contains("data")) {
        action.data = msg["data"];
    }
    acceptSeq(action);
    ackSeq(action);
    return std::optional(action);
}

void ActionStreamClient::acceptSeq(const StreamAction& action) {
    if (action.globalSeq < currentSeq_) {
        return;
    }
    currentSeq_ = action.globalSeq + 1;
}

void ActionStreamClient::ackSeq(const StreamAction& action) {
    if (ackInterval_ <= 0) {
        return;
    }
    if (hasAcked_ && action.globalSeq <= lastAcked_) {
        return;
    }
    const uint64_t diff = action.globalSeq - lastAcked_;
    if (diff >= static_cast<uint64_t>(ackInterval_)) {
        lastAcked_ = action.globalSeq;
        hasAcked_ = true;
        if (ws_ && socketLive_) {
            const std::string text =
                json{{"type", "ack"}, {"seq", std::to_string(action.globalSeq)}}.dump();
            (void)ws_->send(std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(text.data()), text.size()));
        }
    }
}

bool ActionStreamClient::wasHealthy() const {
    if (!catchupCompleteAt_) {
        return false;
    }
    return std::chrono::steady_clock::now() - *catchupCompleteAt_ >= healthyThreshold_;
}

Result<std::optional<StreamAction>> ActionStreamClient::pumpOnce(
    std::optional<std::chrono::milliseconds> slice, CancelToken token) {
    if (!socketLive_) {
        if (!connected_ && !closed_) {
            DK_CHECK(dial());
        }
        if (!socketLive_) {
            return err(ErrorKind::Transport, "Socket is not connected");
        }
    }
    auto message = ws_->receive(slice.value_or(std::chrono::milliseconds(60000)), token);
    if (!message) {
        if (isTimeout(message.error()) && slice) {
            return err(message.error());
        }
        if (message.error().kind == ErrorKind::Canceled) {
            return err(message.error());
        }
        if (isTimeout(message.error())) {
            // internal slice elapsed with no traffic; keep waiting
            return std::optional<StreamAction>();
        }
        // socket failure: reconnect with backoff, like the ws onclose path
        const bool wasConnected = connected_;
        teardownSocket();
        if (wasConnected && onDisconnect) {
            onDisconnect();
        }
        if (closed_) {
            return err(ErrorKind::Canceled, "Client closed");
        }
        if (wasHealthy()) {
            currentBackoff_ = reconnectDelay_;
        }
        const auto delay = currentBackoff_;
        currentBackoff_ = std::min(currentBackoff_ * 2, reconnectMaxDelay_);
        if (token.waitFor(delay)) {
            return err(ErrorKind::Canceled, "Client closed");
        }
        DK_CHECK(dial());
        return std::optional<StreamAction>();
    }
    const std::string text(message->array.begin(), message->array.end());
    return handleMessage(text);
}

Result<StreamAction> ActionStreamClient::next(CancelToken token) {
    if (closed_) {
        return err(ErrorKind::Canceled, "Client closed");
    }
    for (;;) {
        if (closed_) {
            return err(ErrorKind::Canceled, "Client closed");
        }
        DK_TRY(action, pumpOnce(std::nullopt, token));
        if (action) {
            return *action;
        }
    }
}

Result<std::optional<StreamAction>> ActionStreamClient::nextWithTimeout(
    std::chrono::milliseconds timeout, CancelToken token) {
    if (closed_) {
        return err(ErrorKind::Canceled, "Client closed");
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (closed_) {
            return err(ErrorKind::Canceled, "Client closed");
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return std::optional<StreamAction>();
        }
        const auto slice =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        auto result = pumpOnce(slice, token);
        if (!result) {
            if (isTimeout(result.error())) {
                return std::optional<StreamAction>();
            }
            return err(result.error());
        }
        if (*result) {
            return *result;
        }
    }
}

}  // namespace dwarfkit
