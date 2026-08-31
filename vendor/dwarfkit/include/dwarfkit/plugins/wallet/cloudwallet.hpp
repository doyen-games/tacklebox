// Port of wallet-plugin-cloudwallet src/{index,login,sign,interfaces}.ts. The
// popup window + window.postMessage exchange becomes the WebViewBridge
// embedder interface (see DIVERGENCES.md).
#pragma once

#include <dwarfkit/plugins/wallet/cloudwallet/bridge.hpp>
#include <dwarfkit/plugins/wallet/cloudwallet/types.hpp>
#include <dwarfkit/plugins/wallet/cloudwallet/utils.hpp>
#include <dwarfkit/session/login.hpp>
#include <dwarfkit/session/transact.hpp>
#include <dwarfkit/session/wallet.hpp>

namespace dwarfkit {

inline constexpr const char* cloudwalletVersion = "1.6.5";

struct WalletPluginCloudWalletOptions {
    std::vector<std::string> supportedChains;
    std::string url = "https://www.mycloudwallet.com";
    // 5 minutes
    std::chrono::milliseconds loginTimeout = std::chrono::milliseconds(300000);
    // The popup webview the Cloud Wallet pages run in.
    std::shared_ptr<WebViewBridge> bridge;
    // Cancels bridge waits from another thread.
    CancelToken token;
};

// Port of src/login.ts popupLogin.
Result<cloudwallet::WAXCloudWalletLoginResponse> popupLogin(
    const UserInterfaceTranslateFunction& t, WebViewBridge& bridge, const std::string& url,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(300000),
    CancelToken token = {});

// Port of src/sign.ts popupTransact.
Result<cloudwallet::WAXCloudWalletSigningResponse> popupTransact(
    const UserInterfaceTranslateFunction& t, WebViewBridge& bridge, const std::string& url,
    const ResolvedSigningRequest& request,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(300000),
    CancelToken token = {});

class WalletPluginCloudWallet final : public AbstractWalletPlugin {
public:
    explicit WalletPluginCloudWallet(const WalletPluginCloudWalletOptions& options = {});

    std::string id() const override { return "cloudwallet"; }
    LocaleDefinitions translations() const override;

    Result<WalletPluginLoginResponse> login(LoginContext& context) override;
    Result<WalletPluginSignResponse> sign(const ResolvedSigningRequest& resolved,
                                          TransactContext& context) override;
    Result<void> logout(const LogoutContext& context) override;
    bool hasLogout() const override { return true; }

    std::string url;
    std::chrono::milliseconds loginTimeout;
    std::shared_ptr<WebViewBridge> bridge;

private:
    Result<WalletPluginLoginResponse> waxLogin(LoginContext& context);
    Result<WalletPluginSignResponse> waxSign(const ResolvedSigningRequest& resolved,
                                             TransactContext& context);
    Result<cloudwallet::WAXCloudWalletSigningResponse> getWalletResponse(
        const ResolvedSigningRequest& resolved, TransactContext& context,
        const UserInterfaceTranslateFunction& t, std::chrono::milliseconds timeout);

    CancelToken token_;
};

}  // namespace dwarfkit
