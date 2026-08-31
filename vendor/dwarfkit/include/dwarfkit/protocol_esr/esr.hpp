// Port of protocol-esr src/esr.ts: the abstract methods useful to all
// ESR-based wallet plugins. Upstream takes the session kit's LoginContext;
// dwarfkit's protocol_esr sits below the session kit, so functions take
// EsrLoginContext, the slice of LoginContext they actually read, and the
// session kit converts (see DIVERGENCES.md).
#pragma once

#include <dwarfkit/common.hpp>
#include <dwarfkit/protocol_esr/buoy.hpp>
#include <dwarfkit/protocol_esr/sealed_messages.hpp>
#include <dwarfkit/protocol_esr/types.hpp>
#include <dwarfkit/protocol_esr/utils.hpp>
#include <dwarfkit/signing_request.hpp>

namespace dwarfkit {

// The slice of LoginContext read by createIdentityRequest and
// verifyLoginCallbackResponse.
struct EsrLoginContext {
    std::string appName;
    std::optional<ChainDefinition> chain;
    std::vector<ChainDefinition> chains;
    SigningRequestEncodingOptions esrOptions;
};

struct IdentityRequestResponse {
    // The buoy channel the wallet will call back on.
    buoy::ReceiveOptions callback;
    // Request for multi-device login.
    SigningRequest request;
    // Request for same-device login.
    SigningRequest sameDeviceRequest;
    PublicKey requestKey;
    PrivateKey privateKey;
};

// Create an identity request with a fresh request key and buoy callback
// channel.
Result<IdentityRequestResponse> createIdentityRequest(const EsrLoginContext& context,
                                                      const std::string& buoyUrl);

// Set a fresh buoy callback channel on the request (mutating) and return it.
buoy::ReceiveOptions setTransactionCallback(SigningRequest& request, const std::string& buoyUrl);

std::string getUserAgent();

// CallbackType for a buoy channel: url service/channel, background true.
CallbackType prepareCallback(const buoy::ReceiveOptions& callbackChannel);

// A fresh buoy channel on the service: {service: buoyUrl, channel: uuid()}.
buoy::ReceiveOptions prepareCallbackChannel(const std::string& buoyUrl);

// Check a login callback payload against the expected chain(s).
Result<void> verifyLoginCallbackResponse(const json& callbackResponse,
                                         const EsrLoginContext& context);

// Signatures from a callback payload: sig plus sig0/sig1/... (zero- or
// one-based, gaps tolerated), deduplicated in order.
Result<std::vector<Signature>> extractSignaturesFromCallback(const json& payload);

// Whether the object is a callback payload (has a tx key).
bool isCallback(const json& object);

}  // namespace dwarfkit
