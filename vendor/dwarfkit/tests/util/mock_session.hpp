// Port of @wharfkit/mock-data's session, wallet, storage, userinterface,
// transfer and hook mocks for the session kit tests.
#pragma once

#include <dwarfkit/plugins/wallet/privatekey.hpp>
#include <dwarfkit/session.hpp>

#include "mock_fetch_provider.hpp"

namespace dwarfkit::test {

inline const char* mockPermissionLevel = "wharfkit1111@test";

inline ChainDefinition mockChainDefinition() {
    ChainDefinition rv;
    rv.id = Checksum256::from(std::string_view(mockChainId)).value();
    rv.url = mockUrl;
    return rv;
}

inline std::vector<ChainDefinition> mockChainDefinitions() {
    const auto make = [](const char* id, const char* url) {
        ChainDefinition rv;
        rv.id = Checksum256::from(std::string_view(id)).value();
        rv.url = url;
        return rv;
    };
    return {mockChainDefinition(),
            make("aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906",
                 "https://eos.greymass.com"),
            make("4667b205c6838ef70ff7988f6e8257e8be0e1284a2f59699054a018f743b1d11",
                 "https://telos.greymass.com"),
            make("1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4",
                 "https://wax.greymass.com"),
            make("34593b65376aee3c9b06ea8a8595122b39333aaab4c76ad52587831fcc096590",
                 "https://mockuserinterface.greymass.com")};
}

// mock-data MockStorage
class MockStorage final : public SessionStorage {
public:
    json data = json::object();
    Result<void> write(std::string_view key, std::string_view value) override {
        data[std::string(key)] = std::string(value);
        return {};
    }
    Result<std::optional<std::string>> read(std::string_view key) override {
        const std::string k(key);
        if (!data.contains(k)) {
            return std::optional<std::string>{};
        }
        return std::optional(data[k].get<std::string>());
    }
    Result<void> remove(std::string_view key) override {
        data.erase(std::string(key));
        return {};
    }
};

// mock-data MockUserInterface
class MockUserInterface : public AbstractUserInterface {
public:
    std::vector<std::string> messages;

    void log(const std::string& message) { messages.push_back(message); }

    Result<UserInterfaceLoginResponse> login(LoginContext& context) override {
        UserInterfaceLoginResponse response;
        if (context.chain) {
            response.chainId = context.chain->id;
        } else if (!context.chains.empty()) {
            response.chainId = context.chains[0].id;
        }
        if (context.permissionLevel) {
            response.permissionLevel = context.permissionLevel;
        } else {
            response.permissionLevel = PermissionLevel::from("mock@interface").value();
        }
        response.walletPluginIndex = 0;
        return response;
    }
    Result<void> onError(const Error& error) override {
        log("onError: " + error.message);
        return {};
    }
    Result<UserInterfaceAccountCreationResponse> onAccountCreate(CreateAccountContext&) override {
        log("onAccountCreate");
        return UserInterfaceAccountCreationResponse{};
    }
    Result<void> onAccountCreateComplete() override {
        log("onAccountCreateComplete");
        return {};
    }
    Result<void> onLogin() override {
        log("onLogin");
        return {};
    }
    Result<void> onLoginComplete() override {
        log("onLoginComplete");
        return {};
    }
    Result<void> onTransact() override {
        log("onTransact");
        return {};
    }
    Result<void> onTransactComplete() override {
        log("onTransactComplete");
        return {};
    }
    Result<void> onSign() override {
        log("onSign");
        return {};
    }
    Result<void> onSignComplete() override {
        log("onSignComplete");
        return {};
    }
    Result<void> onBroadcast() override {
        log("onBroadcast");
        return {};
    }
    Result<void> onBroadcastComplete() override {
        log("onBroadcastComplete");
        return {};
    }
    Result<PromptResponse> prompt(const PromptArgs& args, CancelToken) override {
        log("prompt: " + args.title);
        return PromptResponse{};
    }
    void status(const std::string& message) override { log("status:('" + message + "')"); }
    void addTranslations(const LocaleDefinitions& definitions) override {
        log("addTranslations: " + definitions.dump());
    }
};

// mock-data makeWallet()
inline std::shared_ptr<WalletPluginPrivateKey> makeWallet() {
    return WalletPluginPrivateKey::make(mockPrivateKey).value();
}

inline std::shared_ptr<FetchProvider> makeMockFetch() {
    return std::make_shared<MockFetchProvider>(DK_FIXTURE_DIR "/session/data");
}

inline SessionKitArgs mockSessionKitArgs(const std::shared_ptr<UserInterface>& ui = nullptr) {
    SessionKitArgs args;
    args.appName = "unittest";
    args.chains = mockChainDefinitions();
    args.ui = ui ? ui : std::make_shared<MockUserInterface>();
    args.walletPlugins = {makeWallet()};
    return args;
}

inline SessionKitOptions mockSessionKitOptions(
    const std::shared_ptr<SessionStorage>& storage = nullptr) {
    SessionKitOptions options;
    options.fetch = makeMockFetch();
    options.storage = storage ? storage : std::make_shared<MockStorage>();
    return options;
}

inline SessionArgs mockSessionArgs() {
    SessionArgs args;
    args.chain = mockChainDefinition();
    args.permissionLevel = PermissionLevel::from(mockPermissionLevel).value();
    args.walletPlugin = makeWallet();
    return args;
}

inline SessionOptions mockSessionOptions() {
    SessionOptions options;
    // Disable broadcasting by default for tests, enable when required
    options.broadcast = false;
    options.fetch = makeMockFetch();
    return options;
}

// mock-data makeMockAction/makeMockActions/makeMockTransaction
inline json makeMockActionJson(const std::string& memo = "wharfkit is the best <3") {
    return json{{"authorization",
                 json::array({{{"actor", mockAccountName}, {"permission", mockPermissionName}}})},
                {"account", "eosio.token"},
                {"name", "transfer"},
                {"data",
                 {{"from", mockAccountName},
                  {"to", "teamgreymass"},
                  {"quantity", "0.1337 EOS"},
                  {"memo", memo}}}};
}

// the typed Action form (data pre-encoded with the token transfer layout)
Action makeMockAction(const std::string& memo = "wharfkit is the best <3");
inline std::vector<Action> makeMockActions(const std::string& memo = "wharfkit is the best <3") {
    return {makeMockAction(memo)};
}
Transaction makeMockTransaction(const api::v1::GetInfoResponse& info,
                                const std::string& memo = "wharfkit is the best <3");

// mock-data hooks and plugins
Result<TransactHookResponseType> mockTransactHook(SigningRequest request, TransactContext&);
Result<TransactHookResponseType> mockTransactResourceProviderPresignHook(SigningRequest request,
                                                                         TransactContext& context);

class MockTransactPlugin final : public AbstractTransactPlugin {
public:
    std::string id() const override { return "mock-transact-plugin"; }
    void register_(TransactContext& context) override;
};

class MockTransactResourceProviderPlugin final : public AbstractTransactPlugin {
public:
    std::string id() const override { return "mock-transact-resource-provider-plugin"; }
    void register_(TransactContext& context) override;
};

class MockTransactActionPrependerPlugin final : public AbstractTransactPlugin {
public:
    std::string id() const override { return "mock-transact-action-prepender-plugin"; }
    void register_(TransactContext& context) override;
};

class MockMetadataFooWriterPlugin final : public AbstractTransactPlugin {
public:
    std::string id() const override { return "mock-metadata-foo-writer-plugin"; }
    void register_(TransactContext& context) override;
};

// mock-data MockWalletPluginConfigs
class MockWalletPluginConfigs final : public AbstractWalletPlugin {
public:
    explicit MockWalletPluginConfigs(std::optional<WalletPluginConfig> config = std::nullopt,
                                     const WalletPluginData& initialData = json::object());
    std::string id() const override { return "MockWalletPluginConfigs"; }
    Result<WalletPluginLoginResponse> login(LoginContext& context) override;
    Result<WalletPluginSignResponse> sign(const ResolvedSigningRequest& resolved,
                                          TransactContext& context) override;
};

}  // namespace dwarfkit::test
