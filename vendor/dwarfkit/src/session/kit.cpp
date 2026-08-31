#include <dwarfkit/session/kit.hpp>

#include <algorithm>

#include <dwarfkit/session/utils.hpp>

namespace dwarfkit {

namespace {

// json text for the storage layer, matching JSON.stringify of the TS shapes
std::string stringifySessions(const std::vector<SerializedSession>& sessions) {
    json array = json::array();
    for (const auto& session : sessions) {
        array.push_back(session.toJSON());
    }
    return array.dump();
}

Result<std::vector<SerializedSession>> parseSessions(const std::string& data) {
    const json parsed = json::parse(data, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) {
        return err(ErrorKind::Storage, "Failed to parse sessions from storage");
    }
    std::vector<SerializedSession> rv;
    for (const auto& item : parsed) {
        DK_TRY(session, SerializedSession::fromJSON(item));
        rv.push_back(std::move(session));
    }
    return rv;
}

bool sameSession(const SerializedSession& a, const SerializedSession& b) {
    return a.chain == b.chain && a.actor == b.actor && a.permission == b.permission;
}

}  // namespace

SessionKit::SessionKit(const SessionKitArgs& args, const SessionKitOptions& options)
    : abis(options.abis),
      appName(args.appName),
      fetch(options.fetch),
      storage(options.storage),
      ui(args.ui),
      walletPlugins(args.walletPlugins),
      accountCreationPlugins(options.accountCreationPlugins),
      chains(args.chains) {
    if (options.loginPlugins) {
        loginPlugins = *options.loginPlugins;
    } else {
        loginPlugins = {std::make_shared<BaseLoginPlugin>()};
    }
    if (!storage) {
        storage = std::make_shared<MemorySessionStorage>();
    }
    if (options.transactPlugins) {
        transactPlugins = *options.transactPlugins;
    } else {
        transactPlugins = {std::make_shared<BaseTransactPlugin>()};
    }
    if (options.allowModify) {
        allowModify = *options.allowModify;
    }
    if (options.expireSeconds) {
        expireSeconds = *options.expireSeconds;
    }
    if (options.transactPluginsOptions) {
        transactPluginsOptions = *options.transactPluginsOptions;
    }
}

Result<void> SessionKit::setEndpoint(const Checksum256& id, const std::string& url) {
    const auto chain = std::find_if(chains.begin(), chains.end(),
                                    [&](const ChainDefinition& c) { return c.id == id; });
    if (chain == chains.end()) {
        return err(ErrorKind::NotFound, "Chain with specified ID not found.");
    }
    chain->url = url;
    return {};
}

Result<ChainDefinition> SessionKit::getChainDefinition(
    const Checksum256& id, const std::optional<std::vector<ChainDefinition>>& override) const {
    const auto& list = override ? *override : chains;
    const auto chain = std::find_if(list.begin(), list.end(),
                                    [&](const ChainDefinition& c) { return c.id == id; });
    if (chain == list.end()) {
        return err(ErrorKind::NotFound, "No chain defined with an ID of: " + id.hexString());
    }
    return *chain;
}

Result<CreateAccountResponse> SessionKit::createAccount(const CreateAccountOptions& options) {
    auto pipeline = [&]() -> Result<CreateAccountResponse> {
        if (accountCreationPlugins.empty()) {
            return err(ErrorKind::Plugin, "No account creation plugins available.");
        }

        // Establish defaults based on options
        std::optional<ChainDefinition> chain = options.chain;
        bool requiresChainSelect = !chain.has_value();
        bool requiresPluginSelect = !options.pluginId.has_value();

        std::shared_ptr<AccountCreationPlugin> accountCreationPlugin;

        // Developer specified a plugin during the createAccount call
        if (options.pluginId) {
            requiresPluginSelect = false;
            const auto found = std::find_if(
                accountCreationPlugins.begin(), accountCreationPlugins.end(),
                [&](const auto& plugin) { return plugin->id() == *options.pluginId; });
            if (found == accountCreationPlugins.end()) {
                return err(ErrorKind::Plugin, "Invalid account creation plugin selected.");
            }
            accountCreationPlugin = *found;

            // Override the chain selection requirement based on the plugin
            requiresChainSelect = accountCreationPlugin->config().requiresChainSelect;

            // Without chain select and exactly one supported chain, default it
            const auto& supported = accountCreationPlugin->config().supportedChains;
            if (!accountCreationPlugin->config().requiresChainSelect && supported.size() == 1) {
                chain = supported[0];
            }
        }

        // The chains available to select from, based on the Session Kit
        std::vector<ChainDefinition> availableChains = chains;

        // With a selected plugin, filter down to the chains it supports
        if (accountCreationPlugin && !accountCreationPlugin->config().supportedChains.empty()) {
            const auto& supported = accountCreationPlugin->config().supportedChains;
            std::vector<ChainDefinition> filtered;
            for (const auto& available : availableChains) {
                if (std::any_of(supported.begin(), supported.end(),
                                [&](const ChainDefinition& c) { return c.id == available.id; })) {
                    filtered.push_back(available);
                }
            }
            availableChains = std::move(filtered);
        }

        CreateAccountContextOptions contextOptions;
        contextOptions.accountCreationPlugins = accountCreationPlugins;
        contextOptions.appName = appName;
        contextOptions.chain = chain;
        contextOptions.chains = availableChains;
        contextOptions.fetch = fetch;
        contextOptions.ui = ui;
        contextOptions.uiRequirements = {requiresChainSelect, requiresPluginSelect};
        CreateAccountContext context(contextOptions);

        // If UI interaction is required before triggering the plugin
        if (requiresPluginSelect || requiresChainSelect) {
            DK_TRY(response, context.ui->onAccountCreate(context));

            // Set pluginId based on options first, then response
            const std::optional<std::string> pluginId =
                options.pluginId ? options.pluginId : response.pluginId;
            if (!pluginId) {
                return err(ErrorKind::Plugin, "No account creation plugin selected.");
            }

            const auto found = std::find_if(
                context.accountCreationPlugins.begin(), context.accountCreationPlugins.end(),
                [&](const auto& plugin) { return plugin->id() == *pluginId; });
            if (found == context.accountCreationPlugins.end()) {
                return err(ErrorKind::Plugin, "No account creation plugin selected.");
            }
            accountCreationPlugin = *found;

            // Without chain select and exactly one supported chain, default it
            const auto& supported = accountCreationPlugin->config().supportedChains;
            if (!accountCreationPlugin->config().requiresChainSelect && supported.size() == 1) {
                context.chain = supported[0];
            }

            // Set chain based on response
            if (response.chain) {
                DK_TRY(selected, getChainDefinition(*response.chain, context.chains));
                context.chain = selected;
            }

            // Ensure a chain was selected when the plugin requires it
            if (accountCreationPlugin->config().requiresChainSelect && !context.chain) {
                return err(ErrorKind::Plugin,
                           "Account creation plugin (" + *pluginId +
                               ") requires chain selection, and no chain was selected.");
            }
        }

        if (!accountCreationPlugin) {
            return err(ErrorKind::Plugin, "No account creation plugin selected");
        }

        // Call the account creation plugin with the context
        DK_TRY(accountCreationData, accountCreationPlugin->create(context));

        // Notify the UI we're done
        DK_CHECK(context.ui->onAccountCreateComplete());

        return accountCreationData;
    };

    auto outcome = pipeline();
    if (!outcome) {
        (void)ui->onError(outcome.error());
    }
    return outcome;
}

Result<LoginResult> SessionKit::login(const LoginOptions& options) {
    auto pipeline = [&]() -> Result<LoginResult> {
        // Create LoginContext for this login request
        LoginContextOptions contextOptions;
        contextOptions.appName = appName;
        contextOptions.arbitrary = options.arbitrary;
        if (options.chains) {
            std::vector<ChainDefinition> subset;
            for (const auto& id : *options.chains) {
                DK_TRY(chain, getChainDefinition(id));
                subset.push_back(chain);
            }
            contextOptions.chains = std::move(subset);
        } else {
            contextOptions.chains = chains;
        }
        contextOptions.fetch = fetch;
        contextOptions.loginPlugins = loginPlugins;
        contextOptions.ui = ui;
        for (const auto& plugin : walletPlugins) {
            UserInterfaceWalletPlugin uiPlugin;
            uiPlugin.config = plugin->config();
            uiPlugin.metadata = plugin->metadata();
            if (plugin->hasRetrievePublicKey()) {
                uiPlugin.retrievePublicKey = [plugin](const Checksum256& chainId) {
                    return plugin->retrievePublicKey(chainId);
                };
            }
            contextOptions.walletPlugins.push_back(std::move(uiPlugin));
        }
        LoginContext context(contextOptions);

        // Tell the UI a login request is beginning
        DK_CHECK(context.ui->onLogin());

        // Predetermine WalletPlugin (if possible) to avoid UI interactions
        std::shared_ptr<WalletPlugin> walletPlugin;
        if (walletPlugins.size() == 1) {
            walletPlugin = walletPlugins[0];  // Default to first when only one
            context.walletPluginIndex = 0;
            context.uiRequirements.requiresWalletSelect = false;
        } else if (options.walletPlugin) {
            const auto found =
                std::find_if(walletPlugins.begin(), walletPlugins.end(),
                             [&](const auto& plugin) { return plugin->id() == *options.walletPlugin; });
            if (found != walletPlugins.end()) {
                walletPlugin = *found;
                context.walletPluginIndex = static_cast<int>(found - walletPlugins.begin());
                context.uiRequirements.requiresWalletSelect = false;
            }
        }
        // Set any uiRequirement overrides from the wallet plugin
        if (walletPlugin) {
            context.uiRequirements.requiresChainSelect = walletPlugin->config().requiresChainSelect;
            context.uiRequirements.requiresPermissionSelect =
                walletPlugin->config().requiresPermissionSelect;
            if (walletPlugin->config().requiresPermissionEntry) {
                context.uiRequirements.requiresPermissionEntry =
                    *walletPlugin->config().requiresPermissionEntry;
            }
            context.ui->addTranslations(getPluginTranslations(*walletPlugin));
        }

        // Predetermine chain (if possible) to avoid UI interactions
        if (options.chain) {
            if (std::holds_alternative<ChainDefinition>(*options.chain)) {
                context.chain = std::get<ChainDefinition>(*options.chain);
            } else {
                DK_TRY(chain,
                       getChainDefinition(std::get<Checksum256>(*options.chain), context.chains));
                context.chain = chain;
            }
            context.uiRequirements.requiresChainSelect = false;
        } else if (context.chains.size() == 1) {
            context.chain = context.chains[0];
            context.uiRequirements.requiresChainSelect = false;
        } else {
            context.uiRequirements.requiresChainSelect = true;
        }

        // Predetermine permission (if possible) to avoid UI interactions
        if (options.permissionLevel) {
            context.permissionLevel = options.permissionLevel;
            context.uiRequirements.requiresPermissionSelect = false;
        }

        // Determine if the login process requires any user interaction
        if (context.uiRequirements.requiresChainSelect ||
            context.uiRequirements.requiresPermissionSelect ||
            context.uiRequirements.requiresPermissionEntry ||
            context.uiRequirements.requiresWalletSelect) {
            // Perform the UserInterface login flow to determine the chain,
            // permission and WalletPlugin
            DK_TRY(uiLoginResponse, context.ui->login(context));

            // Attempt to set the current WalletPlugin to the requested index
            if (uiLoginResponse.walletPluginIndex >= 0 &&
                static_cast<size_t>(uiLoginResponse.walletPluginIndex) < walletPlugins.size()) {
                walletPlugin = walletPlugins[static_cast<size_t>(uiLoginResponse.walletPluginIndex)];
            } else {
                walletPlugin = nullptr;
            }
            if (!walletPlugin) {
                return err(ErrorKind::Plugin,
                           "UserInterface did not return a valid WalletPlugin index.");
            }

            // Attempt to set the current chain to match the UI response
            if (uiLoginResponse.chainId) {
                // Ensure the chain ID returned by the UI is in the list
                if (!std::any_of(context.chains.begin(), context.chains.end(),
                                 [&](const ChainDefinition& c) {
                                     return c.id == *uiLoginResponse.chainId;
                                 })) {
                    return err(ErrorKind::Invalid,
                               "UserInterface did not return a chain ID matching the subset of "
                               "chains.");
                }
                DK_TRY(chain, getChainDefinition(*uiLoginResponse.chainId, context.chains));
                context.chain = chain;
            }

            // Set the PermissionLevel from the UI response to the context
            if (uiLoginResponse.permissionLevel) {
                context.permissionLevel = uiLoginResponse.permissionLevel;
            }
        }

        if (!walletPlugin) {
            return err(ErrorKind::Plugin, "No WalletPlugin available to perform the login.");
        }

        // Ensure the wallet plugin supports the chain that was selected
        const auto& supportedChains = walletPlugin->config().supportedChains;
        if (context.chain && !supportedChains.empty() &&
            std::find(supportedChains.begin(), supportedChains.end(),
                      context.chain->id.hexString()) == supportedChains.end()) {
            return err(ErrorKind::Plugin,
                       "The wallet plugin '" + walletPlugin->metadata().name.value_or("unknown") +
                           "' does not support the chain '" + context.chain->id.hexString() + "'");
        }

        // Call the beforeLogin hooks that were registered by the LoginPlugins
        for (const auto& hook : context.hooks.beforeLogin) {
            DK_CHECK(hook(context));
        }

        // Perform the login request against the selected walletPlugin
        DK_TRY(response, walletPlugin->login(context));

        // Create a session from the resulting login response
        DK_TRY(sessionChain, getChainDefinition(response.chain));
        SessionArgs sessionArgs;
        sessionArgs.chain = sessionChain;
        sessionArgs.permissionLevel = response.permissionLevel;
        sessionArgs.walletPlugin = walletPlugin;
        sessionArgs.appName = appName;
        const auto session =
            std::make_shared<Session>(sessionArgs, getSessionOptions(options));

        // Call the afterLogin hooks that were registered by the LoginPlugins
        for (const auto& hook : context.hooks.afterLogin) {
            DK_CHECK(hook(context));
        }

        // Save the session to storage if it has a storage instance
        DK_CHECK(persistSession(*session, options.setAsDefault.value_or(true)));

        // Notify the UI that the login request has completed
        DK_CHECK(context.ui->onLoginComplete());

        return LoginResult{std::move(context), std::move(response), session};
    };

    auto outcome = pipeline();
    if (!outcome) {
        (void)ui->onError(outcome.error());
    }
    return outcome;
}

LogoutContext SessionKit::logoutParams(const SerializedSession& serialized,
                                       const std::shared_ptr<WalletPlugin>& walletPlugin) const {
    LogoutContext rv;
    rv.appName = appName;
    const auto chain = getChainDefinition(Checksum256::from(serialized.chain).value_or(Checksum256()));
    SessionArgs args;
    if (chain) {
        args.chain = *chain;
    }
    args.permissionLevel = PermissionLevel{Name::from(serialized.actor),
                                           Name::from(serialized.permission)};
    args.walletPlugin = walletPlugin;
    rv.session = std::make_shared<Session>(args, SessionOptions{.fetch = fetch});
    return rv;
}

Result<void> SessionKit::logoutSerialized(const SerializedSession& serialized) {
    if (!storage) {
        return err(ErrorKind::Storage,
                   "An instance of Storage must be provided to utilize the logout method.");
    }
    DK_CHECK(storage->remove("session"));
    const auto walletPlugin =
        std::find_if(walletPlugins.begin(), walletPlugins.end(), [&](const auto& plugin) {
            return plugin->id() == serialized.walletPlugin.id;
        });
    if (walletPlugin != walletPlugins.end() && (*walletPlugin)->hasLogout()) {
        DK_CHECK((*walletPlugin)->logout(logoutParams(serialized, *walletPlugin)));
    }
    DK_TRY(sessions, getSessions());
    std::vector<SerializedSession> other;
    for (const auto& session : sessions) {
        if (!sameSession(session, serialized)) {
            other.push_back(session);
        }
    }
    return storage->write("sessions", stringifySessions(other));
}

Result<void> SessionKit::logout(const Session& session) {
    return logoutSerialized(session.serialize());
}

Result<void> SessionKit::logout(const SerializedSession& session) {
    return logoutSerialized(session);
}

Result<void> SessionKit::logout() {
    if (!storage) {
        return err(ErrorKind::Storage,
                   "An instance of Storage must be provided to utilize the logout method.");
    }
    DK_CHECK(storage->remove("session"));
    DK_TRY(sessions, getSessions());
    DK_CHECK(storage->remove("sessions"));
    for (const auto& serialized : sessions) {
        const auto walletPlugin =
            std::find_if(walletPlugins.begin(), walletPlugins.end(), [&](const auto& plugin) {
                return plugin->id() == serialized.walletPlugin.id;
            });
        if (walletPlugin != walletPlugins.end() && (*walletPlugin)->hasLogout()) {
            DK_CHECK((*walletPlugin)->logout(logoutParams(serialized, *walletPlugin)));
        }
    }
    return {};
}

Result<std::shared_ptr<Session>> SessionKit::restore() {
    DK_TRY(data, storage->read("session"));
    if (!data) {
        return std::shared_ptr<Session>();
    }
    const json parsed = json::parse(*data, nullptr, false);
    if (parsed.is_discarded()) {
        return err(ErrorKind::Storage, "Failed to parse session from storage");
    }
    DK_TRY(serialized, SerializedSession::fromJSON(parsed));
    return restore(serialized);
}

Result<std::shared_ptr<Session>> SessionKit::restore(const SerializedSession& serialized,
                                                     const LoginOptions& options) {
    RestoreArgs args;
    DK_TRY(chainId, Checksum256::from(serialized.chain));
    args.chain = chainId;
    args.actor = serialized.actor;
    args.permission = serialized.permission;
    args.walletPlugin = serialized.walletPlugin;
    args.data = serialized.data;
    return restore(args, options);
}

Result<std::shared_ptr<Session>> SessionKit::restore(const RestoreArgs& args,
                                                     const LoginOptions& options) {
    const Checksum256& chainId = args.chain;

    std::optional<SerializedSession> serializedSession;

    // Retrieve all sessions from storage
    DK_TRY(data, storage->read("sessions"));

    if (data) {
        // If sessions exist, restore the one that matches the provided args
        DK_TRY(sessions, parseSessions(*data));
        if (args.actor && args.permission) {
            // If all args are provided, return exact match
            const auto found = std::find_if(sessions.begin(), sessions.end(), [&](const auto& s) {
                return Checksum256::from(s.chain).value_or(Checksum256()) == chainId &&
                       s.actor == *args.actor && s.permission == *args.permission;
            });
            if (found != sessions.end()) {
                serializedSession = *found;
            }
        } else {
            // If no actor/permission defined, return based on chain
            const auto found = std::find_if(sessions.begin(), sessions.end(), [&](const auto& s) {
                return Checksum256::from(s.chain).value_or(Checksum256()) == chainId &&
                       s.isDefault.value_or(false);
            });
            if (found != sessions.end()) {
                serializedSession = *found;
            }
        }
    } else {
        // No sessions found: if args carry the full session data, use them
        if (args.actor && args.permission && args.walletPlugin) {
            SerializedSession fromArgs;
            fromArgs.chain = chainId.hexString();
            fromArgs.actor = *args.actor;
            fromArgs.permission = *args.permission;
            fromArgs.walletPlugin = *args.walletPlugin;
            fromArgs.data = args.data;
            serializedSession = std::move(fromArgs);
        } else {
            return err(ErrorKind::NotFound,
                       "No sessions found in storage. A wallet plugin must be provided.");
        }
    }

    // If no session found, return
    if (!serializedSession) {
        return std::shared_ptr<Session>();
    }

    // Ensure a WalletPlugin was found with the provided ID
    const auto walletPlugin =
        std::find_if(walletPlugins.begin(), walletPlugins.end(), [&](const auto& plugin) {
            return plugin->id() == serializedSession->walletPlugin.id;
        });
    if (walletPlugin == walletPlugins.end()) {
        return err(ErrorKind::Plugin, "No WalletPlugin found with the ID of: '" +
                                          serializedSession->walletPlugin.id + "'");
    }

    // Set the wallet data from the serialized session
    if (!serializedSession->walletPlugin.data.is_null()) {
        (*walletPlugin)->setData(serializedSession->walletPlugin.data);
    }

    // If walletPlugin data was provided by args, override
    if (args.walletPlugin && !args.walletPlugin->data.is_null() &&
        !(args.walletPlugin->data.is_object() && args.walletPlugin->data.empty())) {
        (*walletPlugin)->setData(args.walletPlugin->data);
    }

    // Create a new session from the provided args
    DK_TRY(chain, getChainDefinition(Checksum256::from(serializedSession->chain).value_or(chainId)));
    SessionArgs sessionArgs;
    sessionArgs.chain = chain;
    sessionArgs.permissionLevel = PermissionLevel{Name::from(serializedSession->actor),
                                                  Name::from(serializedSession->permission)};
    sessionArgs.walletPlugin = *walletPlugin;
    sessionArgs.appName = appName;
    const auto session = std::make_shared<Session>(sessionArgs, getSessionOptions(options));

    if (!serializedSession->data.is_null()) {
        session->setData(serializedSession->data);
    }

    // Save the session to storage
    DK_CHECK(persistSession(*session, options.setAsDefault.value_or(true)));

    return session;
}

Result<std::vector<std::shared_ptr<Session>>> SessionKit::restoreAll() {
    std::vector<std::shared_ptr<Session>> sessions;
    DK_TRY(serializedSessions, getSessions());
    for (const auto& serialized : serializedSessions) {
        DK_TRY(session, restore(serialized));
        if (session) {
            sessions.push_back(session);
        }
    }
    return sessions;
}

Result<void> SessionKit::persistSession(const Session& session, bool setAsDefault) {
    // If no storage exists, do nothing
    if (!storage) {
        return {};
    }

    // Serialize session passed in
    SerializedSession serialized = session.serialize();

    // Specify whether or not this is now the default for the given chain
    serialized.isDefault = setAsDefault;

    // Set this as the current session for all chains
    if (setAsDefault) {
        DK_CHECK(storage->write("session", serialized.toJSON().dump()));
    }

    // Add the current session to the list of sessions, preventing duplicates
    DK_TRY(existing, storage->read("sessions"));
    if (existing) {
        DK_TRY(stored, parseSessions(*existing));
        std::vector<SerializedSession> sessions;
        for (auto& s : stored) {
            // Filter out any matching session to ensure no duplicates
            if (sameSession(s, serialized)) {
                continue;
            }
            // Remove the default status from other sessions for this chain
            if (s.chain == serialized.chain) {
                s.isDefault = false;
            }
            sessions.push_back(std::move(s));
        }

        // Merge and sort sessions by chain, actor and permission
        sessions.push_back(serialized);
        std::sort(sessions.begin(), sessions.end(), [](const auto& a, const auto& b) {
            if (a.chain != b.chain) return a.chain < b.chain;
            if (a.actor != b.actor) return a.actor < b.actor;
            return a.permission < b.permission;
        });

        return storage->write("sessions", stringifySessions(sessions));
    }
    return storage->write("sessions", stringifySessions({serialized}));
}

Result<std::vector<SerializedSession>> SessionKit::getSessions() const {
    if (!storage) {
        return err(ErrorKind::Storage, "No storage instance is available to retrieve sessions from.");
    }
    DK_TRY(data, storage->read("sessions"));
    if (!data) {
        return std::vector<SerializedSession>{};
    }
    DK_TRY(parsed, parseSessions(*data));
    // Only return sessions that have a currently registered wallet plugin
    std::vector<SerializedSession> filtered;
    for (const auto& session : parsed) {
        if (std::any_of(walletPlugins.begin(), walletPlugins.end(), [&](const auto& plugin) {
                return plugin->id() == session.walletPlugin.id;
            })) {
            filtered.push_back(session);
        }
    }
    return filtered;
}

SessionOptions SessionKit::getSessionOptions(const LoginOptions& options) const {
    SessionOptions rv;
    rv.abis = abis;
    rv.allowModify = allowModify;
    rv.expireSeconds = expireSeconds;
    rv.fetch = fetch;
    rv.storage = storage;
    rv.transactPlugins = options.transactPlugins ? *options.transactPlugins : transactPlugins;
    rv.transactPluginsOptions =
        options.transactPluginsOptions ? *options.transactPluginsOptions : transactPluginsOptions;
    rv.ui = ui;
    return rv;
}

}  // namespace dwarfkit
