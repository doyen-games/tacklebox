// Port of session src/account-creation.ts.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <dwarfkit/common/chains.hpp>
#include <dwarfkit/common/locale.hpp>
#include <dwarfkit/common/logo.hpp>
#include <dwarfkit/session/ui.hpp>
#include <dwarfkit/transport/fetch_provider.hpp>

namespace dwarfkit {

struct AccountCreationPlugin;

// The static configuration of an AccountCreationPlugin.
struct AccountCreationPluginConfig {
    // Whether the plugin requires the user to manually select the blockchain
    // to create an account on.
    bool requiresChainSelect = true;
    // If set, which blockchains are compatible with this plugin.
    std::vector<ChainDefinition> supportedChains;
};

// The metadata of an AccountCreationPlugin.
struct AccountCreationPluginMetadata {
    // A display name for the account creation service presented to users.
    std::string name;
    // A description to further identify the account creation service.
    std::optional<std::string> description;
    // Account creation service branding.
    std::optional<Logo> logo;
    // Link to the homepage for the account creation service.
    std::optional<std::string> homepage;

    static AccountCreationPluginMetadata from(const json& data);
    json toJSON() const;
};

// Options for a createAccount call.
struct CreateAccountOptions {
    std::optional<Name> accountName;
    std::optional<ChainDefinition> chain;
    std::optional<std::string> pluginId;
};

// The response for a createAccount call.
struct CreateAccountResponse {
    ChainDefinition chain;
    Name accountName;
};

struct UserInterfaceAccountCreationRequirements {
    bool requiresChainSelect = true;
    bool requiresPluginSelect = true;
};

struct CreateAccountContextOptions {
    std::vector<std::shared_ptr<AccountCreationPlugin>> accountCreationPlugins;
    std::optional<std::string> appName;
    std::optional<ChainDefinition> chain;
    std::vector<ChainDefinition> chains;
    std::shared_ptr<FetchProvider> fetch;
    std::shared_ptr<UserInterface> ui;
    std::optional<UserInterfaceAccountCreationRequirements> uiRequirements;
};

class CreateAccountContext {
public:
    explicit CreateAccountContext(const CreateAccountContextOptions& options);

    std::vector<std::shared_ptr<AccountCreationPlugin>> accountCreationPlugins;
    std::optional<std::string> appName;
    std::optional<ChainDefinition> chain;
    std::vector<ChainDefinition> chains;
    std::shared_ptr<FetchProvider> fetch;
    std::shared_ptr<UserInterface> ui;
    UserInterfaceAccountCreationRequirements uiRequirements;

    std::shared_ptr<APIClient> getClient(const ChainDefinition& chain) const;
};

// Interface which all 3rd party account creation plugins must implement.
struct AccountCreationPlugin {
    // A URL friendly ID for this plugin, used in serialization.
    virtual std::string id() const = 0;
    // A display name presented to users.
    virtual std::string name() const = 0;
    // The SessionKit configuration parameters for this plugin.
    virtual const AccountCreationPluginConfig& config() const = 0;
    // Any translations this plugin requires.
    virtual LocaleDefinitions translations() const { return LocaleDefinitions(); }
    // Request the plugin to create a new account.
    virtual Result<CreateAccountResponse> create(CreateAccountContext& context) = 0;
    virtual ~AccountCreationPlugin() = default;
};

// Abstract class which 3rd party implementations may extend.
class AbstractAccountCreationPlugin : public AccountCreationPlugin {
public:
    const AccountCreationPluginConfig& config() const override { return config_; }

protected:
    AccountCreationPluginConfig config_{.requiresChainSelect = true};
    AccountCreationPluginMetadata metadata_;
};

}  // namespace dwarfkit
