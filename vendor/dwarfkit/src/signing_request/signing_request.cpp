#include <dwarfkit/signing_request/signing_request.hpp>

#include <algorithm>
#include <chrono>

#include <dwarfkit/core/zlib.hpp>
#include <dwarfkit/signing_request/identity_proof.hpp>

namespace dwarfkit {

namespace {

bool isBytesValue(const json& data) {
    if (data.is_string()) return true;
    if (data.is_array()) {
        return std::all_of(data.begin(), data.end(),
                           [](const json& item) { return item.is_number(); });
    }
    return false;
}

// encodeAction from signing-request.ts: actions with object data need an ABI
Result<json> encodeActionJson(const json& action, const AbiMap& abis) {
    if (action.contains("data") && isBytesValue(action.at("data"))) {
        DK_TRY(typed, Action::from(action));
        return Serializer::objectify(typed);
    }
    const std::string account = action.value("account", "");
    const auto abi = abis.find(Name::from(account).toString());
    if (abi == abis.end()) {
        return err(ErrorKind::Invalid, "Missing ABI for " + account);
    }
    DK_TRY(typed, Action::from(action, abi->second));
    return Serializer::objectify(typed);
}

bool isIdentityAction(const Action& action) {
    return action.account.value == 0 && action.name == Name::from("identity");
}

bool hasTapos(const Transaction& tx) {
    return !(tx.expiration.value == 0 && tx.ref_block_num == 0 && tx.ref_block_prefix == 0);
}

TimePointSec expirationTime(const std::optional<TimePointSec>& timestamp,
                            std::optional<uint32_t> expireSeconds) {
    const TimePointSec ts = timestamp ? *timestamp
                                      : TimePointSec::fromMilliseconds(static_cast<double>(
                                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch())
                                                .count()));
    return TimePointSec(ts.value + expireSeconds.value_or(60));
}

template <class F>
auto visitData(const std::variant<RequestDataV2, RequestDataV3>& data, F&& f) {
    return std::visit(std::forward<F>(f), data);
}

}  // namespace

ABI SigningRequest::identityAbi(int version) {
    ABI abi = version == 2 ? Serializer::synthesize<IdentityV2>()
                           : Serializer::synthesize<IdentityV3>();
    abi.actions = {{Name::from("identity"), "identity", ""}};
    return abi;
}

SigningRequest::SigningRequest(int version, std::variant<RequestDataV2, RequestDataV3> data,
                               const SigningRequestEncodingOptions& options,
                               std::optional<RequestSignature> signature)
    : version(version),
      data(std::move(data)),
      signature(std::move(signature)),
      zlib_(options.zlib),
      abiProvider_(options.abiProvider) {}

Result<SigningRequest> SigningRequest::create(const SigningRequestCreateArguments& args,
                                              const SigningRequestEncodingOptions& options,
                                              const AbiMap& presetAbis) {
    int version = 2;

    // collect the actions that need ABI resolution
    std::vector<json> actionList;
    if (args.action) {
        actionList.push_back(*args.action);
    } else if (args.actions) {
        for (const auto& action : *args.actions) actionList.push_back(action);
    } else if (args.transaction && args.transaction->contains("actions")) {
        for (const auto& action : args.transaction->at("actions")) actionList.push_back(action);
    }
    AbiMap abis = presetAbis;
    for (const auto& action : actionList) {
        if (action.contains("data") && !isBytesValue(action.at("data"))) {
            const Name account = Name::from(action.value("account", ""));
            if (!abis.contains(account.toString())) {
                if (!options.abiProvider) {
                    return err(ErrorKind::Invalid, "Missing abi provider");
                }
                DK_TRY(abi, options.abiProvider->getAbi(account));
                abis.emplace(account.toString(), std::move(abi));
            }
        }
    }

    // multi-chain requests require version 3
    if (args.anyChain) {
        version = 3;
    }
    if (args.identity && args.identity->contains("scope")) {
        version = 3;
    }

    // set the request data
    json reqValue;
    std::string reqType;
    if (args.identity) {
        reqType = "identity";
        reqValue = *args.identity;
    } else if (args.action && !args.actions && !args.transaction) {
        reqType = "action";
        DK_TRY(encoded, encodeActionJson(*args.action, abis));
        reqValue = std::move(encoded);
    } else if (args.actions && !args.action && !args.transaction) {
        if (args.actions->size() == 1) {
            reqType = "action";
            DK_TRY(encoded, encodeActionJson(args.actions->at(0), abis));
            reqValue = std::move(encoded);
        } else {
            reqType = "action[]";
            reqValue = json::array();
            for (const auto& action : *args.actions) {
                DK_TRY(encoded, encodeActionJson(action, abis));
                reqValue.push_back(std::move(encoded));
            }
        }
    } else if (args.transaction && !args.action && !args.actions) {
        reqType = "transaction";
        json tx = *args.transaction;
        // set default values if missing
        const auto defaults = json{{"expiration", "1970-01-01T00:00:00.000"},
                                   {"ref_block_num", 0},
                                   {"ref_block_prefix", 0},
                                   {"context_free_actions", json::array()},
                                   {"transaction_extensions", json::array()},
                                   {"delay_sec", 0},
                                   {"max_cpu_usage_ms", 0},
                                   {"max_net_usage_words", 0},
                                   {"actions", json::array()}};
        for (const auto& [key, value] : defaults.items()) {
            if (!tx.contains(key)) tx[key] = value;
        }
        json encodedActions = json::array();
        for (const auto& action : tx.at("actions")) {
            DK_TRY(encoded, encodeActionJson(action, abis));
            encodedActions.push_back(std::move(encoded));
        }
        tx["actions"] = std::move(encodedActions);
        reqValue = std::move(tx);
    } else {
        return err(ErrorKind::Invalid,
                   "Invalid arguments: Must have exactly one of action, actions or transaction");
    }

    // the chain id variant
    ChainIdVariant chainIdVariant{ChainAlias(uint8_t(0))};
    if (!args.anyChain) {
        ChainId id;
        if (args.chainId) {
            id = *args.chainId;
        } else {
            DK_TRY(eos, ChainId::from(ChainName::EOS));
            id = eos;
        }
        chainIdVariant = id.chainVariant();
    }

    // request flags and callback
    RequestFlags flags;
    std::string callback;
    flags.setBroadcast(args.broadcast ? *args.broadcast : reqType != "identity");
    if (args.callback) {
        callback = args.callback->url;
        flags.setBackground(args.callback->background);
    }

    // info pairs
    std::vector<InfoPair> info = args.info;
    if (!args.chainIds.empty() && args.anyChain) {
        std::vector<ChainIdVariant> ids;
        for (const auto& id : args.chainIds) {
            ids.push_back(id.chainVariant());
        }
        DK_TRY(encoded, Serializer::encode(ids));
        info.push_back({"chain_ids", encoded});
    }

    // build the typed request data for the version
    const auto build = [&]<class Data>(Data) -> Result<SigningRequest> {
        Data typed;
        typed.chain_id = chainIdVariant;
        typed.flags = flags;
        typed.callback = callback;
        typed.info = info;
        using ReqVariant = decltype(typed.req);
        json reqJson = json::array({reqType, reqValue});
        DK_TRY(req, ReqVariant::from(reqJson));
        typed.req = std::move(req);
        SigningRequest request(version, std::move(typed), options);
        if (request.isIdentity() &&
            visitData(request.data, [](const auto& d) { return d.flags.broadcast(); })) {
            return err(ErrorKind::Invalid, "Invalid request (identity request cannot be broadcast)");
        }
        if (options.signatureProvider) {
            DK_CHECK(request.sign(*options.signatureProvider));
        }
        return request;
    };
    if (version == 2) {
        return build(RequestDataV2{});
    }
    return build(RequestDataV3{});
}

Result<SigningRequest> SigningRequest::identity(const SigningRequestCreateIdentityArguments& args,
                                                const SigningRequestEncodingOptions& options) {
    SigningRequestCreateArguments createArgs;
    createArgs.chainId = args.chainId;
    createArgs.anyChain = args.anyChain;
    createArgs.chainIds = args.chainIds;
    createArgs.callback = args.callback;
    createArgs.broadcast = false;
    createArgs.info = args.info;

    json identity = json::object();
    const Name actor = args.account.value_or(PlaceholderName);
    const Name permission = args.permission.value_or(PlaceholderPermission);
    if (!(actor == PlaceholderName && permission == PlaceholderPermission)) {
        identity["permission"] = {{"actor", actor.toString()},
                                  {"permission", permission.toString()}};
    }
    if (args.scope) {
        identity["scope"] = args.scope->toString();
    }
    createArgs.identity = identity;
    return create(createArgs, options);
}

Result<SigningRequest> SigningRequest::fromTransaction(
    const ChainId& chainId, std::span<const uint8_t> serializedTransaction,
    const SigningRequestEncodingOptions& options) {
    ABIEncoder encoder;
    encoder.writeByte(2);  // header
    DK_CHECK(abi_traits<ChainIdVariant>::toABI(chainId.chainVariant(), encoder));
    encoder.writeByte(2);  // transaction variant
    encoder.writeArray(serializedTransaction);
    encoder.writeByte(RequestFlags::broadcastFlag);
    encoder.writeByte(0);  // callback
    encoder.writeByte(0);  // info
    return fromData(encoder.getData(), options);
}

Result<SigningRequest> SigningRequest::from(std::string_view uri,
                                            const SigningRequestEncodingOptions& options) {
    const size_t colon = uri.find(':');
    if (colon == std::string_view::npos) {
        return err(ErrorKind::Invalid, "Invalid request uri");
    }
    std::string_view path = uri.substr(colon + 1);
    if (path.starts_with("//")) {
        path = path.substr(2);
    }
    const auto data = base64u::decode(path);
    return fromData(data, options);
}

Result<SigningRequest> SigningRequest::fromData(std::span<const uint8_t> data,
                                                const SigningRequestEncodingOptions& options) {
    if (data.empty()) {
        return err(ErrorKind::Invalid, "Invalid request data");
    }
    const uint8_t header = data[0];
    const int version = header & ~(1 << 7);
    if (version != 2 && version != 3) {
        return err(ErrorKind::Invalid, "Unsupported protocol version");
    }
    std::vector<uint8_t> payload(data.begin() + 1, data.end());
    if ((header & (1 << 7)) != 0) {
        DK_TRY(inflated, inflateRaw(payload));
        payload = std::move(inflated);
    }
    ABIDecoder decoder(payload);
    std::variant<RequestDataV2, RequestDataV3> requestData{RequestDataV2{}};
    if (version == 2) {
        DK_TRY(decoded, abi_traits<RequestDataV2>::fromABI(decoder));
        requestData = std::move(decoded);
    } else {
        DK_TRY(decoded, abi_traits<RequestDataV3>::fromABI(decoder));
        requestData = std::move(decoded);
    }
    std::optional<RequestSignature> signature;
    if (decoder.canRead()) {
        DK_TRY(sig, abi_traits<RequestSignature>::fromABI(decoder));
        signature = std::move(sig);
    }
    SigningRequest request(version, std::move(requestData), options, std::move(signature));
    if (request.isIdentity() &&
        visitData(request.data, [](const auto& d) { return d.flags.broadcast(); })) {
        return err(ErrorKind::Invalid, "Invalid request (identity request cannot be broadcast)");
    }
    return request;
}

Result<void> SigningRequest::sign(SignatureProvider& signatureProvider) {
    DK_TRY(sig, signatureProvider.sign(getSignatureDigest()));
    signature = std::move(sig);
    return {};
}

Checksum256 SigningRequest::getSignatureDigest() const {
    // protocol version + utf8 "request"
    Bytes prefix(std::vector<uint8_t>{static_cast<uint8_t>(version), 0x72, 0x65, 0x71, 0x75, 0x65,
                                      0x73, 0x74});
    return Checksum256::hash(prefix.appending(getData()));
}

Result<void> SigningRequest::setSignature(std::string_view signer, std::string_view sig) {
    DK_TRY(parsed, Signature::from(sig));
    signature = RequestSignature{Name::from(signer), std::move(parsed)};
    return {};
}

void SigningRequest::setCallback(const std::string& url, bool background) {
    std::visit(
        [&](auto& d) {
            d.callback = url;
            d.flags.setBackground(background);
        },
        data);
}

void SigningRequest::setBroadcast(bool broadcast) {
    std::visit([&](auto& d) { d.flags.setBroadcast(broadcast); }, data);
}

std::string SigningRequest::encode(std::optional<bool> compress, bool slashes,
                                   std::string scheme) const {
    const bool shouldCompress = compress.value_or(zlib_);
    uint8_t header = static_cast<uint8_t>(version);
    const Bytes requestData = getData();
    const Bytes sigData = getSignatureData();
    std::vector<uint8_t> array = requestData.array;
    array.insert(array.end(), sigData.array.begin(), sigData.array.end());
    if (shouldCompress) {
        const auto deflated = deflateRaw(array);
        if (deflated && array.size() > deflated->size()) {
            header |= 1 << 7;
            array = *deflated;
        }
    }
    std::vector<uint8_t> out;
    out.reserve(1 + array.size());
    out.push_back(header);
    out.insert(out.end(), array.begin(), array.end());
    if (slashes) {
        scheme += "//";
    }
    return scheme + base64u::encode(out);
}

Bytes SigningRequest::getData() const {
    ABIEncoder encoder;
    visitData(data, [&](const auto& d) {
        (void)abi_traits<std::decay_t<decltype(d)>>::toABI(d, encoder);
    });
    return encoder.getBytes();
}

Bytes SigningRequest::getSignatureData() const {
    if (!signature) {
        return Bytes();
    }
    ABIEncoder encoder;
    (void)abi_traits<RequestSignature>::toABI(*signature, encoder);
    return encoder.getBytes();
}

std::vector<Name> SigningRequest::getRequiredAbis() const {
    std::vector<Name> rv;
    const auto actions = getRawActions();
    if (!actions) return rv;
    for (const auto& action : *actions) {
        if (isIdentityAction(action)) continue;
        if (std::find(rv.begin(), rv.end(), action.account) == rv.end()) {
            rv.push_back(action.account);
        }
    }
    return rv;
}

bool SigningRequest::requiresTapos() const {
    const auto tx = getRawTransaction();
    return !isIdentity() && tx && !hasTapos(*tx);
}

Result<AbiMap> SigningRequest::fetchAbis(AbiProvider* abiProvider) const {
    const auto required = getRequiredAbis();
    AbiMap abis;
    if (!required.empty()) {
        AbiProvider* provider = abiProvider ? abiProvider : abiProvider_;
        if (!provider) {
            return err(ErrorKind::Invalid, "Missing ABI provider");
        }
        for (const auto& account : required) {
            DK_TRY(abi, provider->getAbi(account));
            abis.emplace(account.toString(), std::move(abi));
        }
    }
    return abis;
}

Result<std::vector<ResolvedAction>> SigningRequest::resolveActions(
    const AbiMap& abis, const std::optional<PermissionLevel>& signer) const {
    DK_TRY(rawActions, getRawActions());
    std::vector<ResolvedAction> rv;
    for (const auto& rawAction : rawActions) {
        ABI abi;
        if (isIdentityAction(rawAction)) {
            abi = identityAbi(version);
        } else {
            const auto found = abis.find(rawAction.account.toString());
            if (found == abis.end()) {
                return err(ErrorKind::Invalid,
                           "Missing ABI definition for " + rawAction.account.toString());
            }
            abi = found->second;
        }
        const auto type = abi.getActionType(rawAction.name);
        if (!type) {
            return err(ErrorKind::Invalid, "Missing type for action " +
                                               rawAction.account.toString() + ":" +
                                               rawAction.name.toString() + " in ABI");
        }
        DK_TRY(decodedData, rawAction.decodeData(abi));
        json actionData = std::move(decodedData);
        std::vector<PermissionLevel> authorization = rawAction.authorization;
        if (signer) {
            const std::string actorName = signer->actor.toString();
            const std::string permissionName = signer->permission.toString();
            // placeholder resolution walks the decoded JSON; a string exactly
            // matching a placeholder resolves (upstream checks Name instances)
            const std::function<void(json&)> resolve = [&](json& value) {
                if (value.is_string()) {
                    const auto& s = value.get_ref<const std::string&>();
                    if (s == "............1") {
                        value = actorName;
                    } else if (s == "............2") {
                        value = permissionName;
                    }
                } else if (value.is_array() || value.is_object()) {
                    for (auto& item : value) {
                        resolve(item);
                    }
                }
            };
            resolve(actionData);
            for (auto& auth : authorization) {
                if (auth.actor == PlaceholderName) {
                    auth.actor = signer->actor;
                }
                if (auth.permission == PlaceholderPermission) {
                    auth.permission = signer->permission;
                }
                // backwards compatibility, actor placeholder also resolves to
                // permission when used in auth
                if (auth.permission == PlaceholderName) {
                    auth.permission = signer->permission;
                }
            }
        }
        rv.push_back(ResolvedAction{.account = rawAction.account,
                                    .name = rawAction.name,
                                    .authorization = std::move(authorization),
                                    .data = std::move(actionData)});
    }
    return rv;
}

Result<ResolvedTransaction> SigningRequest::resolveTransaction(const AbiMap& abis,
                                                               const PermissionLevel& signer,
                                                               const TransactionContext& ctx) const {
    DK_TRY(tx, getRawTransaction());
    if (!isIdentity() && !hasTapos(tx)) {
        if (ctx.expiration && ctx.ref_block_num && ctx.ref_block_prefix) {
            tx.expiration = *ctx.expiration;
            tx.ref_block_num = *ctx.ref_block_num;
            tx.ref_block_prefix = *ctx.ref_block_prefix;
        } else if (ctx.block_num && ctx.ref_block_prefix && ctx.timestamp) {
            tx.expiration = expirationTime(ctx.timestamp, ctx.expire_seconds);
            tx.ref_block_num = static_cast<uint16_t>(*ctx.block_num & 0xffff);
            tx.ref_block_prefix = *ctx.ref_block_prefix;
        } else {
            return err(ErrorKind::Invalid,
                       "Invalid transaction context, need either a reference block or explicit "
                       "TaPoS values");
        }
    } else if (isIdentity() && version > 2) {
        // From ESR version 3 all identity requests have expiration
        tx.expiration = ctx.expiration ? *ctx.expiration
                                       : expirationTime(ctx.timestamp, ctx.expire_seconds);
    }
    DK_TRY(actions, resolveActions(abis, signer));
    ResolvedTransaction rv;
    rv.expiration = tx.expiration;
    rv.ref_block_num = tx.ref_block_num;
    rv.ref_block_prefix = tx.ref_block_prefix;
    rv.max_net_usage_words = tx.max_net_usage_words;
    rv.max_cpu_usage_ms = tx.max_cpu_usage_ms;
    rv.delay_sec = tx.delay_sec;
    rv.actions = std::move(actions);
    // context free actions and extensions carry over raw (the upstream spread
    // {...tx, actions} keeps them as-is; data stays hex)
    for (const auto& cfa : tx.context_free_actions) {
        rv.context_free_actions.push_back(ResolvedAction{.account = cfa.account,
                                                         .name = cfa.name,
                                                         .authorization = cfa.authorization,
                                                         .data = json(cfa.data.hexString())});
    }
    rv.transaction_extensions = tx.transaction_extensions;
    return rv;
}

Result<ResolvedSigningRequest> SigningRequest::resolve(const AbiMap& abis,
                                                       const PermissionLevel& signer,
                                                       const TransactionContext& ctx) const {
    DK_TRY(tx, resolveTransaction(abis, signer, ctx));
    std::vector<Action> actions;
    for (const auto& action : tx.actions) {
        ABI abi;
        if (action.account.value == 0 && action.name == Name::from("identity")) {
            abi = identityAbi(version);
        } else {
            const auto found = abis.find(action.account.toString());
            if (found == abis.end()) {
                return err(ErrorKind::Invalid,
                           "Missing ABI definition for " + action.account.toString());
            }
            abi = found->second;
        }
        const auto type = abi.getActionType(action.name);
        if (!type) {
            return err(ErrorKind::Invalid, "Missing action type");
        }
        DK_TRY(encodedData, Serializer::encode(action.data, *type, abi));
        Action typed;
        typed.account = action.account;
        typed.name = action.name;
        typed.authorization = action.authorization;
        typed.data = encodedData;
        actions.push_back(std::move(typed));
    }
    Transaction transaction;
    transaction.expiration = tx.expiration;
    transaction.ref_block_num = tx.ref_block_num;
    transaction.ref_block_prefix = tx.ref_block_prefix;
    transaction.max_net_usage_words = tx.max_net_usage_words;
    transaction.max_cpu_usage_ms = tx.max_cpu_usage_ms;
    transaction.delay_sec = tx.delay_sec;
    transaction.actions = std::move(actions);
    for (const auto& cfa : tx.context_free_actions) {
        Action typed;
        typed.account = cfa.account;
        typed.name = cfa.name;
        typed.authorization = cfa.authorization;
        if (cfa.data.is_string()) {
            DK_TRY(bytes, Bytes::from(cfa.data.get<std::string>()));
            typed.data = bytes;
        }
        transaction.context_free_actions.push_back(std::move(typed));
    }
    transaction.transaction_extensions = tx.transaction_extensions;

    ChainId chainId;
    if (isMultiChain()) {
        if (!ctx.chainId) {
            return err(ErrorKind::Invalid, "Missing chosen chain ID for multi-chain request");
        }
        chainId = *ctx.chainId;
        DK_TRY(ids, getChainIds());
        if (ids && !std::any_of(ids->begin(), ids->end(),
                                [&](const ChainId& id) { return id == chainId; })) {
            return err(ErrorKind::Invalid, "Trying to resolve for chain ID not defined in request");
        }
    } else {
        DK_TRY(id, getChainId());
        chainId = id;
    }
    return ResolvedSigningRequest(*this, signer, std::move(transaction), std::move(tx), chainId);
}

Result<ChainId> SigningRequest::getChainId() const {
    return variantChainId(visitData(data, [](const auto& d) { return d.chain_id; }));
}

Result<std::optional<std::vector<ChainId>>> SigningRequest::getChainIds() const {
    if (!isMultiChain()) {
        return std::optional<std::vector<ChainId>>{};
    }
    const auto raw = getRawInfoKey("chain_ids");
    if (!raw) {
        return std::optional<std::vector<ChainId>>{};
    }
    DK_TRY(variants, Serializer::decode<std::vector<ChainIdVariant>>(*raw));
    std::vector<ChainId> rv;
    for (const auto& variant : variants) {
        DK_TRY(id, variantChainId(variant));
        rv.push_back(id);
    }
    return std::optional(std::move(rv));
}

Result<void> SigningRequest::setChainIds(const std::vector<ChainId>& ids) {
    std::vector<ChainIdVariant> variants;
    for (const auto& id : ids) {
        variants.push_back(id.chainVariant());
    }
    DK_TRY(encoded, Serializer::encode(variants));
    setRawInfoKey("chain_ids", encoded);
    return {};
}

bool SigningRequest::isMultiChain() const {
    const ChainIdVariant variant = visitData(data, [](const auto& d) { return d.chain_id; });
    const ChainAlias* alias = variant.get_if<ChainAlias>();
    return alias != nullptr && alias->value == 0;
}

Result<std::vector<Action>> SigningRequest::getRawActions() const {
    return visitData(data, [&](const auto& d) -> Result<std::vector<Action>> {
        const auto& req = d.req;
        if (const Action* action = req.template get_if<Action>()) {
            return std::vector<Action>{*action};
        }
        if (const auto* actions = req.template get_if<std::vector<Action>>()) {
            return *actions;
        }
        if (const Transaction* tx = req.template get_if<Transaction>()) {
            return tx->actions;
        }
        // identity
        if constexpr (std::is_same_v<std::decay_t<decltype(d)>, RequestDataV2>) {
            const IdentityV2& id = *req.template get_if<IdentityV2>();
            Bytes actionData;
            std::vector<PermissionLevel> authorization = {PlaceholderAuth};
            if (id.permission) {
                DK_TRY(encoded, Serializer::encode(id));
                actionData = encoded;
                authorization = {*id.permission};
            } else {
                DK_TRY(placeholder, Bytes::from("0101000000000000000200000000000000"));
                actionData = placeholder;
            }
            Action action;
            action.account = Name::from("");
            action.name = Name::from("identity");
            action.authorization = std::move(authorization);
            action.data = std::move(actionData);
            return std::vector<Action>{std::move(action)};
        } else {
            const IdentityV3& id = *req.template get_if<IdentityV3>();
            IdentityV3 resolved = id;
            if (!resolved.permission) {
                resolved.permission = PlaceholderAuth;
            }
            DK_TRY(encoded, Serializer::encode(resolved));
            Action action;
            action.account = Name::from("");
            action.name = Name::from("identity");
            action.authorization = {*resolved.permission};
            action.data = encoded;
            return std::vector<Action>{std::move(action)};
        }
    });
}

Result<Transaction> SigningRequest::getRawTransaction() const {
    const Transaction* tx = visitData(
        data, [](const auto& d) { return d.req.template get_if<Transaction>(); });
    if (tx) {
        return *tx;
    }
    DK_TRY(actions, getRawActions());
    Transaction rv;
    rv.actions = std::move(actions);
    return rv;
}

bool SigningRequest::isIdentity() const {
    return visitData(data, [](const auto& d) { return d.req.variantName() == "identity"; });
}

bool SigningRequest::shouldBroadcast() const {
    if (isIdentity()) {
        return false;
    }
    return visitData(data, [](const auto& d) { return d.flags.broadcast(); });
}

std::optional<Name> SigningRequest::getIdentity() const {
    if (!isIdentity()) {
        return std::nullopt;
    }
    const std::optional<PermissionLevel> permission = visitData(data, [](const auto& d) {
        return std::visit(
            [](const auto& value) -> std::optional<PermissionLevel> {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, IdentityV2> || std::is_same_v<T, IdentityV3>) {
                    return value.permission;
                } else {
                    return std::nullopt;
                }
            },
            d.req.value);
    });
    if (permission && !(permission->actor == PlaceholderName)) {
        return permission->actor;
    }
    return std::nullopt;
}

std::optional<Name> SigningRequest::getIdentityPermission() const {
    if (!isIdentity()) {
        return std::nullopt;
    }
    const std::optional<PermissionLevel> permission = visitData(data, [](const auto& d) {
        return std::visit(
            [](const auto& value) -> std::optional<PermissionLevel> {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, IdentityV2> || std::is_same_v<T, IdentityV3>) {
                    return value.permission;
                } else {
                    return std::nullopt;
                }
            },
            d.req.value);
    });
    if (permission && !(permission->permission == PlaceholderPermission)) {
        return permission->permission;
    }
    return std::nullopt;
}

std::optional<Name> SigningRequest::getIdentityScope() const {
    if (!isIdentity() || version <= 2) {
        return std::nullopt;
    }
    const RequestDataV3* v3 = std::get_if<RequestDataV3>(&data);
    if (!v3) {
        return std::nullopt;
    }
    const IdentityV3* id = v3->req.get_if<IdentityV3>();
    return id ? std::optional(id->scope) : std::nullopt;
}

std::map<std::string, Bytes> SigningRequest::getInfo() const {
    std::map<std::string, Bytes> rv;
    visitData(data, [&](const auto& d) {
        for (const auto& pair : d.info) {
            rv[pair.key] = pair.value;
        }
    });
    return rv;
}

std::optional<Bytes> SigningRequest::getRawInfoKey(const std::string& key) const {
    return visitData(data, [&](const auto& d) -> std::optional<Bytes> {
        for (const auto& pair : d.info) {
            if (pair.key == key) {
                return pair.value;
            }
        }
        return std::nullopt;
    });
}

void SigningRequest::setRawInfoKey(const std::string& key, const Bytes& value) {
    std::visit(
        [&](auto& d) {
            for (auto& pair : d.info) {
                if (pair.key == key) {
                    pair.value = value;
                    return;
                }
            }
            d.info.push_back({key, value});
        },
        data);
}

void SigningRequest::setInfoKey(const std::string& key, std::string_view value) {
    // match old behavior where strings encode to raw utf8 as opposed to
    // eosio-abi encoded strings (varuint32 length prefix + utf8 bytes)
    setRawInfoKey(key, Bytes(std::vector<uint8_t>(value.begin(), value.end())));
}

void SigningRequest::setInfoKey(const std::string& key, bool value) {
    setRawInfoKey(key, Bytes(std::vector<uint8_t>{value ? uint8_t(1) : uint8_t(0)}));
}

std::optional<std::string> SigningRequest::getInfoKey(const std::string& key) const {
    const auto raw = getRawInfoKey(key);
    if (!raw) {
        return std::nullopt;
    }
    return raw->utf8String();
}

Result<json> SigningRequest::getInfoKey(const std::string& key, std::string_view type) const {
    const auto raw = getRawInfoKey(key);
    if (!raw) {
        return err(ErrorKind::NotFound, "No info value for key " + key);
    }
    return Serializer::decode(*raw, type, ABI{});
}

bool SigningRequest::dataEquals(const SigningRequest& other) const {
    return getData() == other.getData();
}

// ---- ResolvedSigningRequest ------------------------------------------------

ResolvedSigningRequest::ResolvedSigningRequest(SigningRequest request, PermissionLevel signer,
                                               Transaction transaction,
                                               ResolvedTransaction resolvedTransaction,
                                               ChainId chainId)
    : request(std::move(request)),
      signer(signer),
      transaction(std::move(transaction)),
      resolvedTransaction(std::move(resolvedTransaction)),
      chainId(chainId) {}

Result<ResolvedSigningRequest> ResolvedSigningRequest::fromPayload(
    const json& payload, const SigningRequestEncodingOptions& options) {
    DK_TRY(request, SigningRequest::from(payload.value("req", ""), options));
    DK_TRY(abis, request.fetchAbis());
    TransactionContext ctx;
    if (payload.contains("rbn")) {
        ctx.ref_block_num = static_cast<uint16_t>(std::stoul(payload.value("rbn", "0")));
    }
    if (payload.contains("rid")) {
        ctx.ref_block_prefix = static_cast<uint32_t>(std::stoul(payload.value("rid", "0")));
    }
    if (payload.contains("ex")) {
        DK_TRY(expiration, TimePointSec::from(std::string_view(payload.value("ex", ""))));
        ctx.expiration = expiration;
    }
    if (payload.contains("cid")) {
        DK_TRY(cid, ChainId::from(std::string_view(payload.value("cid", ""))));
        ctx.chainId = cid;
    } else {
        DK_TRY(cid, request.getChainId());
        ctx.chainId = cid;
    }
    const PermissionLevel signer{Name::from(payload.value("sa", "")),
                                 Name::from(payload.value("sp", ""))};
    return request.resolve(abis, signer, ctx);
}

Bytes ResolvedSigningRequest::serializedTransaction() const {
    return Serializer::encode(transaction).value_or(Bytes());
}

Result<std::optional<ResolvedCallback>> ResolvedSigningRequest::getCallback(
    const std::vector<Signature>& signatures, std::optional<uint32_t> blockNum) const {
    const std::string callback =
        std::visit([](const auto& d) { return d.callback; }, request.data);
    const bool background =
        std::visit([](const auto& d) { return d.flags.background(); }, request.data);
    if (callback.empty()) {
        return std::optional<ResolvedCallback>{};
    }
    if (signatures.empty()) {
        return err(ErrorKind::Invalid, "Must have at least one signature to resolve callback");
    }
    json payload = {{"sig", signatures[0].toString()},
                    {"tx", transaction.id().hexString()},
                    {"rbn", std::to_string(transaction.ref_block_num)},
                    {"rid", std::to_string(transaction.ref_block_prefix)},
                    {"ex", transaction.expiration.toString()},
                    {"req", request.encode()},
                    {"sa", signer.actor.toString()},
                    {"sp", signer.permission.toString()},
                    {"cid", chainId.hexString()}};
    for (size_t i = 1; i < signatures.size(); i++) {
        payload["sig" + std::to_string(i - 1)] = signatures[i].toString();
    }
    if (blockNum) {
        payload["bn"] = std::to_string(*blockNum);
    }
    // template {{variable}} placeholders in the url
    std::string url;
    const std::string& source = callback;
    size_t pos = 0;
    while (pos < source.size()) {
        const size_t open = source.find("{{", pos);
        if (open == std::string::npos) {
            url += source.substr(pos);
            break;
        }
        const size_t close = source.find("}}", open);
        if (close == std::string::npos) {
            url += source.substr(pos);
            break;
        }
        url += source.substr(pos, open - pos);
        const std::string key = source.substr(open + 2, close - open - 2);
        if (payload.contains(key) && payload[key].is_string()) {
            url += payload[key].get<std::string>();
        }
        pos = close + 2;
    }
    return std::optional(ResolvedCallback{url, background, std::move(payload)});
}

Result<IdentityProof> ResolvedSigningRequest::getIdentityProof(const Signature& sig) const {
    if (!request.isIdentity()) {
        return err(ErrorKind::Invalid, "Not a identity request");
    }
    // v2 identity requests have no scope; upstream's `getIdentityScope()!`
    // flows null into Name.from, which coerces to an empty name
    const auto scope = request.getIdentityScope();
    IdentityProof proof;
    proof.chainId = chainId;
    proof.scope = scope.value_or(Name());
    proof.expiration = transaction.expiration;
    proof.signer = signer;
    proof.signature = sig;
    return proof;
}

}  // namespace dwarfkit
