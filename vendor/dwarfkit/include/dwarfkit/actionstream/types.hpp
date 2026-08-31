// Port of actionstream src/types.ts.
#pragma once

#include <dwarfkit/antelope.hpp>
#include <dwarfkit/transport/websocket_provider.hpp>

namespace dwarfkit {

struct ActionStreamFilter {
    std::vector<Name> contracts;
    std::vector<Name> receivers;
    std::vector<Name> actions;
};

struct ActionStreamOptions {
    // Resume position; startAtHead subscribes past the current head (the TS
    // startSeq: 'head' form).
    std::optional<uint64_t> startSeq;
    bool startAtHead = false;
    bool decode = true;
    std::chrono::milliseconds reconnectDelay{1000};
    std::chrono::milliseconds reconnectMaxDelay{30000};
    // Send an ack every this many delivered actions; <= 0 disables acks.
    int64_t ackInterval = 1000;
    std::chrono::milliseconds healthyThreshold{10000};
    // The socket transport; a CurlWebSocketProvider when omitted and curl is
    // built in is the embedder's job to supply.
    WebSocketProvider* webSocket = nullptr;
};

struct StreamAction {
    uint64_t globalSeq = 0;
    uint32_t blockNum = 0;
    int64_t blockTime = 0;
    Name contract;
    Name action;
    Name receiver;
    Checksum256 trxId;
    std::optional<std::string> hexData;
    std::optional<json> data;
};

struct StreamState {
    uint64_t headSeq = 0;
    uint64_t libSeq = 0;
};

struct StreamGap {
    int64_t expected = 0;
    int64_t received = 0;
    uint64_t resumeSeq = 0;
};

enum class StreamErrorCode : int {
    InvalidRequest = 1,
    ServerSyncing = 2,
    MaxClients = 3,
    NoActions = 4,
    DataInconsistent = 5,
    ResyncRequired = 6,
};

}  // namespace dwarfkit
