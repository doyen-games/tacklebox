#include <dwarfkit/protocol_esr/callback.hpp>

namespace dwarfkit {

Result<json> waitForCallback(const buoy::ReceiveOptions& callbackArgs, WebSocketProvider* buoyWs,
                             CancelToken token) {
    buoy::ReceiveOptions options = callbackArgs;
    if (buoyWs) {
        options.webSocket = buoyWs;
    }
    DK_TRY(response, buoy::receive(options, token));
    const json payload = json::parse(response, nullptr, false);
    if (payload.is_discarded()) {
        return err(ErrorKind::Invalid, "Unable to decode callback JSON");
    }
    // a wallet can answer with {rejected: reason} instead of a payload
    if (payload.is_object() && payload.contains("rejected") && payload["rejected"].is_string()) {
        return err(ErrorKind::Canceled, payload["rejected"].get<std::string>());
    }
    if (!payload.is_object() || !payload.contains("sa") || !payload.contains("sp") ||
        !payload.contains("cid")) {
        return err(ErrorKind::Canceled, "The request was cancelled from Anchor.");
    }
    return payload;
}

}  // namespace dwarfkit
