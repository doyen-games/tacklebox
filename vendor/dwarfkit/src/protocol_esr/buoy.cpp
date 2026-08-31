#include <dwarfkit/protocol_esr/buoy.hpp>

#include <algorithm>
#include <cmath>

namespace dwarfkit::buoy {

namespace {

// service url with the protocol swapped (^ws -> http or ^http -> ws) and any
// trailing slash removed, matching the upstream regex replaces
std::string swapProtocol(std::string_view service, std::string_view from, std::string_view to) {
    std::string rv(service);
    if (rv.starts_with(from)) {
        rv = std::string(to) + rv.substr(from.size());
    }
    if (rv.ends_with('/')) {
        rv.pop_back();
    }
    return rv;
}

std::optional<std::string> findHeader(const std::vector<std::pair<std::string, std::string>>& headers,
                                      std::string_view name) {
    const auto found = std::find_if(headers.begin(), headers.end(), [&](const auto& header) {
        return header.first.size() == name.size() &&
               std::equal(header.first.begin(), header.first.end(), name.begin(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) ==
                                                      std::tolower(static_cast<unsigned char>(b)); });
    });
    if (found == headers.end()) {
        return std::nullopt;
    }
    return found->second;
}

Result<SendResult> sendBody(std::string body, const SendOptions& options) {
    if (!options.fetch) {
        return err(ErrorKind::Invalid, "Missing fetch provider");
    }
    FetchRequest request;
    request.url = swapProtocol(options.service, "ws", "http") + "/" + options.channel;
    request.method = "POST";
    request.body = std::move(body);
    if (options.requireDelivery) {
        if (!options.timeout) {
            return err(ErrorKind::Invalid, "requireDelivery can only be used with timeout");
        }
        request.headers.push_back(
            {"X-Buoy-Wait", std::to_string((options.timeout->count() + 999) / 1000)});
    } else if (options.timeout) {
        request.headers.push_back(
            {"X-Buoy-Soft-Wait", std::to_string((options.timeout->count() + 999) / 1000)});
    }
    DK_TRY(response, options.fetch->fetch(request));
    if (response.status / 100 != 2) {
        if (response.status == 408) {
            return err(ErrorKind::Transport, "Unable to deliver message", response.status);
        }
        if (response.status == 410) {
            return err(ErrorKind::Transport, "Request cancelled", response.status);
        }
        return err(ErrorKind::Transport,
                   "Unexpected status code " + std::to_string(response.status), response.status);
    }
    const auto delivery = findHeader(response.headers, "X-Buoy-Delivery");
    return delivery == "delivered" ? SendResult::delivered : SendResult::buffered;
}

bool isCode(const Error& error, std::string_view code) {
    if (!error.details.is_object() || !error.details.contains("code")) {
        return false;
    }
    const json& value = error.details["code"];
    return value.is_string() && value.get_ref<const std::string&>() == code;
}

}  // namespace

Result<SendResult> send(std::string_view message, const SendOptions& options) {
    return sendBody(std::string(message), options);
}

Result<SendResult> send(std::span<const uint8_t> message, const SendOptions& options) {
    return sendBody(std::string(message.begin(), message.end()), options);
}

Result<SendResult> send(const json& message, const SendOptions& options) {
    return sendBody(message.dump(), options);
}

std::chrono::milliseconds backoff(int tries) {
    const double value = std::pow(static_cast<double>(tries) * 7.0, 2.0);
    return std::chrono::milliseconds(
        static_cast<int64_t>(std::min(value, 5.0 * 1000.0)));
}

Result<Listener> Listener::make(const ListenerOptions& options) {
    if (options.service.empty()) {
        return err(ErrorKind::Invalid, "Options must include a service url");
    }
    if (options.channel.empty()) {
        return err(ErrorKind::Invalid, "Options must include a channel name");
    }
    if (!options.webSocket) {
        return err(ErrorKind::Invalid, "Options must include a websocket provider");
    }
    Listener listener;
    listener.url_ = swapProtocol(options.service, "http", "ws") + "/" + options.channel + "?v=2";
    listener.webSocket_ = options.webSocket;
    return listener;
}

Result<void> Listener::connect() {
    if (connected_) {
        return {};
    }
    DK_CHECK(webSocket_->connect(url_));
    connected_ = true;
    return {};
}

void Listener::disconnect() {
    if (connected_) {
        webSocket_->close();
        connected_ = false;
    }
}

Result<Bytes> Listener::receiveMessage(std::optional<std::chrono::milliseconds> timeout,
                                       CancelToken token) {
    using clock = std::chrono::steady_clock;
    const auto deadline = timeout ? std::optional(clock::now() + *timeout) : std::nullopt;
    // slice used when waiting without a deadline so cancellation stays snappy
    constexpr std::chrono::milliseconds idleSlice(10'000);
    std::optional<Error> lastError;
    int retries = 0;
    const auto timedOut = [&]() -> Error {
        Error error{ErrorKind::Transport, "Timed out", 0, json{{"code", "E_MESSAGE"}}};
        if (lastError) {
            error.details["underlying"] = lastError->message;
        }
        return error;
    };
    while (true) {
        if (token.cancelled()) {
            return err(ErrorKind::Canceled, "Cancelled");
        }
        if (!connected_) {
            const auto connected = connect();
            if (!connected) {
                lastError = connected.error();
                if (deadline && clock::now() >= *deadline) {
                    return tl::unexpected(timedOut());
                }
                if (token.waitFor(backoff(retries++))) {
                    return err(ErrorKind::Canceled, "Cancelled");
                }
                continue;
            }
            retries = 0;
        }
        std::chrono::milliseconds slice = idleSlice;
        if (deadline) {
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - clock::now());
            if (remaining.count() <= 0) {
                return tl::unexpected(timedOut());
            }
            slice = std::min(remaining, idleSlice);
        }
        auto message = webSocket_->receive(slice, token);
        if (!message) {
            if (message.error().kind == ErrorKind::Canceled) {
                return err(ErrorKind::Canceled, "Cancelled");
            }
            if (isCode(message.error(), "E_TIMEOUT")) {
                continue;  // deadline is checked at the top of the loop
            }
            // socket failure: remember it and reconnect with backoff
            lastError = message.error();
            connected_ = false;
            if (deadline && clock::now() >= *deadline) {
                return tl::unexpected(timedOut());
            }
            if (token.waitFor(backoff(retries++))) {
                return err(ErrorKind::Canceled, "Cancelled");
            }
            continue;
        }
        const auto& bytes = message->array;
        if (bytes.size() >= 4 && bytes[0] == 0x42 && bytes[1] == 0x42 && bytes[2] == 0x01) {
            const uint8_t ack[4] = {0x42, 0x42, 0x02, bytes[3]};
            (void)webSocket_->send(ack);
            if (bytes.size() == 4) {
                continue;
            }
            return Bytes(std::vector<uint8_t>(bytes.begin() + 4, bytes.end()));
        }
        return *message;
    }
}

Result<std::string> receive(const ReceiveOptions& options, CancelToken token) {
    DK_TRY(listener, Listener::make(ListenerOptions{.channel = options.channel,
                                                    .service = options.service,
                                                    .webSocket = options.webSocket}));
    DK_CHECK(listener.connect());
    auto message = listener.receiveMessage(options.timeout, token);
    listener.disconnect();
    if (!message) {
        return err(std::move(message.error()));
    }
    return message->utf8String();
}

}  // namespace dwarfkit::buoy
