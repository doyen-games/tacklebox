#include <dwarfkit/plugins/wallet/anchor.hpp>

namespace dwarfkit {

using anchor::AnchorMode;

WalletPluginAnchor::WalletPluginAnchor(const WalletPluginAnchorOptions& options)
    : buoyUrl(options.buoyUrl),
      buoyWs(options.buoyWs),
      webAuthenticatorUrls(options.webAuthenticatorUrls),
      openLink_(options.openLink),
      knownMobile_(options.knownMobile),
      token_(options.token) {
    config_ = {.requiresChainSelect = false, .requiresPermissionSelect = false};
    metadata_ = WalletPluginMetadata::from(
        json{{"name", "Anchor"},
             {"description", ""},
             {"homepage", "https://anchorwallet.io"},
             {"download", "https://anchorwallet.io/download"}});
    setMode(options.mode);
}

anchor::TransportOptions WalletPluginAnchor::transportOptions() {
    return {.id = id(),
            .data = &data_,
            .buoyUrl = buoyUrl,
            .buoyWs = buoyWs.get(),
            .openLink = openLink_,
            .knownMobile = knownMobile_,
            .token = token_};
}

void WalletPluginAnchor::setMode(std::optional<AnchorMode> mode) {
    if (!mode) {
        loginModeOverride_ = std::nullopt;
        anchor::clearMode(data_);
        return;
    }
    anchor::writeMode(data_, *mode);
    loginModeOverride_ = mode;
}

Result<WalletPluginLoginResponse> WalletPluginAnchor::login(LoginContext& context) {
    return handleLogin(context);
}

Result<WalletPluginLoginResponse> WalletPluginAnchor::handleLogin(LoginContext& context) {
    if (!context.ui) {
        return err(ErrorKind::Invalid, "No UI available");
    }

    DK_TRY(perCall, anchor::readLoginOptions(id(), context.arbitrary));
    const auto t = context.ui->getTranslate(id());
    const auto webUrl = anchor::resolveWebAuthenticatorUrl(
        context.chain ? std::optional(context.chain->id.hexString()) : std::nullopt,
        webAuthenticatorUrls);

    DK_TRY(bundle, createIdentityRequest(
                       {context.appName.value_or(""), context.chain, context.chains,
                        context.esrOptions()},
                       buoyUrl));

    const auto options = transportOptions();
    // Native-only chain: never route to the browser.
    if (!webUrl) {
        return anchor::NativeTransport(options).login(context, bundle, t);
    }

    // Upstream asks interactively when nothing forces a mode; without click
    // callbacks the app transport is the default route.
    const AnchorMode mode =
        perCall.mode ? *perCall.mode
                     : (loginModeOverride_ ? *loginModeOverride_ : AnchorMode::app);
    anchor::writeMode(data_, mode);

    if (mode == AnchorMode::web) {
        return anchor::WebTransport(options).login(context, bundle, *webUrl);
    }
    return anchor::NativeTransport(options).login(context, bundle, t);
}

Result<WalletPluginSignResponse> WalletPluginAnchor::sign(const ResolvedSigningRequest& resolved,
                                                          TransactContext& context) {
    return handleSign(resolved, context);
}

Result<WalletPluginSignResponse> WalletPluginAnchor::handleSign(
    const ResolvedSigningRequest& resolved, TransactContext& context) {
    // Never asks a question; v1.x sessions predate the concept and are always
    // native.
    const AnchorMode mode = anchor::readMode(data_).value_or(AnchorMode::app);

    const auto options = transportOptions();
    if (mode == AnchorMode::web) {
        const auto webUrl = anchor::resolveWebAuthenticatorUrl(context.chain.id.hexString(),
                                                               webAuthenticatorUrls);
        if (!webUrl) {
            return err(ErrorKind::Invalid,
                       "This session signs in the browser, but there is no Anchor web "
                       "authenticator for chain " +
                           context.chain.id.hexString() + ".");
        }
        return anchor::WebTransport(options).sign(resolved, context, *webUrl);
    }

    return anchor::NativeTransport(options).sign(resolved, context);
}

}  // namespace dwarfkit
