// Port of session src/wallet.ts.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <dwarfkit/antelope.hpp>
#include <dwarfkit/common/locale.hpp>
#include <dwarfkit/common/logo.hpp>
#include <dwarfkit/signing_request.hpp>

namespace dwarfkit {

class LoginContext;
class TransactContext;
struct LogoutContext;

// The static configuration of a WalletPlugin.
struct WalletPluginConfig {
    // Whether the plugin requires the user to manually select the blockchain
    // to authorize against.
    bool requiresChainSelect = true;
    // Whether the plugin requires the user to select a permission to use from
    // a list.
    bool requiresPermissionSelect = false;
    // Whether the plugin requires the user to manually enter a permission.
    std::optional<bool> requiresPermissionEntry;
    // If set, which blockchains are compatible with this plugin (chain IDs as
    // hex strings, like the upstream Checksum256Type[]).
    std::vector<std::string> supportedChains;
};

// The metadata of a WalletPlugin for display purposes.
struct WalletPluginMetadata {
    // A display name for the wallet that is presented to users.
    std::optional<std::string> name;
    // A wallet description to further identify the wallet for users.
    std::optional<std::string> description;
    // Wallet branding.
    std::optional<Logo> logo;
    // Link to the homepage for the wallet.
    std::optional<std::string> homepage;
    // Link to the download page for the wallet.
    std::optional<std::string> download;
    // The public key being used by the wallet plugin.
    std::optional<std::string> publicKey;

    static WalletPluginMetadata from(const json& data);
    json toJSON() const;
};

// The response for a login call of a WalletPlugin.
struct WalletPluginLoginResponse {
    Checksum256 chain;
    PermissionLevel permissionLevel;
    std::optional<IdentityProof> identityProof;
};

// The response for a sign call of a WalletPlugin.
struct WalletPluginSignResponse {
    std::optional<ResolvedSigningRequest> resolved;
    std::vector<Signature> signatures;
};

// Persistent storage format for wallet specified data.
using WalletPluginData = json;

// The serialized form of a WalletPlugin instance.
struct SerializedWalletPlugin {
    std::string id;
    WalletPluginData data;

    json toJSON() const { return json{{"id", id}, {"data", data}}; }
    static SerializedWalletPlugin fromJSON(const json& value) {
        return {value.value("id", ""), value.contains("data") ? value["data"] : json::object()};
    }
};

// Interface which all 3rd party wallet plugins must implement.
struct WalletPlugin {
    // A URL friendly (lower case, no spaces, etc) ID for this plugin, used in
    // serialization.
    virtual std::string id() const = 0;
    // The data that needs to persist for the plugin, used in serialization.
    virtual WalletPluginData data() const = 0;
    virtual void setData(const WalletPluginData& data) = 0;
    // The SessionKit configuration parameters for this plugin.
    virtual const WalletPluginConfig& config() const = 0;
    // The metadata for the WalletPlugin itself.
    virtual const WalletPluginMetadata& metadata() const = 0;
    // Any translations this plugin requires.
    virtual LocaleDefinitions translations() const { return LocaleDefinitions(); }
    // Request the WalletPlugin to log in a user.
    virtual Result<WalletPluginLoginResponse> login(LoginContext& context) = 0;
    // Request the WalletPlugin to sign a transaction.
    virtual Result<WalletPluginSignResponse> sign(const ResolvedSigningRequest& transaction,
                                                  TransactContext& context) = 0;
    // Optional: signal to the wallet plugin to log out the user.
    virtual Result<void> logout(const LogoutContext& context) {
        (void)context;
        return {};
    }
    // Whether this plugin implements logout (mirrors the optional TS method).
    virtual bool hasLogout() const { return false; }
    // Serialize the WalletPlugin ID and data into a plain object.
    virtual SerializedWalletPlugin serialize() const = 0;
    // Optional: request a public key from the wallet plugin.
    virtual Result<PublicKey> retrievePublicKey(const Checksum256& chainId) {
        (void)chainId;
        return err(ErrorKind::Unsupported, "retrievePublicKey is not implemented by this plugin");
    }
    virtual bool hasRetrievePublicKey() const { return false; }
    virtual ~WalletPlugin() = default;
};

// Abstract class which 3rd party WalletPlugin implementations may extend.
class AbstractWalletPlugin : public WalletPlugin {
public:
    WalletPluginData data() const override { return data_; }
    void setData(const WalletPluginData& data) override { data_ = data; }
    const WalletPluginConfig& config() const override { return config_; }
    const WalletPluginMetadata& metadata() const override { return metadata_; }
    SerializedWalletPlugin serialize() const override { return {id(), data()}; }

protected:
    WalletPluginData data_ = json::object();
    WalletPluginConfig config_{.requiresChainSelect = true,
                               .requiresPermissionSelect = false,
                               .requiresPermissionEntry = false};
    WalletPluginMetadata metadata_;
};

}  // namespace dwarfkit
