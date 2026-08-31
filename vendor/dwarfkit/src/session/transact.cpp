#include <dwarfkit/session/transact.hpp>

#include <dwarfkit/session/login.hpp>

namespace dwarfkit {

// ---- LoginContext (login.ts) ----------------------------------------------

LoginContext::LoginContext(const LoginContextOptions& options)
    : appName(options.appName),
      arbitrary(options.arbitrary),
      chain(options.chain),
      chains(options.chains),
      fetch(options.fetch),
      permissionLevel(options.permissionLevel),
      ui(options.ui),
      walletPlugins(options.walletPlugins) {
    for (const auto& plugin : options.loginPlugins) {
        if (plugin) {
            plugin->register_(*this);
        }
    }
}

void LoginContext::addHook(LoginHookTypes type, LoginHook hook) {
    switch (type) {
        case LoginHookTypes::beforeLogin:
            hooks.beforeLogin.push_back(std::move(hook));
            break;
        case LoginHookTypes::afterLogin:
            hooks.afterLogin.push_back(std::move(hook));
            break;
    }
}

std::shared_ptr<APIClient> LoginContext::getClient(const ChainDefinition& forChain) const {
    return std::make_shared<APIClient>(APIClientOptions{.url = forChain.url, .fetch = fetch});
}

// ---- TransactContext (transact.ts) -----------------------------------------

TransactContext::TransactContext(const TransactContextOptions& options)
    : abiCache(options.abiCache),
      appName(options.appName),
      chain(options.chain),
      client(options.client),
      createRequest(options.createRequest),
      fetch(options.fetch),
      permissionLevel(options.permissionLevel),
      storage(options.storage),
      transactPluginsOptions(options.transactPluginsOptions),
      ui(options.ui) {
    for (const auto& plugin : options.transactPlugins) {
        if (plugin) {
            plugin->register_(*this);
        }
    }
}

void TransactContext::addHook(TransactHookTypes type, TransactHookMutable hook) {
    if (type == TransactHookTypes::beforeSign) {
        hooks.beforeSign.push_back(std::move(hook));
    }
}

void TransactContext::addHook(TransactHookTypes type, TransactHookImmutable hook) {
    switch (type) {
        case TransactHookTypes::afterSign:
            hooks.afterSign.push_back(std::move(hook));
            break;
        case TransactHookTypes::afterBroadcast:
            hooks.afterBroadcast.push_back(std::move(hook));
            break;
        case TransactHookTypes::beforeSign:
            break;
    }
}

Result<api::v1::GetInfoResponse> TransactContext::getInfo() {
    if (info) {
        return *info;
    }
    DK_TRY(response, client->v1.chain.get_info());
    info = response;
    return response;
}

Result<ResolvedSigningRequest> TransactContext::resolve(const SigningRequest& request,
                                                        uint32_t expireSeconds) {
    // Build the transaction header
    TransactionContext resolveArgs;
    resolveArgs.chainId = ChainId::from(chain.id);

    // Check if this request requires tapos generation
    if (request.requiresTapos()) {
        DK_TRY(chainInfo, getInfo());
        const TransactionHeader header = chainInfo.getTransactionHeader(expireSeconds);
        resolveArgs.expiration = header.expiration;
        resolveArgs.ref_block_num = header.ref_block_num;
        resolveArgs.ref_block_prefix = header.ref_block_prefix;
    }

    // Load ABIs required to resolve this request
    DK_TRY(abis, request.fetchAbis(abiCache.get()));

    // Resolve the request and return
    return request.resolve(abis, permissionLevel, resolveArgs);
}

// ---- TransactRevisions ------------------------------------------------------

TransactRevisions::TransactRevisions(const SigningRequest& request) {
    addRevision({request, {}}, "original", true);
}

void TransactRevisions::addRevision(const TransactHookResponse& response, const std::string& code,
                                    bool allowModify) {
    // Determine if the new response modifies the request
    bool modified = false;
    const std::string encoded = response.request.encode();
    if (!revisions.empty()) {
        modified = revisions.back().response.request != encoded;
    }
    TransactRevision revision;
    revision.allowModify = allowModify;
    revision.code = code;
    revision.modified = modified;
    revision.response.request = encoded;
    for (const auto& signature : response.signatures) {
        revision.response.signatures.push_back(signature.toString());
    }
    revisions.push_back(std::move(revision));
}

}  // namespace dwarfkit
