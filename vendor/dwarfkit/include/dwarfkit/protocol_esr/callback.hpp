// Port of protocol-esr src/callback.ts. The translation function parameter
// becomes the fixed upstream default message; session kit translations arrive
// in Phase 3.
#pragma once

#include <dwarfkit/protocol_esr/buoy.hpp>

namespace dwarfkit {

// Wait for a wallet to answer on the buoy callback channel and return the
// CallbackPayload. buoyWs overrides the provider in callbackArgs when set
// (upstream: WebSocket: buoyWs || WebSocket).
Result<json> waitForCallback(const buoy::ReceiveOptions& callbackArgs,
                             WebSocketProvider* buoyWs = nullptr, CancelToken token = {});

}  // namespace dwarfkit
