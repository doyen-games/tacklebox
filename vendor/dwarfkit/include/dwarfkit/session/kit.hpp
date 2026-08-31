// Port of session src/kit.ts. BrowserLocalStorage's default-storage role goes
// to MemorySessionStorage; pass a FileSessionStorage to persist to disk.
#pragma once

#include <dwarfkit/session/account_creation.hpp>
#include <dwarfkit/session/login.hpp>
#include <dwarfkit/session/session.hpp>

namespace dwarfkit {

struct LoginOptions {
    // Arbitrary data passed via context to the wallet plugin.
    json arbitrary = json::object();
    // Chain to log in on; a definition or a chain id present in the kit.
    std::optional<std::variant<ChainDefinition, Checksum256>> chain;
    // Subset of kit chain ids to offer.
    std::optional<std::vector<Checksum256>> chains;
    std::vector<std::shared_ptr<AbstractLoginPlugin>> loginPlugins;
    std::optional<bool> setAsDefault;
    std::optional<std::vector<std::shared_ptr<AbstractTransactPlugin>>> transactPlugins;
    std::optional<TransactPluginsOptions> transactPluginsOptions;
    std::optional<PermissionLevel> permissionLevel;
    // Id of the wallet plugin to use, skipping wallet selection.
    std::optional<std::string> walletPlugin;
};

struct LoginResult {
    LoginContext context;
    WalletPluginLoginResponse response;
    std::shared_ptr<Session> session;
};

struct LogoutContext {
    std::shared_ptr<Session> session;
    std::string appName;
};

struct RestoreArgs {
    Checksum256 chain;
    std::optional<std::string> actor;
    std::optional<std::string> permission;
    std::optional<SerializedWalletPlugin> walletPlugin;
    json data;
};

struct SessionKitArgs {
    std::string appName;
    std::vector<ChainDefinition> chains;
    std::shared_ptr<UserInterface> ui;
    std::vector<std::shared_ptr<WalletPlugin>> walletPlugins;
};

struct SessionKitOptions {
    std::vector<TransactABIDef> abis;
    std::optional<bool> allowModify;
    std::optional<uint32_t> expireSeconds;
    std::shared_ptr<FetchProvider> fetch;
    std::optional<std::vector<std::shared_ptr<AbstractLoginPlugin>>> loginPlugins;
    std::shared_ptr<SessionStorage> storage;
    std::optional<std::vector<std::shared_ptr<AbstractTransactPlugin>>> transactPlugins;
    std::optional<TransactPluginsOptions> transactPluginsOptions;
    std::vector<std::shared_ptr<AccountCreationPlugin>> accountCreationPlugins;
};

// Request a session from an account.
class SessionKit {
public:
    SessionKit(const SessionKitArgs& args, const SessionKitOptions& options = {});

    std::vector<TransactABIDef> abis;
    bool allowModify = true;
    std::string appName;
    uint32_t expireSeconds = 120;
    std::shared_ptr<FetchProvider> fetch;
    std::vector<std::shared_ptr<AbstractLoginPlugin>> loginPlugins;
    std::shared_ptr<SessionStorage> storage;
    std::vector<std::shared_ptr<AbstractTransactPlugin>> transactPlugins;
    TransactPluginsOptions transactPluginsOptions = json::object();
    std::shared_ptr<UserInterface> ui;
    std::vector<std::shared_ptr<WalletPlugin>> walletPlugins;
    std::vector<std::shared_ptr<AccountCreationPlugin>> accountCreationPlugins;
    std::vector<ChainDefinition> chains;

    // Alters the kit config for a specific chain to change the API endpoint.
    Result<void> setEndpoint(const Checksum256& id, const std::string& url);

    Result<ChainDefinition> getChainDefinition(
        const Checksum256& id,
        const std::optional<std::vector<ChainDefinition>>& override = std::nullopt) const;

    // Request account creation.
    Result<CreateAccountResponse> createAccount(const CreateAccountOptions& options = {});

    // Request a session from an account.
    Result<LoginResult> login(const LoginOptions& options = {});

    Result<void> logout();
    Result<void> logout(const Session& session);
    Result<void> logout(const SerializedSession& session);

    // Restore the default session from storage; nullptr when none exists.
    Result<std::shared_ptr<Session>> restore();
    Result<std::shared_ptr<Session>> restore(const RestoreArgs& args,
                                             const LoginOptions& options = {});
    Result<std::shared_ptr<Session>> restore(const SerializedSession& serialized,
                                             const LoginOptions& options = {});

    Result<std::vector<std::shared_ptr<Session>>> restoreAll();

    Result<void> persistSession(const Session& session, bool setAsDefault = true);

    Result<std::vector<SerializedSession>> getSessions() const;

    SessionOptions getSessionOptions(const LoginOptions& options = {}) const;

private:
    LogoutContext logoutParams(const SerializedSession& serialized,
                               const std::shared_ptr<WalletPlugin>& walletPlugin) const;
    Result<void> logoutSerialized(const SerializedSession& serialized);
};

}  // namespace dwarfkit
