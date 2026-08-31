// Tests for waitForCallback (protocol-esr src/callback.ts has no upstream
// test file; these cover its three outcomes over the fake provider)
#include <doctest/doctest.h>

#include <deque>

#include <dwarfkit/protocol_esr.hpp>

using namespace dwarfkit;

namespace {

struct QueueWebSocket final : WebSocketProvider {
    std::deque<std::string> messages;
    Result<void> connect(std::string_view) override { return {}; }
    Result<Bytes> receive(std::chrono::milliseconds, CancelToken) override {
        if (messages.empty()) {
            return err(ErrorKind::Transport, "Timed out", 0, json{{"code", "E_TIMEOUT"}});
        }
        const std::string message = messages.front();
        messages.pop_front();
        return Bytes(std::vector<uint8_t>(message.begin(), message.end()));
    }
    Result<void> send(std::span<const uint8_t>) override { return {}; }
    void close() override {}
};

buoy::ReceiveOptions channel(QueueWebSocket& ws) {
    return {.channel = "test-channel-wait",
            .service = "https://cb.example.com",
            .webSocket = &ws,
            .timeout = std::chrono::milliseconds(100)};
}

}  // namespace

TEST_SUITE("pesr-callback") {
    TEST_CASE("waitForCallback returns the payload") {
        QueueWebSocket ws;
        ws.messages.push_back(
            R"({"sig":"SIG","sa":"foo","sp":"active","cid":"beef","tx":"00"})");
        const auto payload = waitForCallback(channel(ws)).value();
        CHECK(payload["sa"] == "foo");
        CHECK(payload["sp"] == "active");
        CHECK(payload["cid"] == "beef");
    }

    TEST_CASE("waitForCallback treats an incomplete payload as cancelled") {
        QueueWebSocket ws;
        ws.messages.push_back(R"({"sig":"SIG"})");
        const auto payload = waitForCallback(channel(ws));
        REQUIRE_FALSE(payload.has_value());
        CHECK(payload.error().kind == ErrorKind::Canceled);
        CHECK(payload.error().message == "The request was cancelled from Anchor.");
    }

    TEST_CASE("waitForCallback surfaces a rejection reason") {
        QueueWebSocket ws;
        ws.messages.push_back(R"({"rejected":"Request expired"})");
        const auto payload = waitForCallback(channel(ws));
        REQUIRE_FALSE(payload.has_value());
        CHECK(payload.error().message == "Request expired");
    }

    TEST_CASE("waitForCallback prefers the override websocket") {
        QueueWebSocket unused;
        QueueWebSocket ws;
        ws.messages.push_back(R"({"sa":"a","sp":"b","cid":"c"})");
        auto options = channel(unused);
        const auto payload = waitForCallback(options, &ws).value();
        CHECK(payload["sa"] == "a");
        CHECK(unused.messages.empty());
    }
}
