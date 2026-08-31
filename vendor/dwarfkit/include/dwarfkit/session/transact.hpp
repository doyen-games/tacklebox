// Port of session src/transact.ts. The TS `register` method is register_
// (reserved word in C++); Fetch is the FetchProvider interface.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <dwarfkit/abicache.hpp>
#include <dwarfkit/common/chains.hpp>
#include <dwarfkit/common/locale.hpp>
#include <dwarfkit/session/storage.hpp>
#include <dwarfkit/session/ui.hpp>
#include <dwarfkit/signing_request.hpp>
#include <dwarfkit/transport/fetch_provider.hpp>

namespace dwarfkit {

class TransactContext;
class AbstractTransactPlugin;
struct TransactResult;

using TransactPluginsOptions = json;

enum class TransactHookTypes {
    beforeSign,
    afterSign,
    afterBroadcast,
};

struct TransactHookResponse {
    SigningRequest request;
    std::vector<Signature> signatures;
};

using TransactHookResponseType = std::optional<TransactHookResponse>;

using TransactHookMutable =
    std::function<Result<TransactHookResponseType>(SigningRequest, TransactContext&)>;
using TransactHookImmutable =
    std::function<Result<TransactHookResponseType>(TransactResult&, TransactContext&)>;

struct TransactHooks {
    std::vector<TransactHookImmutable> afterSign;
    std::vector<TransactHookMutable> beforeSign;
    std::vector<TransactHookImmutable> afterBroadcast;
};

// Payload accepted by the Session::transact method. One of action, actions,
// transaction or request must be set; actions/transaction are json (AnyAction
// forms), request is an esr uri or SigningRequest.
struct TransactArgs {
    std::optional<json> transaction;
    std::optional<json> action;
    std::optional<json> actions;
    std::optional<std::variant<SigningRequest, std::string>> request;
    std::optional<json> context_free_actions;
    std::optional<std::vector<std::string>> context_free_data;
};

struct TransactABIDef {
    Name account;
    ABI abi;
};

// Options for creating a new context for a Session::transact call.
struct TransactContextOptions {
    std::shared_ptr<ABICache> abiCache;
    std::optional<std::string> appName;
    ChainDefinition chain;
    std::shared_ptr<APIClient> client;
    std::function<Result<SigningRequest>(const TransactArgs&)> createRequest;
    std::shared_ptr<FetchProvider> fetch;
    PermissionLevel permissionLevel;
    std::shared_ptr<SessionStorage> storage;
    std::vector<std::shared_ptr<AbstractTransactPlugin>> transactPlugins;
    TransactPluginsOptions transactPluginsOptions = json::object();
    std::shared_ptr<UserInterface> ui;
};

// Temporary context created for the duration of a Session::transact call.
// Stores the state of the transact request and lets plugins add hooks into
// the process.
class TransactContext {
public:
    explicit TransactContext(const TransactContextOptions& options);

    std::shared_ptr<ABICache> abiCache;
    std::optional<std::string> appName;
    ChainDefinition chain;
    std::shared_ptr<APIClient> client;
    std::function<Result<SigningRequest>(const TransactArgs&)> createRequest;
    std::shared_ptr<FetchProvider> fetch;
    TransactHooks hooks;
    std::optional<api::v1::GetInfoResponse> info;
    PermissionLevel permissionLevel;
    std::shared_ptr<SessionStorage> storage;
    TransactPluginsOptions transactPluginsOptions = json::object();
    std::shared_ptr<UserInterface> ui;

    Name accountName() const { return permissionLevel.actor; }
    Name permissionName() const { return permissionLevel.permission; }
    SigningRequestEncodingOptions esrOptions() const {
        return {.zlib = true, .abiProvider = abiCache.get()};
    }

    void addHook(TransactHookTypes type, TransactHookMutable hook);
    void addHook(TransactHookTypes type, TransactHookImmutable hook);

    Result<api::v1::GetInfoResponse> getInfo();
    Result<ResolvedSigningRequest> resolve(const SigningRequest& request,
                                           uint32_t expireSeconds = 120);
};

struct TransactRevision {
    // Whether or not the context allowed any modification to take effect.
    bool allowModify = true;
    // The string representation of the code executed.
    std::string code;
    // If the request was modified by this code.
    bool modified = false;
    // The response from the code that was executed.
    struct {
        std::string request;
        std::vector<std::string> signatures;
    } response;
};

class TransactRevisions {
public:
    explicit TransactRevisions(const SigningRequest& request);

    std::vector<TransactRevision> revisions;

    void addRevision(const TransactHookResponse& response, const std::string& code,
                     bool allowModify);
};

// An interface to define a return type.
struct TransactResultReturnType {
    Name name;
    std::string result_type;
};

// The return values from a Session::transact call, processed and decoded.
struct TransactResultReturnValue {
    Name contract;
    Name action;
    std::string hex;
    json data;
    TransactResultReturnType returnType;
};

// The response from a Session::transact call.
struct TransactResult {
    // The chain that was used.
    ChainDefinition chain;
    // The SigningRequest representation of the transaction.
    SigningRequest request;
    // The ResolvedSigningRequest of the transaction.
    std::optional<ResolvedSigningRequest> resolved;
    // The response from the API after sending, only present when broadcast.
    std::optional<json> response;
    // The return values provided by the transaction.
    std::vector<TransactResultReturnValue> returns;
    // Revisions of the transaction as modified by plugins.
    TransactRevisions revisions;
    // The transaction signatures.
    std::vector<Signature> signatures;
    // The signer authority.
    PermissionLevel signer;
    // The resulting transaction.
    std::optional<ResolvedTransaction> transaction;
};

// Options for the Session::transact method.
struct TransactOptions {
    // ABIs to use when resolving the transaction.
    std::vector<TransactABIDef> abis;
    // An optional ABICache to control how ABIs are loaded.
    std::shared_ptr<ABICache> abiCache;
    // Whether to allow the signer to make modifications to the request (e.g.
    // applying a cosigner action to pay for resources).
    std::optional<bool> allowModify;
    // Whether to broadcast the transaction or just return the signature.
    std::optional<bool> broadcast;
    // Chain to use when configured with multiple chains.
    std::optional<Checksum256> chain;
    // The number of seconds in the future this transaction will expire.
    std::optional<uint32_t> expireSeconds;
    // Specific transact plugins to use for this transaction (nullopt falls
    // back to the session's plugins; an empty list disables them).
    std::optional<std::vector<std::shared_ptr<AbstractTransactPlugin>>> transactPlugins;
    // Optional parameters passed in to the various transact plugins.
    std::optional<TransactPluginsOptions> transactPluginsOptions;
};

// Interface which a Session::transact plugin must implement.
struct TransactPlugin {
    // A URL friendly ID for this plugin, used in serialization.
    virtual std::string id() const = 0;
    // Any translations this plugin requires.
    virtual LocaleDefinitions translations() const { return LocaleDefinitions(); }
    // Register hooks into the transaction flow.
    virtual void register_(TransactContext& context) = 0;
    virtual ~TransactPlugin() = default;
};

// Abstract class for Session::transact plugins to extend.
class AbstractTransactPlugin : public TransactPlugin {};

class BaseTransactPlugin : public AbstractTransactPlugin {
public:
    std::string id() const override { return "base-transact-plugin"; }
    void register_(TransactContext&) override {}
};

}  // namespace dwarfkit
