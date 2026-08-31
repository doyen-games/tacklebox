#include <dwarfkit/plugins/wallet/anchor/transports.hpp>

#include <random>

#include <dwarfkit/protocol_esr/callback.hpp>
#include <dwarfkit/protocol_esr/sealed_messages.hpp>
#include <dwarfkit/protocol_esr/utils.hpp>

namespace dwarfkit::anchor {

namespace {

std::string translate(const Translator& t, const std::string& key,
                      const std::string& fallback) {
    if (t) {
        return t(key, {.defaultValue = fallback, .values = json::object()});
    }
    return fallback;
}

// Raw concatenation, not URL encoding: the deployed authenticator decodes the
// values unescaped.
std::string signPageUrl(const std::string& baseUrl,
                        const std::vector<std::pair<std::string, std::string>>& params) {
    std::string query;
    for (const auto& [key, value] : params) {
        if (!query.empty()) {
            query += "&";
        }
        query += key + "=" + value;
    }
    return baseUrl + "/sign?" + query;
}

EsrLoginContext esrLoginContext(const LoginContext& context) {
    return {context.appName.value_or(""), context.chain, context.chains,
            context.esrOptions()};
}

}  // namespace

Result<WalletPluginLoginResponse> NativeTransport::login(LoginContext& context,
                                                         const IdentityRequestBundle& bundle,
                                                         const Translator& t) const {
    if (!context.ui) {
        return err(ErrorKind::Invalid, "No UI available");
    }

    const std::string sameDeviceUri = bundle.sameDeviceRequest.encode(true, false, "esr:");

    std::vector<PromptElement> elements;
    // If we know this is NOT a mobile device, show the QR code
    if (!options_.knownMobile) {
        PromptElement qr;
        qr.type = PromptElementType::qr;
        qr.data = bundle.request.encode(true, false, "esr:");
        elements.push_back(qr);
    }
    PromptElement link;
    link.type = PromptElementType::link;
    link.label = translate(t, "login.link", "Launch Anchor");
    link.data = json{{"href", sameDeviceUri},
                     {"label", translate(t, "login.link", "Launch Anchor")},
                     {"variant", "primary"}};
    elements.push_back(link);

    // Automatically try to open the link (upstream window.location.href)
    if (options_.openLink) {
        options_.openLink(sameDeviceUri);
    }

    (void)context.ui->prompt(
        {.title = translate(t, "login.title", "Connect with Anchor"),
         .body = translate(t, "login.body",
                           "Scan with Anchor on your mobile device or click the button below "
                           "to open on this device."),
         .elements = elements},
        options_.token);

    auto callbackResponse = waitForCallback(bundle.callback, options_.buoyWs, options_.token);
    if (!callbackResponse) {
        return err(callbackResponse.error());
    }
    const json& payload = *callbackResponse;

    DK_CHECK(verifyLoginCallbackResponse(payload, esrLoginContext(context)));

    if (!payload.contains("cid") || !payload.contains("sa") || !payload.contains("sp")) {
        return err(ErrorKind::Invalid, "Invalid callback response");
    }

    json& data = *options_.data;
    const auto has = [&](const char* key) {
        return payload.contains(key) && payload[key].is_string() &&
               !payload[key].get_ref<const std::string&>().empty();
    };
    if (has("link_ch") && has("link_key") && has("link_name")) {
        data["requestKey"] = bundle.requestKey.toString();
        data["privateKey"] = bundle.privateKey.toString();
        data["signerKey"] = payload["link_key"];
        data["channelUrl"] = payload["link_ch"];
        data["channelName"] = payload["link_name"];

        if (has("link_meta")) {
            // link_meta is advisory; a malformed value must not fail the login
            const json metadata =
                json::parse(payload["link_meta"].get_ref<const std::string&>(), nullptr, false);
            if (metadata.is_object()) {
                if (metadata.contains("sameDevice")) {
                    data["sameDevice"] = metadata["sameDevice"];
                }
                if (metadata.contains("launchUrl")) {
                    data["launchUrl"] = metadata["launchUrl"];
                }
                if (metadata.contains("triggerUrl")) {
                    data["triggerUrl"] = metadata["triggerUrl"];
                }
            }
        }
    }

    DK_TRY(resolvedResponse,
           ResolvedSigningRequest::fromPayload(payload, context.esrOptions()));

    WalletPluginLoginResponse response;
    DK_TRY(chain, Checksum256::from(payload["cid"].get<std::string>()));
    response.chain = chain;
    response.permissionLevel = PermissionLevel{Name::from(payload["sa"].get<std::string>()),
                                               Name::from(payload["sp"].get<std::string>())};
    if (payload.contains("sig") && payload["sig"].is_string()) {
        DK_TRY(signature, Signature::from(payload["sig"].get<std::string>()));
        DK_TRY(proof, resolvedResponse.getIdentityProof(signature));
        response.identityProof = proof;
    }
    return response;
}

Result<WalletPluginSignResponse> NativeTransport::sign(const ResolvedSigningRequest& resolved,
                                                       TransactContext& context) const {
    if (!context.ui) {
        return err(ErrorKind::Invalid, "No UI available");
    }

    json& data = *options_.data;
    const auto t = context.ui->getTranslate(options_.id);

    const auto expiration = resolved.transaction.expiration;
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const auto expiresIn = std::chrono::milliseconds(expiration.toMilliseconds() - nowMs);

    DK_TRY(modifiedRequest,
           context.createRequest({.transaction = Serializer::objectify(resolved.transaction)}));

    DK_CHECK(modifiedRequest.setInfoKey("link", LinkInfo{expiration}));

    const auto callback = setTransactionCallback(modifiedRequest, options_.buoyUrl);

    const std::string request = modifiedRequest.encode(true, false);

    // Mobile will return true or false, desktop will return undefined
    const bool sameDevice = data.value("sameDevice", false);

    SigningRequest sameDeviceRequest = modifiedRequest;
    sameDeviceRequest.setInfoKey("same_device", true);
    if (const auto returnUrl = generateReturnUrl()) {
        sameDeviceRequest.setInfoKey("return_path", std::string_view(*returnUrl));
    }

    if (sameDevice && options_.openLink) {
        // upstream also deep-links anchor://link on Apple handhelds; there is
        // no user agent to sniff here
        const std::string launchUrl = data.value("launchUrl", "");
        if (!launchUrl.empty()) {
            options_.openLink(launchUrl);
        }
    }

    const std::string channelName = data.value("channelName", "");
    (void)context.ui->prompt(
        {.title = translate(t, "transact.title", "Complete using Anchor"),
         .body = translate(t, "transact.body",
                           "Please open your Anchor Wallet on \"" + channelName +
                               "\" to review and approve this transaction."),
         .elements = {PromptElement{
                          PromptElementType::countdown,
                          std::nullopt,
                          json{{"label", translate(t, "transact.await",
                                                   "Waiting for response from Anchor")},
                               {"end", expiration.toString()}}},
                      PromptElement{PromptElementType::qr, std::nullopt, json(request)},
                      PromptElement{
                          PromptElementType::link,
                          translate(t, "transact.label",
                                    "Sign manually or with another device"),
                          json{{"href", sameDeviceRequest.encode()},
                               {"label", translate(t, "transact.label",
                                                   "Sign manually or with another device")}}}}},
        options_.token);

    const std::string channelUrl = data.value("channelUrl", "");
    if (!channelUrl.empty()) {
        // service origin + channel path from the stored channel url
        const auto schemeEnd = channelUrl.find("://");
        const auto pathStart =
            channelUrl.find('/', schemeEnd == std::string::npos ? 0 : schemeEnd + 3);
        if (pathStart != std::string::npos) {
            const std::string service = channelUrl.substr(0, pathStart);
            const std::string channel = channelUrl.substr(pathStart + 1);
            DK_TRY(privateKey, PrivateKey::from(data.value("privateKey", "")));
            DK_TRY(signerKey, PublicKey::from(data.value("signerKey", "")));
            DK_TRY(sealedMessage,
                   sealMessage((sameDevice ? sameDeviceRequest : modifiedRequest)
                                   .encode(true, false, "esr:"),
                               privateKey, signerKey));
            DK_TRY(encoded, Serializer::encode(sealedMessage));
            // fire and forget, like the upstream un-awaited send()
            (void)buoy::send(std::span<const uint8_t>(encoded.array),
                             {.channel = channel,
                              .service = service,
                              .fetch = context.fetch.get()});
        }
    } else if (options_.openLink) {
        // If no channel is defined, fall back to the same device request and
        // trigger immediately
        options_.openLink(sameDeviceRequest.encode());
    }

    buoy::ReceiveOptions receiveOptions = callback;
    if (expiresIn.count() > 0) {
        receiveOptions.timeout = expiresIn;
    }
    auto callbackResponse = waitForCallback(receiveOptions, options_.buoyWs, options_.token);
    if (!callbackResponse) {
        if (callbackResponse.error().kind == ErrorKind::Transport) {
            return err(ErrorKind::Canceled,
                       translate(t, "error.expired", "The request expired, please try again."));
        }
        return err(callbackResponse.error());
    }
    const json& payload = *callbackResponse;

    DK_TRY(signatures, extractSignaturesFromCallback(payload));
    if (!isCallback(payload) || signatures.empty()) {
        return err(ErrorKind::Invalid,
                   translate(t, "error.not_completed", "The request was not completed."));
    }

    DK_TRY(resolvedRequest,
           ResolvedSigningRequest::fromPayload(payload, context.esrOptions()));

    WalletPluginSignResponse response;
    response.signatures = signatures;
    response.resolved = resolvedRequest;
    return response;
}

std::string WebTransport::loginUrl(const LoginContext& context,
                                   const IdentityRequestBundle& bundle,
                                   const std::string& baseUrl) const {
    return signPageUrl(baseUrl,
                       {{"esr", bundle.request.encode()},
                        {"chain", context.chain ? context.chain->id.hexString() : ""},
                        {"requestKey", bundle.requestKey.toString()}});
}

Result<WalletPluginLoginResponse> WebTransport::login(LoginContext& context,
                                                      const IdentityRequestBundle& bundle,
                                                      const std::string& baseUrl) const {
    if (!context.appName || context.appName->empty()) {
        context.appName = "Unknown App";
    }

    const auto t = context.ui ? context.ui->getTranslate(options_.id) : Translator();
    const std::string url = loginUrl(context, bundle, baseUrl);

    const bool opened = options_.openLink && options_.openLink(url);
    if (context.ui) {
        if (opened) {
            (void)context.ui->prompt(
                {.title = translate(t, "web.waiting.title", "Approve in Anchor"),
                 .body =
                     translate(t, "web.waiting.body", "Please approve this in the Anchor window."),
                 .elements = {}},
                options_.token);
        } else {
            // No way to open a window here: offer the URL as a link (the
            // upstream popup-blocked path).
            PromptElement link;
            link.type = PromptElementType::link;
            link.label = translate(t, "web.blocked.label", "Open Anchor");
            link.data = json{{"href", url},
                             {"label", translate(t, "web.blocked.label", "Open Anchor")},
                             {"variant", "primary"}};
            (void)context.ui->prompt(
                {.title = translate(t, "web.blocked.title", "Pop-up blocked"),
                 .body = translate(t, "web.blocked.body",
                                   "Pop-up blocked by your browser. Open the Anchor window "
                                   "manually."),
                 .elements = {link}},
                options_.token);
        }
    }

    auto callbackResponse = waitForCallback(bundle.callback, options_.buoyWs, options_.token);
    if (!callbackResponse) {
        return err(callbackResponse.error());
    }
    const json& payload = *callbackResponse;

    json& data = *options_.data;
    data["encryptionKey"] = bundle.privateKey.toString();
    if (payload.contains("link_key")) {
        data["messageKey"] = payload["link_key"];
    }

    if (!payload.contains("cid") || !payload["cid"].is_string()) {
        return err(ErrorKind::Invalid, "Login failed: No chain ID returned");
    }

    WalletPluginLoginResponse response;
    DK_TRY(chain, Checksum256::from(payload["cid"].get<std::string>()));
    response.chain = chain;
    response.permissionLevel =
        PermissionLevel{Name::from(payload.value("sa", "")), Name::from(payload.value("sp", ""))};

    if (payload.contains("sig") && payload["sig"].is_string()) {
        // Upstream returns a loose {signature, signedRequest} pair here; the
        // typed IdentityProof comes from resolving the payload instead (see
        // DIVERGENCES.md).
        DK_TRY(signature, Signature::from(payload["sig"].get<std::string>()));
        const auto resolvedResponse =
            ResolvedSigningRequest::fromPayload(payload, context.esrOptions());
        if (resolvedResponse) {
            DK_TRY(proof, resolvedResponse->getIdentityProof(signature));
            response.identityProof = proof;
        }
    }

    return response;
}

Result<WalletPluginSignResponse> WebTransport::sign(const ResolvedSigningRequest& resolved,
                                                    TransactContext& context,
                                                    const std::string& baseUrl) const {
    json& data = *options_.data;
    if (data.value("encryptionKey", "").empty() || data.value("messageKey", "").empty()) {
        return err(ErrorKind::Invalid, "No request keys available - please login first");
    }

    const auto t = context.ui ? context.ui->getTranslate(options_.id) : Translator();

    DK_TRY(modifiedRequest,
           context.createRequest({.transaction = Serializer::objectify(resolved.transaction)}));
    DK_CHECK(modifiedRequest.setInfoKey("link", LinkInfo{resolved.transaction.expiration}));

    const auto callback = setTransactionCallback(modifiedRequest, options_.buoyUrl);

    std::random_device rd;
    std::uniform_int_distribution<uint64_t> dist(0, (uint64_t(1) << 53) - 1);
    const uint64_t nonce = dist(rd);

    DK_TRY(encryptionKey, PrivateKey::from(data.value("encryptionKey", "")));
    DK_TRY(messageKey, PublicKey::from(data.value("messageKey", "")));
    DK_TRY(sealedRequest,
           sealMessage(modifiedRequest.encode(), encryptionKey, messageKey, nonce));
    DK_TRY(requestKey, encryptionKey.toPublic());

    // Bare ciphertext, not the serialized SealedMessage; that is the format
    // the authenticator decodes.
    const std::string signUrl = signPageUrl(
        baseUrl,
        {{"sealed", sealedRequest.ciphertext.hexString()},
         {"nonce", std::to_string(nonce)},
         {"chain", context.chain.id.hexString()},
         {"accountName", context.accountName().toString()},
         {"permissionName", context.permissionName().toString()},
         {"appName", context.appName.value_or("")},
         {"requestKey", requestKey.toString()}});

    const bool opened = options_.openLink && options_.openLink(signUrl);
    if (context.ui) {
        if (opened) {
            (void)context.ui->prompt(
                {.title = translate(t, "web.waiting.title", "Approve in Anchor"),
                 .body =
                     translate(t, "web.waiting.body", "Please approve this in the Anchor window."),
                 .elements = {}},
                options_.token);
        } else {
            PromptElement link;
            link.type = PromptElementType::link;
            link.label = translate(t, "web.blocked.label", "Open Anchor");
            link.data = json{{"href", signUrl},
                             {"label", translate(t, "web.blocked.label", "Open Anchor")},
                             {"variant", "primary"}};
            (void)context.ui->prompt(
                {.title = translate(t, "web.blocked.title", "Pop-up blocked"),
                 .body = translate(t, "web.blocked.body",
                                   "Pop-up blocked by your browser. Open the Anchor window "
                                   "manually."),
                 .elements = {link}},
                options_.token);
        }
    }

    auto callbackResponse = waitForCallback(callback, options_.buoyWs, options_.token);
    if (!callbackResponse) {
        return err(callbackResponse.error());
    }
    const json& payload = *callbackResponse;

    DK_TRY(signatures, extractSignaturesFromCallback(payload));
    if (!isCallback(payload) || signatures.empty()) {
        return err(ErrorKind::Invalid, "Signing failed: No signatures returned");
    }

    WalletPluginSignResponse response;
    response.signatures = signatures;
    response.resolved = resolved;
    return response;
}

}  // namespace dwarfkit::anchor
