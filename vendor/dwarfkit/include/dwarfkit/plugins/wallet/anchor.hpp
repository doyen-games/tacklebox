// Port of wallet-plugin-anchor src/index.ts. The browser popup/mode-switch UX
// (chooseMode, loginWithSwitch, recoverLogin) needs interactive button
// callbacks; mode selection here comes from options or per-call arbitrary
// data, defaulting to the app transport (see DIVERGENCES.md).
#pragma once

#include <dwarfkit/plugins/wallet/anchor/chains.hpp>
#include <dwarfkit/plugins/wallet/anchor/mode.hpp>
#include <dwarfkit/plugins/wallet/anchor/transports.hpp>

namespace dwarfkit {

// Options controlling Anchor's native and browser transports.
struct WalletPluginAnchorOptions {
    // Buoy callback service URL.
    std::string buoyUrl = "https://cb.anchor.link";
    // WebSocket override forwarded to buoy callback handling.
    std::shared_ptr<WebSocketProvider> buoyWs;
    // Extra or replacement web-authenticator URLs keyed by chain ID.
    std::map<std::string, std::string> webAuthenticatorUrls;
    // Explicit login route; omit it to use the stored mode or the app route.
    std::optional<anchor::AnchorMode> mode;
    // Embedder hook that opens an esr: deep link or https url on this device.
    std::function<bool(const std::string& url)> openLink;
    // Hide the QR code, as upstream does on known mobile devices.
    bool knownMobile = false;
    // Cancels callback waits from another thread.
    CancelToken token;
};

class WalletPluginAnchor final : public AbstractWalletPlugin {
public:
    explicit WalletPluginAnchor(const WalletPluginAnchorOptions& options = {});

    std::string id() const override { return "anchor"; }
    LocaleDefinitions translations() const override;

    Result<WalletPluginLoginResponse> login(LoginContext& context) override;
    Result<WalletPluginSignResponse> sign(const ResolvedSigningRequest& resolved,
                                          TransactContext& context) override;

    // Override login routing until cleared; nullopt restores the default.
    void setMode(std::optional<anchor::AnchorMode> mode);
    // The mode stored or inferred for signing; this does not imply a login
    // override.
    std::optional<anchor::AnchorMode> getMode() const { return anchor::readMode(data_); }

    // The chain's web authenticator URL, when the chain has one.
    std::optional<std::string> webAuthenticatorUrl(
        const std::optional<std::string>& chainId) const {
        return anchor::resolveWebAuthenticatorUrl(chainId, webAuthenticatorUrls);
    }

    std::string buoyUrl;
    std::shared_ptr<WebSocketProvider> buoyWs;
    std::map<std::string, std::string> webAuthenticatorUrls;

private:
    Result<WalletPluginLoginResponse> handleLogin(LoginContext& context);
    Result<WalletPluginSignResponse> handleSign(const ResolvedSigningRequest& resolved,
                                                TransactContext& context);
    anchor::TransportOptions transportOptions();

    std::function<bool(const std::string& url)> openLink_;
    bool knownMobile_ = false;
    CancelToken token_;
    std::optional<anchor::AnchorMode> loginModeOverride_;
};

}  // namespace dwarfkit
