#include <dwarfkit/transport/curl_websocket_provider.hpp>

#include <mutex>
#include <thread>

#include <curl/curl.h>

namespace dwarfkit {

namespace {

void globalInit() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

Error networkError(std::string message) {
    return Error{ErrorKind::Transport, std::move(message), 0, json{{"code", "E_NETWORK"}}};
}

}  // namespace

CurlWebSocketProvider::~CurlWebSocketProvider() { close(); }

Result<void> CurlWebSocketProvider::connect(std::string_view url) {
    globalInit();
    close();
    CURL* curl = curl_easy_init();
    if (!curl) {
        return err(ErrorKind::Transport, "Failed to initialize curl");
    }
    const std::string urlString(url);
    curl_easy_setopt(curl, CURLOPT_URL, urlString.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);  // WebSocket
    const CURLcode status = curl_easy_perform(curl);
    if (status != CURLE_OK) {
        curl_easy_cleanup(curl);
        return tl::unexpected(networkError(curl_easy_strerror(status)));
    }
    curl_ = curl;
    return {};
}

Result<Bytes> CurlWebSocketProvider::receive(std::chrono::milliseconds timeout,
                                             CancelToken token) {
    if (!curl_) {
        return tl::unexpected(networkError("Not connected"));
    }
    CURL* curl = static_cast<CURL*>(curl_);
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + timeout;
    std::vector<uint8_t> message;
    char buffer[4096];
    while (true) {
        if (token.cancelled()) {
            return err(ErrorKind::Canceled, "Cancelled");
        }
        size_t received = 0;
        const curl_ws_frame* meta = nullptr;
        const CURLcode status = curl_ws_recv(curl, buffer, sizeof(buffer), &received, &meta);
        if (status == CURLE_AGAIN) {
            if (clock::now() >= deadline) {
                return err(ErrorKind::Transport, "Timed out", 0, json{{"code", "E_TIMEOUT"}});
            }
            // curl has no blocking wait for ws frames; poll with a short sleep
            if (token.waitFor(std::chrono::milliseconds(50))) {
                return err(ErrorKind::Canceled, "Cancelled");
            }
            continue;
        }
        if (status != CURLE_OK) {
            close();
            return tl::unexpected(networkError(curl_easy_strerror(status)));
        }
        if (meta && (meta->flags & CURLWS_CLOSE) != 0) {
            close();
            return tl::unexpected(networkError("Connection closed"));
        }
        if (meta && (meta->flags & CURLWS_PING) != 0) {
            size_t sent = 0;
            (void)curl_ws_send(curl, buffer, received, &sent, 0, CURLWS_PONG);
            continue;
        }
        message.insert(message.end(), buffer, buffer + received);
        if (!meta || meta->bytesleft == 0) {
            return Bytes(std::move(message));
        }
    }
}

Result<void> CurlWebSocketProvider::send(std::span<const uint8_t> data) {
    if (!curl_) {
        return tl::unexpected(networkError("Not connected"));
    }
    CURL* curl = static_cast<CURL*>(curl_);
    size_t offset = 0;
    while (offset < data.size()) {
        size_t sent = 0;
        const CURLcode status = curl_ws_send(curl, data.data() + offset, data.size() - offset,
                                             &sent, 0, CURLWS_BINARY);
        if (status == CURLE_AGAIN) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (status != CURLE_OK) {
            return tl::unexpected(networkError(curl_easy_strerror(status)));
        }
        offset += sent;
    }
    return {};
}

void CurlWebSocketProvider::close() {
    if (curl_) {
        CURL* curl = static_cast<CURL*>(curl_);
        size_t sent = 0;
        (void)curl_ws_send(curl, "", 0, &sent, 0, CURLWS_CLOSE);
        curl_easy_cleanup(curl);
        curl_ = nullptr;
    }
}

}  // namespace dwarfkit
