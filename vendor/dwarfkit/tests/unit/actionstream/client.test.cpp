// Port of actionstream test/tests/client.ts. The mock ws server becomes a
// scripted WebSocketProvider; the event-driven cases run through the blocking
// pull API (callbacks fire while nextWithTimeout pumps).
#include <doctest/doctest.h>

#include <deque>

#include <dwarfkit/actionstream.hpp>

using namespace dwarfkit;

namespace {

constexpr auto ms = [](int64_t v) { return std::chrono::milliseconds(v); };

// Plays the MockActionStreamServer role: send() receives client messages and
// the onClientMessage script queues frames for receive().
class FakeStreamSocket final : public WebSocketProvider {
public:
    static constexpr const char* closeSentinel = "\x01CLOSE";

    std::function<void(const json& message, FakeStreamSocket& socket)> onClientMessage;
    std::deque<std::string> frames;
    std::vector<json> clientMessages;
    int connects = 0;
    int closes = 0;

    void push(const json& frame) { frames.push_back(frame.dump()); }
    void pushClose() { frames.push_back(closeSentinel); }

    Result<void> connect(std::string_view) override {
        connects++;
        return {};
    }
    Result<Bytes> receive(std::chrono::milliseconds, CancelToken) override {
        if (frames.empty()) {
            Error error{ErrorKind::Transport, "timeout", 0, json{{"code", "E_TIMEOUT"}}};
            return err(std::move(error));
        }
        const std::string frame = frames.front();
        frames.pop_front();
        if (frame == closeSentinel) {
            Error error{ErrorKind::Transport, "closed", 0, json{{"code", "E_NETWORK"}}};
            return err(std::move(error));
        }
        return Bytes(std::vector<uint8_t>(frame.begin(), frame.end()));
    }
    Result<void> send(std::span<const uint8_t> data) override {
        const json message = json::parse(std::string(data.begin(), data.end()), nullptr, false);
        clientMessages.push_back(message);
        if (onClientMessage) {
            onClientMessage(message, *this);
        }
        return {};
    }
    void close() override { closes++; }
};

json actionFrame(uint64_t globalSeq, int64_t subSeq = -1,
                 const std::string& trxId =
                     "b8e921a7b68d7309847e633d74963f25eb5a7d0b15b1aceb143723c234686a8d") {
    json frame = {{"type", "action"},
                  {"global_seq", std::to_string(globalSeq)},
                  {"block_num", 12345},
                  {"block_time", 1710000000000},
                  {"contract", "eosio.token"},
                  {"action", "transfer"},
                  {"receiver", "eosio.token"},
                  {"trx_id", trxId},
                  {"data", {{"from", "alice"}, {"to", "bob"}}}};
    if (subSeq >= 0) {
        frame["sub_seq"] = subSeq;
    }
    return frame;
}

ActionStreamOptions makeOptions(FakeStreamSocket& socket) {
    ActionStreamOptions options;
    options.webSocket = &socket;
    options.reconnectDelay = ms(1);
    options.reconnectMaxDelay = ms(4);
    return options;
}

}  // namespace

TEST_SUITE("actionstream") {
    TEST_CASE("connect and receive heartbeat") {
        FakeStreamSocket socket;
        socket.onClientMessage = [](const json& message, FakeStreamSocket& s) {
            if (message.value("type", "") == "subscribe") {
                s.push({{"type", "heartbeat"}, {"head_seq", "1000"}, {"lib_seq", "900"}});
            }
        };
        ActionStreamClient client("wss://stream.test", {.contracts = {"eosio.token"_n}},
                                  makeOptions(socket));
        std::optional<StreamState> seen;
        client.onHeartbeat = [&](const StreamState& state) { seen = state; };
        bool connected = false;
        client.onConnect = [&] { connected = true; };
        CHECK(client.connect().has_value());
        CHECK(connected);
        // pump: no action arrives, but the heartbeat is processed
        CHECK(client.nextWithTimeout(ms(10)).value() == std::nullopt);
        REQUIRE(seen.has_value());
        CHECK(seen->headSeq == 1000);
        CHECK(seen->libSeq == 900);
        CHECK(client.headSeq() == 1000);
        CHECK(client.libSeq() == 900);
        client.close();
    }

    TEST_CASE("connecting after close errors") {
        FakeStreamSocket socket;
        ActionStreamClient client("wss://stream.test", {}, makeOptions(socket));
        client.close();
        CHECK_FALSE(client.connect().has_value());
        CHECK_FALSE(client.next().has_value());
    }

    TEST_CASE("receives actions") {
        FakeStreamSocket socket;
        socket.onClientMessage = [](const json& message, FakeStreamSocket& s) {
            if (message.value("type", "") == "subscribe") {
                s.push(actionFrame(100, 1));
                s.push(actionFrame(101, 2));
            }
        };
        ActionStreamClient client("wss://stream.test", {}, makeOptions(socket));
        CHECK(client.connect().has_value());
        SUBCASE("via next with data, trx_id and order") {
            const auto first = client.next().value();
            CHECK(first.globalSeq == 100);
            CHECK(first.contract == "eosio.token"_n);
            CHECK(first.action == "transfer"_n);
            CHECK(first.trxId.hexString() ==
                  "b8e921a7b68d7309847e633d74963f25eb5a7d0b15b1aceb143723c234686a8d");
            REQUIRE(first.data.has_value());
            CHECK((*first.data)["from"] == "alice");
            const auto second = client.next().value();
            CHECK(second.globalSeq == 101);
        }
        SUBCASE("hex_data passes through") {
            socket.frames.clear();
            json frame = actionFrame(200, -1);
            frame["hex_data"] = "deadbeef";
            frame.erase("data");
            socket.push(frame);
            const auto action = client.next().value();
            REQUIRE(action.hexData.has_value());
            CHECK(*action.hexData == "deadbeef");
            CHECK_FALSE(action.data.has_value());
        }
        client.close();
    }

    TEST_CASE("missing trx_id reports DataInconsistent") {
        FakeStreamSocket socket;
        ActionStreamClient client("wss://stream.test", {}, makeOptions(socket));
        CHECK(client.connect().has_value());
        json frame = actionFrame(300, -1);
        frame.erase("trx_id");
        socket.push(frame);
        int code = 0;
        client.onError = [&](int c, const std::string&) { code = c; };
        CHECK(client.nextWithTimeout(ms(10)).value() == std::nullopt);
        CHECK(code == static_cast<int>(StreamErrorCode::DataInconsistent));
        client.close();
    }

    TEST_CASE("nextWithTimeout returns nullopt on timeout") {
        FakeStreamSocket socket;
        ActionStreamClient client("wss://stream.test", {}, makeOptions(socket));
        CHECK(client.connect().has_value());
        CHECK(client.nextWithTimeout(ms(5)).value() == std::nullopt);
        client.close();
    }

    TEST_CASE("catchup complete sets flag and fires callback") {
        FakeStreamSocket socket;
        ActionStreamClient client("wss://stream.test", {}, makeOptions(socket));
        CHECK(client.connect().has_value());
        socket.push({{"type", "catchup_complete"}, {"head_seq", "5000"}, {"lib_seq", "4900"}});
        std::optional<StreamState> seen;
        client.onCatchupComplete = [&](const StreamState& state) { seen = state; };
        CHECK_FALSE(client.catchupComplete());
        CHECK(client.nextWithTimeout(ms(10)).value() == std::nullopt);
        CHECK(client.catchupComplete());
        REQUIRE(seen.has_value());
        CHECK(seen->headSeq == 5000);
        client.close();
    }

    TEST_CASE("server error fires onError") {
        FakeStreamSocket socket;
        ActionStreamClient client("wss://stream.test", {}, makeOptions(socket));
        CHECK(client.connect().has_value());
        socket.push({{"type", "error"}, {"code", 3}, {"message", "too many clients"}});
        int code = 0;
        std::string message;
        client.onError = [&](int c, const std::string& m) {
            code = c;
            message = m;
        };
        CHECK(client.nextWithTimeout(ms(10)).value() == std::nullopt);
        CHECK(code == 3);
        CHECK(message == "too many clients");
        client.close();
    }

    TEST_CASE("subscribe message carries the filter and decode flag") {
        FakeStreamSocket socket;
        ActionStreamClient client("wss://stream.test",
                                  {.contracts = {"eosio.token"_n}, .actions = {"transfer"_n}},
                                  makeOptions(socket));
        CHECK(client.connect().has_value());
        REQUIRE(socket.clientMessages.size() == 1);
        const json& subscribe = socket.clientMessages[0];
        CHECK(subscribe["type"] == "subscribe");
        CHECK(subscribe["contracts"] == json::array({"eosio.token"}));
        CHECK(subscribe["actions"] == json::array({"transfer"}));
        CHECK_FALSE(subscribe.contains("receivers"));
        CHECK(subscribe["decode"] == true);
        CHECK_FALSE(subscribe.contains("start_seq"));
        client.close();
    }

    TEST_CASE("startAtHead subscribes past head, then resumes from delivered") {
        FakeStreamSocket socket;
        socket.onClientMessage = [](const json& message, FakeStreamSocket& s) {
            if (message.value("type", "") == "subscribe" && s.connects == 1) {
                s.push(actionFrame(7000, 1));
                s.pushClose();
            }
        };
        ActionStreamOptions options = makeOptions(socket);
        options.startAtHead = true;
        ActionStreamClient client("wss://stream.test", {}, options);
        CHECK(client.connect().has_value());
        CHECK(socket.clientMessages[0]["start_seq"] == "18446744073709551615");
        const auto action = client.next().value();
        CHECK(action.globalSeq == 7000);
        // the socket drop reconnects; the new subscribe resumes after the
        // delivered action, not from the sentinel
        CHECK(client.nextWithTimeout(ms(20)).value() == std::nullopt);
        REQUIRE(socket.clientMessages.size() >= 2);
        CHECK(socket.clientMessages.back()["start_seq"] == "7001");
        client.close();
    }

    TEST_CASE("acks are sent on the ack interval watermark") {
        FakeStreamSocket socket;
        ActionStreamOptions options = makeOptions(socket);
        options.ackInterval = 2;
        ActionStreamClient client("wss://stream.test", {}, options);
        CHECK(client.connect().has_value());
        socket.push(actionFrame(1, 1));
        socket.push(actionFrame(2, 2));
        socket.push(actionFrame(3, 3));
        CHECK(client.next().value().globalSeq == 1);
        CHECK(client.next().value().globalSeq == 2);
        CHECK(client.next().value().globalSeq == 3);
        json acks = json::array();
        for (const auto& message : socket.clientMessages) {
            if (message.value("type", "") == "ack") {
                acks.push_back(message["seq"]);
            }
        }
        // watermark: first ack at seq 2 (diff from 0 reaches 2), next at 3
        // only if diff >= 2, so exactly one ack
        CHECK(acks == json::array({"2"}));
        client.close();
    }

    TEST_CASE("gap in sub_seq fires onGap and resubscribes from last accepted") {
        FakeStreamSocket socket;
        ActionStreamClient client("wss://stream.test", {}, makeOptions(socket));
        CHECK(client.connect().has_value());
        socket.push(actionFrame(100, 1));
        socket.push(actionFrame(102, 3));
        std::optional<StreamGap> gap;
        client.onGap = [&](const StreamGap& g) { gap = g; };
        CHECK(client.next().value().globalSeq == 100);
        CHECK(client.nextWithTimeout(ms(20)).value() == std::nullopt);
        REQUIRE(gap.has_value());
        CHECK(gap->expected == 2);
        CHECK(gap->received == 3);
        CHECK(gap->resumeSeq == 101);
        // the resubscribe reconnected and resumed from the accepted position
        CHECK(socket.connects >= 2);
        CHECK(socket.clientMessages.back()["start_seq"] == "101");
        client.close();
    }

    TEST_CASE("contiguous sub_seq does not fire onGap") {
        FakeStreamSocket socket;
        ActionStreamClient client("wss://stream.test", {}, makeOptions(socket));
        CHECK(client.connect().has_value());
        socket.push(actionFrame(100, 1));
        socket.push(actionFrame(101, 2));
        bool gapped = false;
        client.onGap = [&](const StreamGap&) { gapped = true; };
        CHECK(client.next().value().globalSeq == 100);
        CHECK(client.next().value().globalSeq == 101);
        CHECK_FALSE(gapped);
        client.close();
    }

    TEST_CASE("omitted sub_seq disables gap checking") {
        FakeStreamSocket socket;
        ActionStreamClient client("wss://stream.test", {}, makeOptions(socket));
        CHECK(client.connect().has_value());
        socket.push(actionFrame(100, -1));
        socket.push(actionFrame(105, 9));
        bool gapped = false;
        client.onGap = [&](const StreamGap&) { gapped = true; };
        CHECK(client.next().value().globalSeq == 100);
        CHECK(client.next().value().globalSeq == 105);
        CHECK_FALSE(gapped);
        client.close();
    }

    TEST_CASE("sub_seq restarts at 1 after a reconnect") {
        FakeStreamSocket socket;
        socket.onClientMessage = [](const json& message, FakeStreamSocket& s) {
            if (message.value("type", "") != "subscribe") {
                return;
            }
            if (s.connects == 1) {
                s.push(actionFrame(100, 1));
                s.push(actionFrame(101, 2));
                s.pushClose();
            } else {
                s.push(actionFrame(102, 1));
            }
        };
        ActionStreamClient client("wss://stream.test", {}, makeOptions(socket));
        CHECK(client.connect().has_value());
        bool gapped = false;
        client.onGap = [&](const StreamGap&) { gapped = true; };
        int disconnects = 0;
        client.onDisconnect = [&] { disconnects++; };
        CHECK(client.next().value().globalSeq == 100);
        CHECK(client.next().value().globalSeq == 101);
        CHECK(client.next().value().globalSeq == 102);
        CHECK_FALSE(gapped);
        CHECK(disconnects == 1);
        CHECK(socket.connects == 2);
        client.close();
    }
}
