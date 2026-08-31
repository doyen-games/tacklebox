// The popup + postMessage exchange the Cloud Wallet pages speak, as an
// embedder interface. Engines implement it with a WebView: open() shows the
// page, awaitMessage() blocks for the next message the page posts,
// postMessage() sends into the page, close() dismisses it (see
// DIVERGENCES.md).
#pragma once

#include <chrono>
#include <string>

#include <dwarfkit/core/cancel.hpp>
#include <dwarfkit/core/json.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

struct WebViewBridge {
    virtual Result<void> open(const std::string& url) = 0;
    // The next message posted by the page. Error contract mirrors
    // WebSocketProvider: timeout is (Transport, details["code"]=E_TIMEOUT), a
    // page closed by the user is (Canceled).
    virtual Result<json> awaitMessage(std::chrono::milliseconds timeout, CancelToken token) = 0;
    virtual Result<void> postMessage(const json& message) = 0;
    virtual void close() = 0;
    virtual ~WebViewBridge() = default;
};

}  // namespace dwarfkit
