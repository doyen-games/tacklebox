#include <dwarfkit/plugins/wallet/cloudwallet.hpp>

#include <dwarfkit/core/base64.hpp>
#include <dwarfkit/signing_request/identity_proof.hpp>

namespace dwarfkit {

using cloudwallet::WAXCloudWalletLoginResponse;
using cloudwallet::WAXCloudWalletSigningResponse;

namespace {

std::string translate(const UserInterfaceTranslateFunction& t, const std::string& key,
                      const std::string& fallback, const json& values = json::object()) {
    if (t) {
        return t(key, {.defaultValue = fallback, .values = values});
    }
    return fallback;
}

// Map a bridge wait error to the upstream popup error strings.
Error mapBridgeError(const Error& error, const UserInterfaceTranslateFunction& t,
                     std::chrono::milliseconds timeout) {
    const bool isTimeout = error.kind == ErrorKind::Transport &&
                           error.details.value("code", "") == "E_TIMEOUT";
    if (isTimeout) {
        return Error{ErrorKind::Canceled,
                     translate(t, "error.timeout",
                               "The request has timed out after " +
                                   std::to_string(timeout.count() / 1000) +
                                   " seconds. Please try again.",
                               json{{"timeout", timeout.count() / 1000}}),
                     {}};
    }
    if (error.kind == ErrorKind::Canceled) {
        return Error{ErrorKind::Canceled,
                     translate(t, "error.closed",
                               "The Cloud Wallet was closed before the request was completed"),
                     {}};
    }
    return error;
}

int64_t getCurrentTime() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

Result<WAXCloudWalletLoginResponse> popupLogin(const UserInterfaceTranslateFunction& t,
                                               WebViewBridge& bridge, const std::string& url,
                                               std::chrono::milliseconds timeout,
                                               CancelToken token) {
    const auto opened = bridge.open(url);
    if (!opened) {
        return err(ErrorKind::Invalid,
                   translate(t, "error.popup",
                             "Unable to open the popup window. Check your browser settings and "
                             "try again."));
    }
    auto message = bridge.awaitMessage(timeout, token);
    bridge.close();
    if (!message) {
        return err(mapBridgeError(message.error(), t, timeout));
    }
    return WAXCloudWalletLoginResponse::from(*message);
}

Result<WAXCloudWalletSigningResponse> popupTransact(const UserInterfaceTranslateFunction& t,
                                                    WebViewBridge& bridge,
                                                    const std::string& url,
                                                    const ResolvedSigningRequest& request,
                                                    std::chrono::milliseconds timeout,
                                                    CancelToken token) {
    const auto opened = bridge.open(url);
    if (!opened) {
        return err(ErrorKind::Invalid,
                   translate(t, "error.popup",
                             "Unable to open the popup window. Check your browser settings and "
                             "try again."));
    }
    // The page posts a ready event first; answer it with the transaction.
    auto readyEvent = bridge.awaitMessage(timeout, token);
    if (!readyEvent) {
        bridge.close();
        return err(mapBridgeError(readyEvent.error(), t, timeout));
    }
    DK_TRY(serialized, Serializer::encode(request.transaction));
    const auto posted =
        bridge.postMessage(json{{"feeFallback", true},
                                {"freeBandwidth", true},
                                {"startTime", getCurrentTime()},
                                {"transaction", serialized.hexString()},
                                {"type", "TRANSACTION"},
                                {"version", cloudwalletVersion}});
    if (!posted) {
        bridge.close();
        return err(posted.error());
    }
    auto message = bridge.awaitMessage(timeout, token);
    bridge.close();
    if (!message) {
        return err(mapBridgeError(message.error(), t, timeout));
    }
    return WAXCloudWalletSigningResponse::from(*message);
}

WalletPluginCloudWallet::WalletPluginCloudWallet(const WalletPluginCloudWalletOptions& options)
    : url(options.url), loginTimeout(options.loginTimeout), bridge(options.bridge),
      token_(options.token) {
    config_ = {.requiresChainSelect = false,
               .requiresPermissionSelect = false,
               .supportedChains = {
                   // WAX (Mainnet)
                   "1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4",
                   // WAX (Testnet) - new wallet
                   "f16b1833c747c43682f4386fca9cbb327929334a762755ebec17f6f23c9b8a12",
               }};
    if (!options.supportedChains.empty()) {
        config_.supportedChains = options.supportedChains;
    }
    metadata_ = WalletPluginMetadata::from(
        json{{"name", "Cloud Wallet"},
             {"description",
              "Own your keys with a mnemonic phrase and use passkeys for faster, safer access - "
              "all in one wallet built for the WAX ecosystem."},
             {"homepage", "https://www.mycloudwallet.com"},
             {"download", "https://www.mycloudwallet.com"}});
}

Result<WalletPluginLoginResponse> WalletPluginCloudWallet::login(LoginContext& context) {
    return waxLogin(context);
}

Result<WalletPluginLoginResponse> WalletPluginCloudWallet::waxLogin(LoginContext& context) {
    if (!context.chain) {
        return err(ErrorKind::Invalid, "A chain must be selected to login with.");
    }
    if (!bridge) {
        return err(ErrorKind::Invalid, "The Cloud Wallet requires a WebViewBridge.");
    }
    const auto t = context.ui ? context.ui->getTranslate(id())
                              : UserInterfaceTranslateFunction();

    // Create common search parameters
    std::string search = "v=";
    search += cloudwalletVersion;
    if (context.arbitrary.contains("nonce") && context.arbitrary["nonce"].is_string()) {
        const auto& nonce = context.arbitrary["nonce"].get_ref<const std::string&>();
        search += "&n=" + base64Encode(std::span<const uint8_t>(
                              reinterpret_cast<const uint8_t*>(nonce.data()), nonce.size()));
    }
    const std::string popupLoginUrl = url + "/cloud-wallet/login?" + search;

    DK_TRY(response, popupLogin(t, *bridge, popupLoginUrl, loginTimeout, token_));

    if (!response.verified) {
        return err(ErrorKind::Canceled,
                   translate(t, "error.closed",
                             "Cloud Wallet closed before the login was completed"));
    }

    std::optional<IdentityProof> identityProof;
    if (response.proof.is_object() && response.proof.contains("data") &&
        response.proof["data"].is_object() &&
        response.proof["data"].contains("signature")) {
        DK_TRY(proof, IdentityProof::from(response.proof["data"]));
        identityProof = proof;
    }

    // upstream: localStorage.setItem('connectedType', 'web')
    data_["connectedType"] = "web";

    WalletPluginLoginResponse rv;
    rv.chain = context.chain->id;
    rv.permissionLevel = PermissionLevel{Name::from(response.userAccount),
                                         Name::from(response.permission.value_or("active"))};
    rv.identityProof = identityProof;
    return rv;
}

Result<WalletPluginSignResponse> WalletPluginCloudWallet::sign(
    const ResolvedSigningRequest& resolved, TransactContext& context) {
    return waxSign(resolved, context);
}

Result<WalletPluginSignResponse> WalletPluginCloudWallet::waxSign(
    const ResolvedSigningRequest& resolved, TransactContext& context) {
    if (!context.ui) {
        return err(ErrorKind::Invalid, "A UserInterface must be defined to sign transactions.");
    }
    if (!bridge) {
        return err(ErrorKind::Invalid, "The Cloud Wallet requires a WebViewBridge.");
    }
    const auto t = context.ui->getTranslate(id());

    // Set expiration time frames for the request
    const auto expiration = resolved.transaction.expiration;
    const int64_t timeoutMs = expiration.toMilliseconds() - getCurrentTime();
    const auto timeout = timeoutMs > 0 ? std::chrono::milliseconds(timeoutMs) : loginTimeout;

    // Tell Wharf we need to prompt the user with a countdown
    (void)context.ui->prompt(
        {.title = "Sign",
         .body = "Please complete the transaction using the Cloud Wallet popup window.",
         .optional = true,
         .elements = {PromptElement{PromptElementType::countdown, std::nullopt,
                                    json(expiration.toString())}}},
        token_);

    DK_TRY(callbackResponse, getWalletResponse(resolved, context, t, timeout));

    // The response to return to the Session Kit
    WalletPluginSignResponse result;
    result.signatures = callbackResponse.signatures;

    // If a transaction was returned by the Cloud Wallet
    if (callbackResponse.serializedTransaction) {
        // Convert the serialized transaction from the Cloud Wallet
        DK_TRY(responseTransaction,
               Serializer::decode<Transaction>(callbackResponse.serializedTransaction->array));

        // Determine if the transaction changed from the requested transaction
        if (!responseTransaction.equals(resolved.transaction)) {
            // Evaluate whether modifications are valid, if not error
            DK_CHECK(cloudwallet::validateModifications(resolved.transaction,
                                                        responseTransaction));
            // If transaction modified, return a new resolved request to Wharf
            SigningRequestCreateArguments createArgs;
            createArgs.transaction = Serializer::objectify(responseTransaction);
            DK_TRY(request, SigningRequest::create(createArgs, context.esrOptions()));
            DK_TRY(resolvedTransaction,
                   abi_traits<ResolvedTransaction>::fromJSON(
                       Serializer::objectify(responseTransaction)));
            result.resolved =
                ResolvedSigningRequest(request, context.permissionLevel, responseTransaction,
                                       resolvedTransaction, ChainId(context.chain.id));
        }
    } else {
        // upstream isCallback requires the serializedTransaction key
        return err(ErrorKind::Invalid, "The Cloud Wallet failed to respond");
    }

    return result;
}

Result<WAXCloudWalletSigningResponse> WalletPluginCloudWallet::getWalletResponse(
    const ResolvedSigningRequest& resolved, TransactContext& context,
    const UserInterfaceTranslateFunction& t, std::chrono::milliseconds timeout) {
    if (!context.ui) {
        return err(ErrorKind::Invalid, "The Cloud Wallet requires a UI to sign transactions.");
    }
    DK_TRY(response, popupTransact(t, *bridge, url + "/cloud-wallet/signing/", resolved,
                                   timeout, token_));

    // Ensure the response is verified, if not the user most likely cancelled
    // the request
    if (!response.verified) {
        return err(ErrorKind::Canceled,
                   translate(t, "error.closed",
                             "The Cloud Wallet was closed before the request was completed"));
    }
    return response;
}

Result<void> WalletPluginCloudWallet::logout(const LogoutContext&) {
    return {};
}

}  // namespace dwarfkit
