#include <dwarfkit/plugins/transact/resource_provider.hpp>

#include <algorithm>

namespace dwarfkit {

namespace {

bool sameActionAs(const Action& original, const Action& modified) {
    // Ensure the original contract account matches
    const bool matchesAccount = original.account == modified.account;
    // Ensure the original contract action matches
    const bool matchesName = original.name == modified.name;
    // Ensure the original authorization is intact
    const bool matchesAuthorization =
        original.authorization.size() == modified.authorization.size() &&
        !original.authorization.empty() &&
        original.authorization[0].actor == modified.authorization[0].actor;
    // Ensure the original action data matches
    const bool matchesData = original.data == modified.data;
    return matchesAccount && matchesName && matchesAuthorization && matchesData;
}

}  // namespace

bool hasOriginalActions(const Transaction& original, const Transaction& modified) {
    return std::all_of(original.actions.begin(), original.actions.end(),
                       [&](const Action& originalAction) {
                           return std::any_of(modified.actions.begin(), modified.actions.end(),
                                              [&](const Action& modifiedAction) {
                                                  return sameActionAs(originalAction,
                                                                      modifiedAction);
                                              });
                       });
}

std::vector<Action> getNewActions(const Transaction& original, const Transaction& modified) {
    std::vector<Action> rv;
    for (const Action& modifiedAction : modified.actions) {
        const bool isOriginal =
            std::any_of(original.actions.begin(), original.actions.end(),
                        [&](const Action& originalAction) {
                            return sameActionAs(originalAction, modifiedAction);
                        });
        if (!isOriginal) {
            rv.push_back(modifiedAction);
        }
    }
    return rv;
}

const std::map<std::string, std::string>& TransactPluginResourceProvider::defaultEndpoints() {
    static const std::map<std::string, std::string> endpoints = {
        {"aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906",
         "https://eos.greymass.com"},
        {"73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d",
         "https://jungle4.greymass.com"},
        {"4667b205c6838ef70ff7988f6e8257e8be0e1284a2f59699054a018f743b1d11",
         "https://telos.greymass.com"},
        {"1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4",
         "https://wax.greymass.com"}};
    return endpoints;
}

TransactPluginResourceProvider::TransactPluginResourceProvider(
    const ResourceProviderOptions& options)
    : endpoints(defaultEndpoints()) {
    if (!options.endpoints.empty()) {
        endpoints = options.endpoints;
    }
    if (options.allowFees) {
        allowFees = *options.allowFees;
    }
    if (options.maxFee) {
        maxFee = *options.maxFee;
    }
}

LocaleDefinitions TransactPluginResourceProvider::translations() const {
    // src/translations: en, ko, zh-Hans, zh-Hant
    static const json locales = json::parse(R"({
        "en": {
            "timeout": "The offer from the resource provider has expired.",
            "will-continue": "The transaction will continue without the resource provider.",
            "fee": {
                "title": "Accept Transaction Fee?",
                "body": "Additional resources ({{resource}}) are required for your account to perform this transaction. Would you like to automatically purchase these resources and proceed?",
                "cost": "Cost of {{resource}}"
            },
            "rejected": {
                "no-fees": "A resource provider offered to cover this transaction for a fee, but fee-based transactions are disabled by the configuration using `allowFees = false`.",
                "original-modified": "The original transaction returned by the resource provider has been modified too much. Continuing without the resource provider",
                "max-fee": "The fee requested by the resource provider is unusually high and has been rejected."
            }
        }
    })");
    return locales;
}

void TransactPluginResourceProvider::register_(TransactContext& context) {
    context.addHook(TransactHookTypes::beforeSign,
                    [this](SigningRequest request, TransactContext& ctx) {
                        return this->request(std::move(request), ctx);
                    });
}

std::string TransactPluginResourceProvider::getEndpoint(const ChainDefinition& chain) const {
    const auto found = endpoints.find(chain.id.hexString());
    return found == endpoints.end() ? std::string() : found->second;
}

Result<TransactHookResponseType> TransactPluginResourceProvider::request(
    SigningRequest request, TransactContext& context) {
    // Mock the translation function if no UI is available
    UserInterfaceTranslateFunction t =
        [](const std::string&, const UserInterfaceTranslateOptions& options) {
            return options.defaultValue;
        };
    if (context.ui) {
        t = context.ui->getTranslate(id());
    }
    const auto willContinue = [&] {
        return t("will-continue",
                 {.defaultValue = "The transaction will continue without the resource provider.",
                  .values = {}});
    };

    // Determine appropriate URL for this request; gracefully fail and return
    // the original request when no endpoint was found
    const std::string endpoint = getEndpoint(context.chain);
    if (endpoint.empty()) {
        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
    }

    // Resolve the request as a transaction for placeholders + tapos
    DK_TRY(abis, request.fetchAbis(context.abiCache.get()));
    SigningRequestCreateArguments modifiedArgs;
    if (request.requiresTapos()) {
        DK_TRY(info, context.getInfo());
        const TransactionHeader header = info.getTransactionHeader(120);
        TransactionContext resolveCtx;
        resolveCtx.expiration = header.expiration;
        resolveCtx.ref_block_num = header.ref_block_num;
        resolveCtx.ref_block_prefix = header.ref_block_prefix;
        DK_TRY(resolved,
               request.resolveTransaction(abis, context.permissionLevel, resolveCtx));
        modifiedArgs.transaction = Serializer::objectify(resolved);
        DK_TRY(chainId, request.getChainId());
        modifiedArgs.chainId = chainId;
        if (request.isMultiChain()) {
            DK_TRY(ids, request.getChainIds());
            if (ids) {
                modifiedArgs.chainIds = *ids;
            }
        }
    } else {
        DK_TRY(resolved, request.resolveTransaction(abis, context.permissionLevel));
        modifiedArgs.transaction = Serializer::objectify(resolved);
    }
    DK_TRY(modifiedRequest, SigningRequest::create(modifiedArgs, context.esrOptions()));

    // Validate that this request is valid for the resource provider
    DK_CHECK(validateRequest(modifiedRequest, context));

    // Assemble and perform the request to the resource provider
    const std::string url = endpoint + "/v1/resource_provider/request_transaction";
    if (!context.fetch) {
        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
    }
    FetchRequest fetchRequest;
    fetchRequest.url = url;
    fetchRequest.method = "POST";
    fetchRequest.body = json{{"request", modifiedRequest.encode()},
                             {"signer", Serializer::objectify(context.permissionLevel)}}
                            .dump();
    const auto response = context.fetch->fetch(fetchRequest);
    if (!response) {
        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
    }
    const json responseJson = json::parse(response->body, nullptr, false);
    if (responseJson.is_discarded()) {
        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
    }

    // If the resource provider refused to process this request, or the status
    // isn't an expected 200 or 402, return the original request unmodified
    if (response->status == 400 || (response->status != 200 && response->status != 402)) {
        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
    }

    const bool requiresPayment = response->status == 402;
    if (requiresPayment && !allowFees) {
        // Notify that a fee was required but not allowed via allowFees: false
        if (context.ui) {
            context.ui->status(
                t("rejected.no-fees",
                  {.defaultValue =
                       "A resource provider offered to cover this transaction for a fee, but "
                       "fee-based transactions are disabled by the configuration using "
                       "`allowFees = false`.",
                   .values = {}}) +
                " " + willContinue());
        }
        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
    }

    // Retrieve the transaction from the response and ensure the new
    // transaction has an unmodified version of the original action(s)
    DK_TRY(modifiedTransaction, getModifiedTransaction(responseJson));
    DK_TRY(rawTransaction, modifiedRequest.getRawTransaction());
    const bool originalActionsIntact = hasOriginalActions(rawTransaction, modifiedTransaction);
    if (!originalActionsIntact) {
        if (context.ui) {
            context.ui->status(
                t("rejected.original-modified",
                  {.defaultValue =
                       "The original transaction returned by the resource provider has been "
                       "modified too much. Continuing without the resource provider",
                   .values = {}}) +
                " " + willContinue());
        }
        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
    }

    // Retrieve all newly appended actions from the modified transaction
    const std::vector<Action> addedActions = getNewActions(rawTransaction, modifiedTransaction);

    for (const Action& action : addedActions) {
        const bool isNoop = action.name == Name::from("noop");
        const bool isTransfer = action.name == Name::from("transfer") &&
                                (action.account == Name::from("eosio.token") ||
                                 action.account == Name::from("core.vaulta"));
        const bool isBuyRAMBytes = action.name == Name::from("buyrambytes") &&
                                   (action.account == Name::from("eosio") ||
                                    action.account == Name::from("core.vaulta"));
        if (!isNoop && !isTransfer && !isBuyRAMBytes) {
            if (context.ui) {
                context.ui->status(
                    t("rejected.unexpected-action",
                      {.defaultValue = "The resource provider added an unexpected action to the "
                                       "transaction.",
                       .values = {}}) +
                    " " + willContinue());
            }
            return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
        }
        if (isBuyRAMBytes) {
            DK_TRY(decoded, Serializer::decode<BuyRAMBytes>(action.data.array));
            if (!(decoded.receiver == context.permissionLevel.actor)) {
                if (context.ui) {
                    context.ui->status(
                        t("rejected.invalid-buyrambytes",
                          {.defaultValue = "The resource provider added a RAM purchase for an "
                                           "account other than the requesting account.",
                           .values = {}}) +
                        " " + willContinue());
                }
                return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
            }
        }
    }

    // Sum any fees from added transfer actions and RAM purchases
    std::vector<Asset> allFees;
    std::vector<Action> ramActions;
    for (const Action& action : addedActions) {
        const bool isTokenTransfer = action.name == Name::from("transfer") &&
                                     (action.account == Name::from("eosio.token") ||
                                      action.account == Name::from("core.vaulta"));
        if (isTokenTransfer) {
            DK_TRY(decoded, Serializer::decode<Transfer>(action.data.array));
            allFees.push_back(decoded.quantity);
        }
        if (action.name == Name::from("buyrambytes") &&
            (action.account == Name::from("eosio") ||
             action.account == Name::from("core.vaulta"))) {
            ramActions.push_back(action);
        }
    }
    if (!ramActions.empty()) {
        DK_TRY(resources, Resources::make({.api = context.client}));
        DK_TRY(ramState, resources.v1().ram.get_state());
        for (const Action& action : ramActions) {
            DK_TRY(decoded, Serializer::decode<BuyRAMBytes>(action.data.array));
            DK_TRY(price, ramState.price_per(static_cast<double>(decoded.bytes)));
            allFees.push_back(price);
        }
    }
    Asset addedFees = allFees.empty()
                          ? Asset::fromUnits(0, Asset::Symbol::from("4,TOKEN").value())
                          : Asset::fromUnits(0, allFees[0].symbol);
    for (const Asset& fee : allFees) {
        addedFees.units += fee.units;
    }

    // If the fee was higher than allowed, return the original transaction
    if (maxFee && addedFees.units > maxFee->units) {
        if (context.ui) {
            context.ui->status(
                t("rejected.max-fee",
                  {.defaultValue = "The fee requested by the resource provider is unusually high "
                                   "and has been rejected.",
                   .values = {}}) +
                " " + willContinue());
        }
        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
    }

    // Validate that the response is valid for the session
    DK_CHECK(validateResponseData(responseJson));

    // Create a new signing request based on the response
    DK_TRY(modified, createRequest(responseJson, context));

    std::vector<Signature> signatures;
    for (const auto& sig : responseJson["data"]["signatures"]) {
        DK_TRY(parsed, Signature::from(sig.get<std::string>()));
        signatures.push_back(std::move(parsed));
    }

    if (context.ui && addedFees.value() > 0) {
        // Determine which resources are covered by this fee
        std::vector<std::string> resourceTypes;
        const json& data = responseJson["data"];
        if (data.contains("costs")) {
            const json& costs = data["costs"];
            const auto costValue = [&](const char* key) {
                const auto asset = Asset::from(costs.value(key, "0.0000 EOS"));
                return asset ? asset->value() : 0.0;
            };
            if (costValue("cpu") > 0) resourceTypes.push_back("CPU");
            if (costValue("net") > 0) resourceTypes.push_back("NET");
            if (costValue("ram") > 0) resourceTypes.push_back("RAM");
        } else {
            resourceTypes.push_back("Unknown");
        }
        std::string resourceList;
        for (size_t i = 0; i < resourceTypes.size(); i++) {
            resourceList += (i ? "/" : "") + resourceTypes[i];
        }
        // Prompt the user to accept the fee; the upstream 120s expiry timer
        // (a leftover TODO) is not replicated
        PromptArgs prompt;
        prompt.title = t("fee.title", {.defaultValue = "Accept Transaction Fee?", .values = {}});
        prompt.body = t("fee.body",
                        {.defaultValue =
                             "Additional resources ({{resource}}) are required for your account "
                             "to perform this transaction. Would you like to automatically "
                             "purchase these resources and proceed?",
                         .values = {{"resource", resourceList}}});
        PromptElement costElement;
        costElement.type = PromptElementType::asset;
        costElement.data = json{{"label", t("fee.cost", {.defaultValue = "Cost of {{resource}}",
                                                         .values = {{"resource", resourceList}}})},
                                {"value", addedFees.toString()}};
        prompt.elements.push_back(std::move(costElement));
        prompt.elements.push_back(PromptElement{.type = PromptElementType::accept});
        const auto promptResponse = context.ui->prompt(prompt, CancelToken());
        if (!promptResponse) {
            if (promptResponse.error().kind == ErrorKind::Canceled) {
                // a cancelation aborts the whole transact call
                return err(promptResponse.error());
            }
            // a rejection continues without modification
            return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
        }
        return TransactHookResponseType{
            TransactHookResponse{std::move(modified), std::move(signatures)}};
    }

    // Return the modified transaction and additional signatures
    return TransactHookResponseType{
        TransactHookResponse{std::move(modified), std::move(signatures)}};
}

Result<Transaction> TransactPluginResourceProvider::getModifiedTransaction(
    const json& response) const {
    const json& request = response["data"]["request"];
    const std::string variant = request[0].get<std::string>();
    if (variant == "action") {
        return err(ErrorKind::Plugin,
                   "A resource provider providing an \"action\" is not supported.");
    }
    if (variant == "actions") {
        return err(ErrorKind::Plugin,
                   "A resource provider providing \"actions\" is not supported.");
    }
    if (variant == "transaction") {
        return structFrom<Transaction>(request[1]);
    }
    return err(ErrorKind::Plugin, "Invalid request type provided by resource provider.");
}

Result<SigningRequest> TransactPluginResourceProvider::createRequest(
    const json& response, TransactContext& context) const {
    // Create a new signing request based on the response
    TransactArgs args;
    args.transaction = response["data"]["request"][1];
    DK_TRY(request, context.createRequest(args));

    // Set the required fee onto the request itself for wallets to process
    const json& data = response["data"];
    if (response.value("code", 0) == 402 && data.contains("fee")) {
        DK_TRY(fee, Asset::from(data["fee"].get<std::string>()));
        DK_CHECK(request.setInfoKey("txfee", fee));
    }

    // If the fee costs exist, set them for the signature provider to consume
    if (data.contains("costs")) {
        request.setInfoKey("txfeecpu", std::string_view(data["costs"]["cpu"].get<std::string>()));
        request.setInfoKey("txfeenet", std::string_view(data["costs"]["net"].get<std::string>()));
        request.setInfoKey("txfeeram", std::string_view(data["costs"]["ram"].get<std::string>()));
    }

    return request;
}

Result<void> TransactPluginResourceProvider::validateRequest(const SigningRequest& request,
                                                             TransactContext& context) const {
    // Retrieve first authorizer and ensure it matches session context
    DK_TRY(actions, request.getRawActions());
    if (actions.empty() || actions[0].authorization.empty() ||
        !(actions[0].authorization[0].actor == context.permissionLevel.actor)) {
        return err(ErrorKind::Plugin,
                   "The first authorizer of the transaction does not match this session.");
    }
    return {};
}

Result<void> TransactPluginResourceProvider::validateResponseData(const json& response) const {
    // If the data wasn't provided in the response, error
    if (response.is_null() || !response.is_object()) {
        return err(ErrorKind::Plugin, "Resource provider did not respond to the request.");
    }
    const json data = response.contains("data") ? response["data"] : json();
    // If a malformed response with a fee was provided, error
    if (response.value("code", 0) == 402 && !data.contains("fee")) {
        return err(ErrorKind::Plugin,
                   "Resource provider returned a response indicating required payment, but "
                   "provided no fee amount.");
    }
    // If no signatures were provided, error
    if (!data.contains("signatures") || !data["signatures"].is_array() ||
        data["signatures"].empty()) {
        return err(ErrorKind::Plugin, "Resource provider did not return a signature.");
    }
    return {};
}

}  // namespace dwarfkit
