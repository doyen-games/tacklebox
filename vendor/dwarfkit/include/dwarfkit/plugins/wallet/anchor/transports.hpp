// Port of wallet-plugin-anchor src/transports/{types,errors,native,web}.ts.
// Browser windows become the openLink embedder hook; request cancellation maps
// to ErrorKind::Canceled (AnchorRequestCancelledError).
#pragma once

#include <functional>

#include <dwarfkit/protocol_esr/esr.hpp>
#include <dwarfkit/session/login.hpp>
#include <dwarfkit/session/transact.hpp>
#include <dwarfkit/session/wallet.hpp>

namespace dwarfkit::anchor {

// The output of a single createIdentityRequest call.
using IdentityRequestBundle = IdentityRequestResponse;

using Translator = UserInterfaceTranslateFunction;

// Everything a transport needs from the plugin that owns it.
struct TransportOptions {
    std::string id;
    // The plugin's persisted storage, shared by reference across transports.
    json* data = nullptr;
    std::string buoyUrl;
    WebSocketProvider* buoyWs = nullptr;
    // Embedder hook that opens an esr: deep link or https url on this device.
    // Return false when nothing could be opened; failures are non-fatal.
    std::function<bool(const std::string& url)> openLink;
    // Hide the QR code, as upstream does on known mobile devices.
    bool knownMobile = false;
    // Cancels callback waits (the upstream prompt-cancel path).
    CancelToken token;
};

// Drives the native Anchor apps over an esr: deep link, a QR code, and buoy.
class NativeTransport {
public:
    explicit NativeTransport(const TransportOptions& options) : options_(options) {}

    Result<WalletPluginLoginResponse> login(LoginContext& context,
                                            const IdentityRequestBundle& bundle,
                                            const Translator& t) const;
    Result<WalletPluginSignResponse> sign(const ResolvedSigningRequest& resolved,
                                          TransactContext& context) const;

private:
    TransportOptions options_;
};

// Drives the Anchor web authenticator over buoy; the popup window becomes the
// openLink hook.
class WebTransport {
public:
    explicit WebTransport(const TransportOptions& options) : options_(options) {}

    // Build the login URL for a prepared identity request.
    std::string loginUrl(const LoginContext& context, const IdentityRequestBundle& bundle,
                         const std::string& baseUrl) const;

    Result<WalletPluginLoginResponse> login(LoginContext& context,
                                            const IdentityRequestBundle& bundle,
                                            const std::string& baseUrl) const;
    Result<WalletPluginSignResponse> sign(const ResolvedSigningRequest& resolved,
                                          TransactContext& context,
                                          const std::string& baseUrl) const;

private:
    TransportOptions options_;
};

}  // namespace dwarfkit::anchor
