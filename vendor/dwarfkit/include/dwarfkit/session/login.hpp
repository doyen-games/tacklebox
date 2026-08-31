// Port of session src/login.ts.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <dwarfkit/common/chains.hpp>
#include <dwarfkit/session/ui.hpp>
#include <dwarfkit/session/wallet.hpp>
#include <dwarfkit/transport/fetch_provider.hpp>

namespace dwarfkit {

class LoginContext;
class AbstractLoginPlugin;

enum class LoginHookTypes {
    beforeLogin,
    afterLogin,
};

using LoginHook = std::function<Result<void>(LoginContext&)>;

struct LoginHooks {
    std::vector<LoginHook> afterLogin;
    std::vector<LoginHook> beforeLogin;
};

struct UserInterfaceRequirements {
    bool requiresChainSelect = true;
    bool requiresPermissionSelect = true;
    bool requiresPermissionEntry = false;
    bool requiresWalletSelect = true;
};

// The wallet plugin surface exposed to the UserInterface during login.
struct UserInterfaceWalletPlugin {
    WalletPluginConfig config;
    WalletPluginMetadata metadata;
    std::function<Result<PublicKey>(const Checksum256&)> retrievePublicKey;
};

// Options for creating a new context for a Kit::login call.
struct LoginContextOptions {
    std::optional<std::string> appName;
    json arbitrary = json::object();
    std::optional<ChainDefinition> chain;
    std::vector<ChainDefinition> chains;
    std::shared_ptr<FetchProvider> fetch;
    std::vector<std::shared_ptr<AbstractLoginPlugin>> loginPlugins;
    std::optional<PermissionLevel> permissionLevel;
    std::vector<UserInterfaceWalletPlugin> walletPlugins;
    std::shared_ptr<UserInterface> ui;
};

// Temporary context created for the duration of a Kit::login call. Stores the
// state of the login request and lets plugins add hooks into the process.
class LoginContext {
public:
    explicit LoginContext(const LoginContextOptions& options);

    std::optional<std::string> appName;
    json arbitrary = json::object();
    std::optional<ChainDefinition> chain;
    std::vector<ChainDefinition> chains;
    std::shared_ptr<FetchProvider> fetch;
    LoginHooks hooks;
    std::optional<PermissionLevel> permissionLevel;
    std::shared_ptr<UserInterface> ui;
    UserInterfaceRequirements uiRequirements;
    std::optional<int> walletPluginIndex;
    std::vector<UserInterfaceWalletPlugin> walletPlugins;

    void addHook(LoginHookTypes type, LoginHook hook);
    std::shared_ptr<APIClient> getClient(const ChainDefinition& chain) const;
    SigningRequestEncodingOptions esrOptions() const { return {.zlib = true}; }
};

// Payload accepted by the Kit::login method.
struct LoginPlugin {
    virtual void register_(LoginContext& context) = 0;
    virtual ~LoginPlugin() = default;
};

// Abstract class for Kit::login plugins to extend.
class AbstractLoginPlugin : public LoginPlugin {};

class BaseLoginPlugin : public AbstractLoginPlugin {
public:
    void register_(LoginContext&) override {}
};

}  // namespace dwarfkit
