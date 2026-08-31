#include "mock_session.hpp"

namespace dwarfkit::test {

namespace {

struct MockTransfer {
    DK_STRUCT("transfer")
    Name from;
    Name to;
    Asset quantity;
    std::string memo;
    DK_FIELDS(from, to, quantity, memo)
};

}  // namespace

Action makeMockAction(const std::string& memo) {
    const MockTransfer transfer{.from = Name::from(mockAccountName),
                                .to = Name::from("teamgreymass"),
                                .quantity = Asset::from("0.1337 EOS").value(),
                                .memo = memo};
    Action action;
    action.account = Name::from("eosio.token");
    action.name = Name::from("transfer");
    action.authorization = {
        PermissionLevel{Name::from(mockAccountName), Name::from(mockPermissionName)}};
    action.data = Serializer::encode(transfer).value();
    return action;
}

Transaction makeMockTransaction(const api::v1::GetInfoResponse& info, const std::string& memo) {
    const TransactionHeader header = info.getTransactionHeader(90);
    Transaction transaction;
    transaction.expiration = header.expiration;
    transaction.ref_block_num = header.ref_block_num;
    transaction.ref_block_prefix = header.ref_block_prefix;
    transaction.actions = makeMockActions(memo);
    return transaction;
}

Result<TransactHookResponseType> mockTransactHook(SigningRequest request, TransactContext&) {
    return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
}

void MockTransactPlugin::register_(TransactContext& context) {
    context.addHook(TransactHookTypes::beforeSign, TransactHookMutable(mockTransactHook));
    const TransactHookImmutable immutable = [](TransactResult& result,
                                               TransactContext&) -> Result<TransactHookResponseType> {
        return TransactHookResponseType{TransactHookResponse{result.request, {}}};
    };
    context.addHook(TransactHookTypes::afterSign, immutable);
    context.addHook(TransactHookTypes::afterBroadcast, immutable);
}

Result<TransactHookResponseType> mockTransactResourceProviderPresignHook(SigningRequest request,
                                                                         TransactContext& context) {
    // If any options this plugin is interested in are set, react; here we
    // just bypass the plugin with a flag.
    if (context.transactPluginsOptions.is_object() &&
        context.transactPluginsOptions.value("disableExamplePlugin", false)) {
        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
    }
    Action newAction;
    newAction.account = Name::from("greymassnoop");
    newAction.name = Name::from("noop");
    newAction.authorization = {PermissionLevel{Name::from("greymassfuel"), Name::from("cosign")}};
    newAction.data = Bytes();
    DK_TRY(modified, prependAction(request, newAction));
    return TransactHookResponseType{TransactHookResponse{std::move(modified), {}}};
}

void MockTransactResourceProviderPlugin::register_(TransactContext& context) {
    context.addHook(TransactHookTypes::beforeSign,
                    TransactHookMutable(mockTransactResourceProviderPresignHook));
}

void MockTransactActionPrependerPlugin::register_(TransactContext& context) {
    context.addHook(
        TransactHookTypes::beforeSign,
        [](SigningRequest request, TransactContext& context) -> Result<TransactHookResponseType> {
            // a random 12-char base36 actor, like the upstream mock
            static const char* alphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
            const auto random = secureRandom(12).value();
            std::string actor;
            for (int i = 0; i < 12; i++) {
                actor += alphabet[random[static_cast<size_t>(i)] % 36];
            }
            DK_TRY(rawActions, request.getRawActions());
            json actions = json::array();
            actions.push_back(json{{"account", "greymassnoop"},
                                   {"name", "noop"},
                                   {"authorization", json::array({{{"actor", actor},
                                                                   {"permission", "test"}}})},
                                   {"data", ""}});
            for (const auto& action : rawActions) {
                actions.push_back(Serializer::objectify(action));
            }
            SigningRequestCreateArguments args;
            args.actions = std::move(actions);
            DK_TRY(created, SigningRequest::create(args, context.esrOptions()));
            return TransactHookResponseType{TransactHookResponse{std::move(created), {}}};
        });
}

void MockMetadataFooWriterPlugin::register_(TransactContext& context) {
    context.addHook(TransactHookTypes::beforeSign,
                    [](SigningRequest request, TransactContext&) -> Result<TransactHookResponseType> {
                        request.setInfoKey("foo", std::string_view("baz"));
                        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
                    });
}

MockWalletPluginConfigs::MockWalletPluginConfigs(std::optional<WalletPluginConfig> config,
                                                 const WalletPluginData& initialData) {
    metadata_ = WalletPluginMetadata::from(
        json{{"name", "Mock Wallet Plugin"},
             {"description", "A mock wallet plugin for testing chain selection"},
             {"logo", "https://assets.wharfkit.com/chain/jungle.png"}});
    if (config) {
        config_ = *config;
    } else {
        config_ = {.requiresChainSelect = true, .requiresPermissionSelect = false};
    }
    data_ = initialData;
}

Result<WalletPluginLoginResponse> MockWalletPluginConfigs::login(LoginContext& context) {
    WalletPluginLoginResponse response;
    response.chain = context.chain ? context.chain->id : mockChainDefinition().id;
    response.permissionLevel = context.permissionLevel
                                   ? *context.permissionLevel
                                   : PermissionLevel::from(mockPermissionLevel).value();
    return response;
}

Result<WalletPluginSignResponse> MockWalletPluginConfigs::sign(
    const ResolvedSigningRequest& resolved, TransactContext& context) {
    if (context.storage) {
        DK_CHECK(context.storage->write(
            "testModify", data_.value("testModify", false) ? "true" : "false"));
    }
    DK_TRY(privateKey, PrivateKey::from(data_.value("privateKey", "")));
    // If the testModify flag is enabled, modify the transaction for testing
    if (data_.is_object() && data_.value("testModify", false)) {
        TransactArgs args;
        args.action = makeMockActionJson("modified transaction");
        DK_TRY(request, context.createRequest(args));
        DK_TRY(modified, context.resolve(request));
        const Checksum256 digest = modified.transaction.signingDigest(context.chain.id);
        DK_TRY(signature, privateKey.signDigest(digest));
        WalletPluginSignResponse response;
        response.resolved = std::move(modified);
        response.signatures = {signature};
        return response;
    }
    // Otherwise sign what was returned
    const Checksum256 digest = resolved.transaction.signingDigest(context.chain.id);
    DK_TRY(signature, privateKey.signDigest(digest));
    WalletPluginSignResponse response;
    response.signatures = {signature};
    return response;
}

}  // namespace dwarfkit::test
