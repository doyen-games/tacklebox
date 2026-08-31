#include <dwarfkit/protocol_esr/esr.hpp>

#include <algorithm>

#include <dwarfkit/core/version.hpp>

namespace dwarfkit {

Result<IdentityRequestResponse> createIdentityRequest(const EsrLoginContext& context,
                                                      const std::string& buoyUrl) {
    // a new private and public key to act as the request key
    DK_TRY(privateKey, PrivateKey::generate(KeyType::K1));
    DK_TRY(requestKey, privateKey.toPublic());

    // a new BuoySession struct to be used as the info field
    BuoySession createInfo;
    createInfo.session_name = Name::from(context.appName);
    createInfo.request_key = requestKey;
    createInfo.user_agent = getUserAgent();

    // whether this is a multichain request
    const bool isMultiChain = !(context.chain || context.chains.size() == 1);

    const buoy::ReceiveOptions callbackChannel = prepareCallbackChannel(buoyUrl);

    // the chain id(s) to use; no chain resolves to a TS null chainId, which
    // maps to anyChain here
    std::optional<ChainId> chainId;
    if (!isMultiChain && context.chain) {
        chainId = ChainId::from(context.chain->id);
    }
    std::vector<ChainId> chainIds;
    if (isMultiChain) {
        for (const auto& chain : context.chains) {
            chainIds.push_back(ChainId::from(chain.id));
        }
    }

    SigningRequestCreateIdentityArguments args;
    args.callback = prepareCallback(callbackChannel);
    args.scope = Name::from(context.appName);
    args.chainId = chainId;
    args.anyChain = !chainId.has_value();
    args.chainIds = std::move(chainIds);
    DK_TRY(link, Serializer::encode(createInfo));
    args.info.push_back({"link", link});
    args.info.push_back(
        {"scope", Bytes(std::vector<uint8_t>(context.appName.begin(), context.appName.end()))});

    DK_TRY(request, SigningRequest::identity(args, context.esrOptions));

    // without a browser there is no same-device return path; the clone stays
    // identical (upstream only adds same_device/return_path when window exists)
    SigningRequest sameDeviceRequest = request.clone();

    IdentityRequestResponse rv;
    rv.callback = callbackChannel;
    rv.request = std::move(request);
    rv.sameDeviceRequest = std::move(sameDeviceRequest);
    rv.requestKey = requestKey;
    rv.privateKey = privateKey;
    return rv;
}

buoy::ReceiveOptions setTransactionCallback(SigningRequest& request, const std::string& buoyUrl) {
    const buoy::ReceiveOptions callback = prepareCallbackChannel(buoyUrl);
    request.setCallback(callback.service + "/" + callback.channel, true);
    return callback;
}

std::string getUserAgent() {
    // upstream: "@wharfkit/protocol-esr __ver" plus the platform user agent
    return "@wharfkit/protocol-esr 1.6.1 dwarfkit/" + std::string(version());
}

CallbackType prepareCallback(const buoy::ReceiveOptions& callbackChannel) {
    return {callbackChannel.service + "/" + callbackChannel.channel, true};
}

buoy::ReceiveOptions prepareCallbackChannel(const std::string& buoyUrl) {
    buoy::ReceiveOptions rv;
    rv.service = buoyUrl;
    rv.channel = uuid();
    return rv;
}

Result<void> verifyLoginCallbackResponse(const json& callbackResponse,
                                         const EsrLoginContext& context) {
    const bool hasSig = callbackResponse.contains("sig") && callbackResponse["sig"].is_string() &&
                        !callbackResponse["sig"].get_ref<const std::string&>().empty();
    if (!hasSig) {
        return err(ErrorKind::Invalid, "Invalid response, must have at least one signature");
    }
    if (!context.chain && context.chains.size() > 1) {
        if (!callbackResponse.contains("cid")) {
            return err(ErrorKind::Invalid,
                       "Multi chain response payload must specify resolved chain id (cid)");
        }
    } else if (context.chain || !context.chains.empty()) {
        const ChainDefinition& chain = context.chain ? *context.chain : context.chains[0];
        if (callbackResponse.contains("cid") &&
            callbackResponse["cid"] != chain.id.hexString()) {
            return err(ErrorKind::Invalid, "Got response for wrong chain id");
        }
    }
    return {};
}

Result<std::vector<Signature>> extractSignaturesFromCallback(const json& payload) {
    std::vector<std::string> signatures;
    const auto pushTruthy = [&](const json& value) {
        if (value.is_string() && !value.get_ref<const std::string&>().empty()) {
            signatures.push_back(value.get<std::string>());
        }
    };
    if (payload.contains("sig")) {
        pushTruthy(payload["sig"]);
    }
    // tolerate zero-based (sig0) and one-based (sig1, swift-eosio) numbering,
    // including gaps
    std::vector<std::pair<long, std::string>> indexed;
    for (const auto& item : payload.items()) {
        const std::string& key = item.key();
        if (key.size() > 3 && key.compare(0, 3, "sig") == 0 &&
            std::all_of(key.begin() + 3, key.end(),
                        [](char c) { return c >= '0' && c <= '9'; })) {
            indexed.push_back({std::stol(key.substr(3)), key});
        }
    }
    std::sort(indexed.begin(), indexed.end());
    for (const auto& [index, key] : indexed) {
        pushTruthy(payload[key]);
    }
    // deduplicate preserving first-occurrence order, then parse
    std::vector<Signature> rv;
    std::vector<std::string> seen;
    for (const auto& sig : signatures) {
        if (std::find(seen.begin(), seen.end(), sig) != seen.end()) {
            continue;
        }
        seen.push_back(sig);
        DK_TRY(parsed, Signature::from(sig));
        rv.push_back(std::move(parsed));
    }
    return rv;
}

bool isCallback(const json& object) { return object.is_object() && object.contains("tx"); }

}  // namespace dwarfkit
