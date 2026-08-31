// Port of session src/session.ts. TS constructor throws (missing permission,
// missing fetch) become type-level requirements: SessionArgs carries a typed
// PermissionLevel and the fetch provider is used lazily (see DIVERGENCES.md).
#pragma once

#include <dwarfkit/session/transact.hpp>
#include <dwarfkit/session/wallet.hpp>

namespace dwarfkit {

// Arguments required to create a new Session.
struct SessionArgs {
    ChainDefinition chain;
    PermissionLevel permissionLevel;
    std::shared_ptr<WalletPlugin> walletPlugin;
    std::optional<std::string> appName;
};

// Options for creating a new Session.
struct SessionOptions {
    std::vector<TransactABIDef> abis;
    std::shared_ptr<ABICache> abiCache;
    std::optional<bool> allowModify;
    std::optional<bool> broadcast;
    std::optional<uint32_t> expireSeconds;
    std::shared_ptr<FetchProvider> fetch;
    std::shared_ptr<SessionStorage> storage;
    std::optional<std::vector<std::shared_ptr<AbstractTransactPlugin>>> transactPlugins;
    std::optional<TransactPluginsOptions> transactPluginsOptions;
    std::shared_ptr<UserInterface> ui;
};

// The serialized, storable form of a Session.
struct SerializedSession {
    std::string actor;
    std::string chain;  // chain id hex
    std::optional<bool> isDefault;  // json key "default"
    std::string permission;
    SerializedWalletPlugin walletPlugin;
    json data;  // null when absent

    json toJSON() const;
    static Result<SerializedSession> fromJSON(const json& value);
};

// A representation of a session to interact with a specific blockchain
// account.
class Session {
public:
    Session(const SessionArgs& args, const SessionOptions& options = {});

    std::optional<std::string> appName;
    std::vector<TransactABIDef> abis;
    std::shared_ptr<ABICache> abiCache;
    bool allowModify = true;
    bool broadcast = true;
    ChainDefinition chain;
    uint32_t expireSeconds = 120;
    std::shared_ptr<FetchProvider> fetch;
    PermissionLevel permissionLevel;
    std::shared_ptr<SessionStorage> storage;
    std::vector<std::shared_ptr<AbstractTransactPlugin>> transactPlugins;
    TransactPluginsOptions transactPluginsOptions = json::object();
    std::shared_ptr<UserInterface> ui;
    std::shared_ptr<WalletPlugin> walletPlugin;

    // Data stored in this session instance.
    const json& data() const { return data_; }
    void setData(const json& data) { data_ = data; }

    // The name of the actor that is being used for this session.
    Name actor() const { return permissionLevel.actor; }
    // The name of the permission that is being used for this session.
    Name permission() const { return permissionLevel.permission; }
    // An APIClient configured for this session (a fresh instance per call,
    // like the upstream getter).
    std::shared_ptr<APIClient> client() const;

    // Alters the session config to change the API endpoint in use.
    void setEndpoint(const std::string& url) { chain.url = url; }

    // Templates in any missing fields from partial transactions.
    TransactArgs upgradeTransaction(const TransactArgs& args) const;

    // Create a clone of the given SigningRequest bound to an abi cache.
    SigningRequest cloneRequest(const SigningRequest& request,
                                const std::shared_ptr<ABICache>& abiCache) const;

    // Convert any provided form of TransactArgs to a SigningRequest.
    Result<SigningRequest> createRequest(const TransactArgs& args,
                                         const std::shared_ptr<ABICache>& abiCache) const;

    // Update a SigningRequest, ensuring its old metadata is retained.
    Result<SigningRequest> updateRequest(const SigningRequest& previous,
                                         const SigningRequest& modified,
                                         const std::shared_ptr<ABICache>& abiCache) const;

    // Perform a transaction using this session.
    Result<TransactResult> transact(const TransactArgs& args, const TransactOptions& options = {});

    // Request a signature for a given transaction. Does not use plugins and
    // does not broadcast.
    Result<std::vector<Signature>> signTransaction(const Transaction& transaction);

    SerializedSession serialize() const;

    // The abi cache for this transact call with all provided ABIs merged in.
    Result<std::shared_ptr<ABICache>> getMergedAbiCache(const TransactArgs& args,
                                                        const TransactOptions& options);

private:
    json data_ = json::object();
};

}  // namespace dwarfkit
