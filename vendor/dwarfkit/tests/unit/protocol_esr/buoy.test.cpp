// Unit tests for the buoy client over fake transport providers (the upstream
// buoy-client test suite runs against the live cb.anchor.link service; a
// DK_LIVE_TESTS variant of send & receive lives at the bottom).
#include <doctest/doctest.h>

#include <deque>

#include <dwarfkit/protocol_esr.hpp>

#ifdef DK_LIVE_TESTS
#include <dwarfkit/transport/curl_fetch_provider.hpp>
#include <dwarfkit/transport/curl_websocket_provider.hpp>
#endif

using namespace dwarfkit;

namespace {

struct FakeFetch final : FetchProvider {
    FetchRequest last;
    FetchResponse response{200, "", {}};
    Result<FetchResponse> fetch(const FetchRequest& request) override {
        last = request;
        return response;
    }
};

struct FakeWebSocket final : WebSocketProvider {
    struct Step {
        enum Kind { Message, SocketError, Timeout } kind = Message;
        std::vector<uint8_t> data;
    };
    std::deque<Step> steps;
    std::vector<std::vector<uint8_t>> sent;
    std::vector<std::string> urls;
    int connectCalls = 0;

    Result<void> connect(std::string_view url) override {
        connectCalls++;
        urls.emplace_back(url);
        return {};
    }
    Result<Bytes> receive(std::chrono::milliseconds, CancelToken) override {
        if (steps.empty()) {
            return err(ErrorKind::Transport, "Timed out", 0, json{{"code", "E_TIMEOUT"}});
        }
        const Step step = steps.front();
        steps.pop_front();
        switch (step.kind) {
            case Step::SocketError:
                return err(ErrorKind::Transport, "Socket error", 0, json{{"code", "E_NETWORK"}});
            case Step::Timeout:
                return err(ErrorKind::Transport, "Timed out", 0, json{{"code", "E_TIMEOUT"}});
            case Step::Message:
            default:
                return Bytes(step.data);
        }
    }
    Result<void> send(std::span<const uint8_t> data) override {
        sent.emplace_back(data.begin(), data.end());
        return {};
    }
    void close() override {}
};

std::vector<uint8_t> toBytes(std::string_view text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

}  // namespace

TEST_SUITE("buoy") {
    TEST_CASE("send posts the message to the channel") {
        FakeFetch fetch;
        fetch.response.headers.push_back({"X-Buoy-Delivery", "buffered"});
        const auto result = buoy::send(
            "foo", {.channel = "test-channel", .service = "wss://cb.example.com/", .fetch = &fetch});
        CHECK(result.value() == buoy::SendResult::buffered);
        CHECK(fetch.last.url == "https://cb.example.com/test-channel");
        CHECK(fetch.last.method == "POST");
        CHECK(fetch.last.body == "foo");
        CHECK(fetch.last.headers.empty());
    }

    TEST_CASE("send reports delivery") {
        FakeFetch fetch;
        fetch.response.headers.push_back({"x-buoy-delivery", "delivered"});
        const auto result = buoy::send(
            "foo", {.channel = "test-channel", .service = "https://cb.example.com", .fetch = &fetch});
        CHECK(result.value() == buoy::SendResult::delivered);
    }

    TEST_CASE("send encodes json messages like JSON.stringify") {
        FakeFetch fetch;
        const json message = {{"foo", {{"bar", {{"baz", json::array({-420})}}}}}};
        REQUIRE(buoy::send(message, {.channel = "test-channel",
                                     .service = "https://cb.example.com",
                                     .fetch = &fetch})
                    .has_value());
        CHECK(fetch.last.body == R"({"foo":{"bar":{"baz":[-420]}}})");
    }

    TEST_CASE("send timeout headers") {
        FakeFetch fetch;
        REQUIRE(buoy::send("m", {.channel = "test-channel",
                                 .service = "https://s",
                                 .timeout = std::chrono::milliseconds(1500),
                                 .fetch = &fetch})
                    .has_value());
        REQUIRE(fetch.last.headers.size() == 1);
        CHECK(fetch.last.headers[0].first == "X-Buoy-Soft-Wait");
        CHECK(fetch.last.headers[0].second == "2");
        REQUIRE(buoy::send("m", {.channel = "test-channel",
                                 .service = "https://s",
                                 .timeout = std::chrono::milliseconds(1500),
                                 .requireDelivery = true,
                                 .fetch = &fetch})
                    .has_value());
        REQUIRE(fetch.last.headers.size() == 1);
        CHECK(fetch.last.headers[0].first == "X-Buoy-Wait");
        CHECK(fetch.last.headers[0].second == "2");
    }

    TEST_CASE("send requireDelivery needs a timeout") {
        FakeFetch fetch;
        const auto result = buoy::send("m", {.channel = "test-channel",
                                             .service = "https://s",
                                             .requireDelivery = true,
                                             .fetch = &fetch});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message == "requireDelivery can only be used with timeout");
    }

    TEST_CASE("send maps error statuses") {
        FakeFetch fetch;
        const buoy::SendOptions options{
            .channel = "test-channel", .service = "https://s", .fetch = &fetch};
        fetch.response.status = 408;
        CHECK(buoy::send("m", options).error().message == "Unable to deliver message");
        fetch.response.status = 410;
        CHECK(buoy::send("m", options).error().message == "Request cancelled");
        fetch.response.status = 500;
        CHECK(buoy::send("m", options).error().message == "Unexpected status code 500");
    }

    TEST_CASE("listener requires service, channel and websocket") {
        FakeWebSocket ws;
        CHECK(buoy::Listener::make({.channel = "c", .service = "", .webSocket = &ws})
                  .error()
                  .message == "Options must include a service url");
        CHECK(buoy::Listener::make({.channel = "", .service = "s", .webSocket = &ws})
                  .error()
                  .message == "Options must include a channel name");
        CHECK_FALSE(buoy::Listener::make({.channel = "c", .service = "s"}).has_value());
    }

    TEST_CASE("listener url swaps the protocol and adds the v2 query") {
        FakeWebSocket ws;
        const auto listener =
            buoy::Listener::make(
                {.channel = "my-channel", .service = "https://cb.example.com/", .webSocket = &ws})
                .value();
        CHECK(listener.url() == "wss://cb.example.com/my-channel?v=2");
    }

    TEST_CASE("receive returns a message") {
        FakeWebSocket ws;
        ws.steps.push_back({FakeWebSocket::Step::Message, toBytes("foo")});
        const auto result = buoy::receive({.channel = "test-channel-receive",
                                           .service = "https://cb.example.com",
                                           .webSocket = &ws});
        CHECK(result.value() == "foo");
        REQUIRE(ws.urls.size() == 1);
        CHECK(ws.urls[0] == "wss://cb.example.com/test-channel-receive?v=2");
    }

    TEST_CASE("receive acks heartbeat frames and keeps waiting") {
        FakeWebSocket ws;
        ws.steps.push_back({FakeWebSocket::Step::Message, {0x42, 0x42, 0x01, 0x07}});
        ws.steps.push_back({FakeWebSocket::Step::Message, toBytes("foo")});
        const auto result = buoy::receive(
            {.channel = "c", .service = "https://s", .webSocket = &ws});
        CHECK(result.value() == "foo");
        REQUIRE(ws.sent.size() == 1);
        CHECK(ws.sent[0] == std::vector<uint8_t>{0x42, 0x42, 0x02, 0x07});
    }

    TEST_CASE("receive delivers the payload of an ack-requesting frame") {
        FakeWebSocket ws;
        std::vector<uint8_t> frame = {0x42, 0x42, 0x01, 0x03};
        const auto payload = toBytes("bar");
        frame.insert(frame.end(), payload.begin(), payload.end());
        ws.steps.push_back({FakeWebSocket::Step::Message, frame});
        const auto result = buoy::receive(
            {.channel = "c", .service = "https://s", .webSocket = &ws});
        CHECK(result.value() == "bar");
        REQUIRE(ws.sent.size() == 1);
        CHECK(ws.sent[0] == std::vector<uint8_t>{0x42, 0x42, 0x02, 0x03});
    }

    TEST_CASE("receive reconnects after a socket error") {
        FakeWebSocket ws;
        ws.steps.push_back({FakeWebSocket::Step::SocketError, {}});
        ws.steps.push_back({FakeWebSocket::Step::Message, toBytes("foo")});
        const auto result = buoy::receive(
            {.channel = "c", .service = "https://s", .webSocket = &ws});
        CHECK(result.value() == "foo");
        CHECK(ws.connectCalls == 2);
    }

    TEST_CASE("receive times out") {
        FakeWebSocket ws;
        const auto result = buoy::receive({.channel = "c",
                                           .service = "https://s",
                                           .webSocket = &ws,
                                           .timeout = std::chrono::milliseconds(30)});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message == "Timed out");
    }

    TEST_CASE("receive honors a cancelled token") {
        FakeWebSocket ws;
        CancelToken token;
        token.cancel();
        const auto result = buoy::receive(
            {.channel = "c", .service = "https://s", .webSocket = &ws}, token);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == ErrorKind::Canceled);
    }

    TEST_CASE("backoff caps at five seconds") {
        CHECK(buoy::backoff(0) == std::chrono::milliseconds(0));
        CHECK(buoy::backoff(1) == std::chrono::milliseconds(49));
        CHECK(buoy::backoff(2) == std::chrono::milliseconds(196));
        CHECK(buoy::backoff(100) == std::chrono::milliseconds(5000));
    }

#ifdef DK_LIVE_TESTS
    TEST_CASE("send & receive against the live buoy service") {
        CurlFetchProvider fetch;
        CurlWebSocketProvider ws;
        const std::string channel = "dwarfkit-test-" + uuid();
        const std::string service = "https://cb.anchor.link";
        const auto sent = buoy::send(
            "foo", {.channel = channel, .service = service, .fetch = &fetch});
        CHECK(sent.value() == buoy::SendResult::buffered);
        const auto received = buoy::receive({.channel = channel,
                                             .service = service,
                                             .webSocket = &ws,
                                             .timeout = std::chrono::seconds(10)});
        CHECK(received.value() == "foo");
    }
#endif
}
