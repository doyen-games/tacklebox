#include <dwarfkit/session/session.hpp>

#include <cstdio>

#include <dwarfkit/session/utils.hpp>

namespace dwarfkit {

namespace {

void warn(const std::string& message) { std::fprintf(stderr, "[dwarfkit] %s\n", message.c_str()); }

Result<std::vector<TransactResultReturnValue>> processReturnValues(
    const json& response, const std::shared_ptr<ABICache>& abiCache) {
    std::vector<TransactResultReturnValue> decoded;
    const json& traces = response["processed"]["action_traces"];
    for (const auto& actionTrace : traces) {
        // upstream tests truthiness: absent or empty hex both skip
        if (!actionTrace.contains("return_value_hex_data") ||
            !actionTrace["return_value_hex_data"].is_string() ||
            actionTrace["return_value_hex_data"].get_ref<const std::string&>().empty()) {
            continue;
        }
        const Name contract = Name::from(actionTrace["act"].value("account", ""));
        const Name action = Name::from(actionTrace["act"].value("name", ""));
        const std::string hex = actionTrace["return_value_hex_data"].get<std::string>();
        DK_TRY(abi, abiCache->getAbi(contract));
        const auto returnType =
            std::find_if(abi.action_results.begin(), abi.action_results.end(),
                         [&](const auto& result) { return result.name == action; });
        if (returnType != abi.action_results.end()) {
            DK_TRY(bytes, Bytes::from(hex));
            auto data = Serializer::decode(bytes.array, returnType->result_type, abi);
            if (data) {
                decoded.push_back({contract, action, hex, *data,
                                   {returnType->name, returnType->result_type}});
            } else {
                warn("Error decoding return value for " + contract.toString() +
                     "::" + action.toString() + ": " + data.error().message);
                decoded.push_back({contract, action, hex, json(""),
                                   {returnType->name, returnType->result_type}});
            }
        } else {
            warn("No return type found for " + contract.toString() + "::" + action.toString());
            decoded.push_back({contract, action, hex, json(""), {action, ""}});
        }
    }
    return decoded;
}

}  // namespace

json SerializedSession::toJSON() const {
    json rv = json::object();
    rv["chain"] = chain;
    rv["actor"] = actor;
    rv["permission"] = permission;
    rv["walletPlugin"] = walletPlugin.toJSON();
    if (isDefault) {
        rv["default"] = *isDefault;
    }
    if (!data.is_null() && !(data.is_object() && data.empty())) {
        rv["data"] = data;
    }
    return rv;
}

Result<SerializedSession> SerializedSession::fromJSON(const json& value) {
    if (!value.is_object()) {
        return err(ErrorKind::Invalid, "Serialized session must be an object");
    }
    SerializedSession rv;
    rv.chain = value.value("chain", "");
    rv.actor = value.value("actor", "");
    rv.permission = value.value("permission", "");
    if (value.contains("default")) {
        rv.isDefault = value["default"].get<bool>();
    }
    if (value.contains("walletPlugin")) {
        rv.walletPlugin = SerializedWalletPlugin::fromJSON(value["walletPlugin"]);
    }
    if (value.contains("data")) {
        rv.data = value["data"];
    }
    return rv;
}

Session::Session(const SessionArgs& args, const SessionOptions& options)
    : appName(args.appName),
      abis(options.abis),
      chain(args.chain),
      fetch(options.fetch),
      permissionLevel(args.permissionLevel),
      storage(options.storage),
      ui(options.ui),
      walletPlugin(args.walletPlugin) {
    if (options.allowModify) {
        allowModify = *options.allowModify;
    }
    if (options.broadcast) {
        broadcast = *options.broadcast;
    }
    if (options.expireSeconds) {
        expireSeconds = *options.expireSeconds;
    }
    if (options.transactPlugins) {
        transactPlugins = *options.transactPlugins;
    } else {
        transactPlugins = {std::make_shared<BaseTransactPlugin>()};
    }
    if (options.transactPluginsOptions) {
        transactPluginsOptions = *options.transactPluginsOptions;
    }
    if (options.abiCache) {
        abiCache = options.abiCache;
    } else {
        abiCache = std::make_shared<ABICache>(client());
    }
}

std::shared_ptr<APIClient> Session::client() const {
    return std::make_shared<APIClient>(APIClientOptions{.url = chain.url, .fetch = fetch});
}

TransactArgs Session::upgradeTransaction(const TransactArgs& args) const {
    // The eosjs loose-header compat branch has no C++ equivalent: TransactArgs
    // is typed, so stray header fields cannot ride along with actions.
    if (args.context_free_actions || args.context_free_data) {
        json tx = {{"expiration", "1970-01-01T00:00:00"},
                   {"ref_block_num", 0},
                   {"ref_block_prefix", 0},
                   {"max_net_usage_words", 0},
                   {"max_cpu_usage_ms", 0},
                   {"delay_sec", 0},
                   {"context_free_actions", json::array()},
                   {"context_free_data", json::array()}};
        if (args.transaction) {
            for (const auto& item : args.transaction->items()) {
                tx[item.key()] = item.value();
            }
        }
        if (!tx.contains("actions") || tx["actions"].is_null()) {
            if (args.actions) {
                tx["actions"] = *args.actions;
            } else if (args.action) {
                tx["actions"] = json::array({*args.action});
            }
        }
        if (args.context_free_actions) {
            tx["context_free_actions"] = *args.context_free_actions;
        }
        if (args.context_free_data) {
            tx["context_free_data"] = *args.context_free_data;
        }
        TransactArgs upgraded;
        upgraded.transaction = std::move(tx);
        return upgraded;
    }
    return args;
}

SigningRequest Session::cloneRequest(const SigningRequest& request,
                                     const std::shared_ptr<ABICache>& cache) const {
    // Lifted from the upstream cloneRequest: rebuild with this abi provider
    return SigningRequest(request.version, request.data,
                          SigningRequestEncodingOptions{.zlib = true, .abiProvider = cache.get()},
                          request.signature);
}

Result<SigningRequest> Session::createRequest(const TransactArgs& args,
                                              const std::shared_ptr<ABICache>& cache) const {
    const SigningRequestEncodingOptions options{.zlib = true, .abiProvider = cache.get()};
    std::optional<SigningRequest> request;
    if (args.request && std::holds_alternative<SigningRequest>(*args.request)) {
        request = cloneRequest(std::get<SigningRequest>(*args.request), cache);
    } else if (args.request) {
        DK_TRY(parsed, SigningRequest::from(std::get<std::string>(*args.request), options));
        request = std::move(parsed);
    } else {
        const TransactArgs upgraded = upgradeTransaction(args);
        SigningRequestCreateArguments createArgs;
        createArgs.action = upgraded.action;
        createArgs.actions = upgraded.actions;
        createArgs.transaction = upgraded.transaction;
        createArgs.chainId = ChainId::from(chain.id);
        DK_TRY(created, SigningRequest::create(createArgs, options));
        request = std::move(created);
    }
    // Always set the broadcast flag to false on signing requests, Wharf
    // needs to do it
    request->setBroadcast(false);
    return *request;
}

Result<SigningRequest> Session::updateRequest(const SigningRequest& previous,
                                              const SigningRequest& modified,
                                              const std::shared_ptr<ABICache>& cache) const {
    SigningRequest updatedRequest = cloneRequest(modified, cache);
    const auto info = updatedRequest.getInfo();
    // Take all the metadata from the previous and set it on the modified
    // request, preserving it as it is modified by various plugins.
    std::visit(
        [&](const auto& d) {
            for (const auto& metadata : d.info) {
                if (info.contains(metadata.key)) {
                    warn("During an updateRequest call, the previous request had already set the "
                         "metadata key of \"" +
                         metadata.key + "\" which will not be overwritten.");
                }
                updatedRequest.setRawInfoKey(metadata.key, metadata.value);
            }
        },
        previous.data);
    return updatedRequest;
}

Result<TransactResult> Session::transact(const TransactArgs& args, const TransactOptions& options) {
    auto pipeline = [&]() -> Result<TransactResult> {
        // The number of seconds before this transaction expires
        const uint32_t expire = options.expireSeconds ? *options.expireSeconds : expireSeconds;

        // Whether or not the request should be broadcast during this call
        const bool willBroadcast = options.broadcast ? *options.broadcast : broadcast;

        // The abi provider to use for this transaction
        DK_TRY(cache, getMergedAbiCache(args, options));

        // The TransactPlugins to use, falling back to the session instance
        const auto& plugins = options.transactPlugins ? *options.transactPlugins : transactPlugins;
        const TransactPluginsOptions& pluginsOptions = options.transactPluginsOptions
                                                           ? *options.transactPluginsOptions
                                                           : transactPluginsOptions;

        // Whether the request can be modified by beforeSign hooks or wallets
        bool modifyAllowed = options.allowModify ? *options.allowModify : allowModify;

        // The context object for this transaction
        TransactContextOptions contextOptions;
        contextOptions.abiCache = cache;
        contextOptions.appName = appName;
        contextOptions.chain = chain;
        contextOptions.client = client();
        contextOptions.createRequest = [this, cache](const TransactArgs& a) {
            return createRequest(a, cache);
        };
        contextOptions.fetch = fetch;
        contextOptions.permissionLevel = permissionLevel;
        contextOptions.storage = storage;
        contextOptions.transactPlugins = plugins;
        contextOptions.transactPluginsOptions = pluginsOptions;
        contextOptions.ui = ui;
        TransactContext context(contextOptions);

        if (context.ui) {
            // Notify the UI that a transaction is about to begin
            DK_CHECK(context.ui->onTransact());
            // Merge in any new localization strings from the plugins
            for (const auto& plugin : plugins) {
                context.ui->addTranslations(getPluginTranslations(*plugin));
            }
        }

        // Process incoming TransactArgs and convert to a SigningRequest
        DK_TRY(initialRequest, createRequest(args, cache));
        SigningRequest request = std::move(initialRequest);

        // Create TransactResult to eventually respond to this call with
        TransactResult result{.chain = chain,
                              .request = request,
                              .resolved = std::nullopt,
                              .response = std::nullopt,
                              .returns = {},
                              .revisions = TransactRevisions(request),
                              .signatures = {},
                              .signer = permissionLevel,
                              .transaction = std::nullopt};

        // Call the beforeSign hooks that were registered by the plugins
        size_t hookIndex = 0;
        for (const auto& hook : context.hooks.beforeSign) {
            // Get the response of the hook by passing a cloned request
            DK_TRY(response, hook(request.clone(), context));
            hookIndex++;
            if (response) {
                // Save revision history for developers to debug modifications
                result.revisions.addRevision(*response, "beforeSign#" + std::to_string(hookIndex),
                                             modifyAllowed);

                // If modification is allowed, change the current request
                if (modifyAllowed) {
                    DK_TRY(updated, updateRequest(request, response->request, cache));
                    request = std::move(updated);
                }

                if (!response->signatures.empty()) {
                    // If signatures were returned, append them
                    result.signatures.insert(result.signatures.end(), response->signatures.begin(),
                                             response->signatures.end());
                    // Disable further modifications once the request is signed
                    modifyAllowed = false;
                }
            }
        }

        // Resolve the final SigningRequest and assign it to the result
        result.request = request;
        DK_TRY(resolved, context.resolve(request, expire));
        result.resolved = resolved;
        result.transaction = resolved.resolvedTransaction;

        // Merge in any new localization strings from the wallet plugin
        if (context.ui) {
            DK_CHECK(context.ui->onSign());
            context.ui->addTranslations(getPluginTranslations(*walletPlugin));
        }

        // Retrieve the signature(s) and request modifications from the wallet
        DK_TRY(walletResponse, walletPlugin->sign(*result.resolved, context));

        // Merge signatures in to the TransactResult
        result.signatures.insert(result.signatures.end(), walletResponse.signatures.begin(),
                                 walletResponse.signatures.end());

        // If a ResolvedSigningRequest was returned from the wallet, determine
        // if it was modified, then if that was allowed.
        if (walletResponse.resolved) {
            const ResolvedSigningRequest& walletResolved = *walletResponse.resolved;
            const bool requestWasModified =
                !result.resolved->transaction.equals(walletResolved.transaction);
            if (requestWasModified) {
                if (modifyAllowed) {
                    result.request = walletResolved.request;
                    result.resolved = walletResolved;
                    result.transaction = walletResolved.resolvedTransaction;
                } else {
                    return err(ErrorKind::Plugin,
                               "The " + walletPlugin->metadata().name.value_or("wallet") +
                                   " plugin modified the transaction when it was not allowed to.");
                }
            }
        }

        // Run the afterSign hooks that were registered by the plugins
        for (const auto& hook : context.hooks.afterSign) {
            DK_CHECK(hook(result, context));
        }

        // Notify the UI that the signing operations are complete
        if (context.ui) {
            DK_CHECK(context.ui->onSignComplete());
        }

        if (willBroadcast) {
            if (context.ui) {
                // Notify the UI that broadcast logic will run
                DK_CHECK(context.ui->onBroadcast());
            }

            // Assemble the SignedTransaction to broadcast
            SignedTransaction signed_;
            static_cast<Transaction&>(signed_) = result.resolved->transaction;
            signed_.signatures = result.signatures;

            // Broadcast and save the API response to the TransactResult
            DK_TRY(response, context.client->v1.chain.send_transaction(signed_));
            result.response = response;

            // Find and process any return values from the transaction
            if (response.contains("processed") && response["processed"].contains("action_traces")) {
                DK_TRY(returns, processReturnValues(response, cache));
                result.returns = std::move(returns);
            }

            // Run the afterBroadcast hooks that were registered by the plugins
            for (const auto& hook : context.hooks.afterBroadcast) {
                DK_CHECK(hook(result, context));
            }

            if (context.ui) {
                // Notify the UI that the broadcast logic completed
                DK_CHECK(context.ui->onBroadcastComplete());
            }
        }

        // Notify the UI that the transaction has completed
        if (context.ui) {
            DK_CHECK(context.ui->onTransactComplete());
        }

        return result;
    };

    auto outcome = pipeline();
    if (!outcome) {
        Error error = outcome.error();
        if (error.kind == ErrorKind::Api) {
            // Surface the first chain error detail message like upstream
            const json details = apierror::details(error);
            if (details.is_array() && !details.empty() && details[0].contains("message")) {
                error.message = details[0]["message"].get<std::string>();
                if (ui) {
                    (void)ui->onError(error);
                }
            }
        } else if (ui) {
            (void)ui->onError(error);
        }
        return err(std::move(error));
    }
    return outcome;
}

Result<std::vector<Signature>> Session::signTransaction(const Transaction& transaction) {
    // Create a TransactContext for the WalletPlugin to use
    TransactContextOptions contextOptions;
    contextOptions.abiCache = abiCache;
    contextOptions.chain = chain;
    contextOptions.client = client();
    contextOptions.createRequest = [this](const TransactArgs& a) {
        return createRequest(a, abiCache);
    };
    contextOptions.fetch = fetch;
    contextOptions.permissionLevel = permissionLevel;
    TransactContext context(contextOptions);
    // Create a request based on the incoming transaction
    SigningRequestCreateArguments createArgs;
    createArgs.transaction = Serializer::objectify(transaction);
    createArgs.chainId = ChainId::from(chain.id);
    DK_TRY(request, SigningRequest::create(createArgs, context.esrOptions()));
    // Always set the broadcast flag to false, Wharf needs to do it
    request.setBroadcast(false);
    // Resolve the request since the WalletPlugin expects one
    DK_TRY(resolvedTransaction,
           structFrom<ResolvedTransaction>(Serializer::objectify(transaction)));
    const ResolvedSigningRequest resolvedRequest(request, permissionLevel, transaction,
                                                 resolvedTransaction, ChainId::from(chain.id));
    // Request the signature from the WalletPlugin
    DK_TRY(walletResponse, walletPlugin->sign(resolvedRequest, context));
    return walletResponse.signatures;
}

SerializedSession Session::serialize() const {
    SerializedSession rv;
    rv.chain = chain.id.hexString();
    rv.actor = permissionLevel.actor.toString();
    rv.permission = permissionLevel.permission.toString();
    rv.walletPlugin = walletPlugin->serialize();
    if (data_.is_object() && !data_.empty()) {
        rv.data = data_;
    }
    return rv;
}

Result<std::shared_ptr<ABICache>> Session::getMergedAbiCache(const TransactArgs& args,
                                                             const TransactOptions& options) {
    std::shared_ptr<ABICache> cache = options.abiCache ? options.abiCache : abiCache;

    // Append all ABIs that exist on the Session
    for (const auto& def : abis) {
        cache->setAbi(def.account, def.abi);
    }

    // ABIs from the TransactOptions
    for (const auto& def : options.abis) {
        cache->setAbi(def.account, def.abi);
    }

    // Merge any partial ABIs from the action(s)
    const auto mergeFromAction = [&](const json& action) -> Result<void> {
        if (action.is_object() && action.contains("abi")) {
            DK_TRY(abi, ABI::from(action["abi"]));
            cache->setAbi(Name::from(action.value("account", "")), abi, true);
        }
        return {};
    };
    if (args.action) {
        DK_CHECK(mergeFromAction(*args.action));
    }
    if (args.actions) {
        for (const auto& action : *args.actions) {
            DK_CHECK(mergeFromAction(action));
        }
    }
    if (args.transaction && args.transaction->contains("actions")) {
        for (const auto& action : args.transaction->at("actions")) {
            DK_CHECK(mergeFromAction(action));
        }
    }

    return cache;
}

}  // namespace dwarfkit
