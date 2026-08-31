#include <dwarfkit/plugins/transact/autocorrect.hpp>

#include <regex>
#include <set>

namespace dwarfkit {

namespace {

enum class ChainFeature { BuyRAM, PowerUp };

struct ChainConfig {
    std::vector<ChainFeature> features;
    std::string sampleAccount;
    std::string symbol;
};

const std::map<std::string, ChainConfig>& chainConfigs() {
    static const std::map<std::string, ChainConfig> configs = {
        // EOS
        {"aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906",
         {{ChainFeature::BuyRAM, ChainFeature::PowerUp}, "eosio.reserv", "4,EOS"}},
        // Jungle 4
        {"73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d",
         {{ChainFeature::BuyRAM, ChainFeature::PowerUp}, "eosio.reserv", "4,EOS"}},
        // WAX
        {"1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4",
         {{ChainFeature::BuyRAM, ChainFeature::PowerUp}, "boost.wax", "8,WAX"}}};
    return configs;
}

bool hasFeature(const ChainConfig& config, ChainFeature feature) {
    return std::find(config.features.begin(), config.features.end(), feature) !=
           config.features.end();
}

// Multiply all resource purchases to provide extra based on inaccurate
// estimates
constexpr double multiplier = 1.5;

Asset::Symbol configSymbol(const ChainConfig& config) {
    return Asset::Symbol::from(config.symbol).value_or(Asset::Symbol());
}

}  // namespace

json getException(const json& response) {
    if (response.contains("error") && !response["error"].is_null()) {
        return response["error"];
    }
    if (response.contains("processed") && response["processed"].contains("except") &&
        !response["processed"]["except"].is_null()) {
        return response["processed"]["except"];
    }
    return json();
}

LocaleDefinitions TransactPluginAutoCorrect::translations() const {
    static const json locales = json::parse(R"({
        "en": {
            "checking": "Checking transaction",
            "fee": {
                "title": "Accept Transaction Fee?",
                "body": "Additional resources ({{resource}}) are required for your account to perform this transaction. Would you like to automatically purchase these resources from the network and proceed?",
                "cost": "Cost of {{resource}}"
            }
        }
    })");
    return locales;
}

void TransactPluginAutoCorrect::register_(TransactContext& context) {
    if (!context.ui) {
        // upstream throws in register; without exceptions the plugin simply
        // declines to install its hook and the beforeSign run reports the
        // error instead
        context.addHook(TransactHookTypes::beforeSign,
                        [](SigningRequest, TransactContext&) -> Result<TransactHookResponseType> {
                            return err(ErrorKind::Plugin,
                                       "The TransactPluginAutoCorrect plugin requires a UI to be "
                                       "present.");
                        });
        return;
    }
    context.addHook(TransactHookTypes::beforeSign,
                    [this](SigningRequest request, TransactContext& ctx) {
                        return this->run(std::move(request), ctx);
                    });
}

Result<TransactHookResponseType> TransactPluginAutoCorrect::run(SigningRequest request,
                                                                TransactContext& context) {
    // Abort if no UI is present
    if (!context.ui) {
        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
    }

    // Reset internal state between transactions
    price_ = std::nullopt;
    resources_.clear();
    iterations_ = 0;

    // Retrieve translation helper from the UI, passing the app ID
    const auto t = context.ui->getTranslate(id());

    // Notify the UI that we are checking the transaction (the upstream
    // cancelable prompt raced against the correction becomes a status line)
    context.ui->status(t("checking", {.defaultValue = "Checking transaction", .values = {}}));

    // Retrieve account data for the current account
    DK_TRY(account, context.client->v1.chain.get_account(context.permissionLevel.actor));

    // Attempt to correct this transaction
    DK_TRY(modified, correct(request, context, account));

    // If the request wasn't modified and no price exists, just return
    const bool unmodified = modified.encode() == request.encode();
    if (unmodified && !price_) {
        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
    }

    // Create unique set of resources that will be purchased
    std::string resourceList;
    const std::set<std::string> unique(resources_.begin(), resources_.end());
    for (const auto& resource : unique) {
        resourceList += (resourceList.empty() ? "" : "/") + resource;
    }

    // Prompt the user to accept the fee
    PromptArgs prompt;
    prompt.title = t("fee.title", {.defaultValue = "Accept Transaction Fee?", .values = {}});
    prompt.body = t("fee.body",
                    {.defaultValue =
                         "Additional resources ({{resource}}) are required for your account to "
                         "perform this transaction. Would you like to automatically purchase "
                         "these resources from the network and proceed?",
                     .values = {{"resource", resourceList}}});
    PromptElement costElement;
    costElement.type = PromptElementType::asset;
    costElement.data = json{{"label", t("fee.cost", {.defaultValue = "Cost of {{resource}}",
                                                     .values = {{"resource", resourceList}}})},
                            {"value", price_ ? price_->toString() : ""}};
    prompt.elements.push_back(std::move(costElement));
    prompt.elements.push_back(PromptElement{.type = PromptElementType::accept});
    const auto promptResponse = context.ui->prompt(prompt, CancelToken());
    if (!promptResponse) {
        if (promptResponse.error().kind == ErrorKind::Canceled) {
            return err(promptResponse.error());
        }
        return TransactHookResponseType{TransactHookResponse{std::move(request), {}}};
    }
    return TransactHookResponseType{TransactHookResponse{std::move(modified), {}}};
}

Result<SigningRequest> TransactPluginAutoCorrect::correct(const SigningRequest& request,
                                                          TransactContext& context,
                                                          api::v1::AccountObject& account) {
    // Keep track of how many iterations have been done
    iterations_++;
    if (iterations_ > 3) {
        return err(ErrorKind::Plugin, "Too many iterations. Please report this bug if you see it.");
    }

    // If the chain is not configured to correct issues or no UI, abort
    const auto configFound = chainConfigs().find(context.chain.id.hexString());
    if (configFound == chainConfigs().end() || !context.ui) {
        return request;
    }
    const ChainConfig& config = configFound->second;

    // Set instance of resource library
    DK_TRY(resources, Resources::make({.api = context.client,
                                       .sampleAccount = config.sampleAccount}));

    // Resolve any placeholders and complete the transaction for compute
    DK_TRY(resolved, context.resolve(request));

    // Call compute_transaction against the resolved transaction
    SignedTransaction signed_;
    static_cast<Transaction&>(signed_) = resolved.transaction;
    const auto response = context.client->v1.chain.compute_transaction(signed_);

    if (response) {
        // Extract any exceptions from the response
        const json exception = getException(*response);
        if (exception.is_object() && exception.contains("stack") &&
            exception["stack"].is_array() && !exception["stack"].empty() &&
            exception["stack"][0].is_object()) {
            const std::string name = exception.value("name", "");
            const json& data = exception["stack"][0]["data"];
            if (name == "tx_net_usage_exceeded") {
                const double needed = data.value("net_usage", 0.0) * multiplier;
                if (hasFeature(config, ChainFeature::PowerUp)) {
                    return powerup(context, resolved, account, resources, 0, needed);
                }
            } else if (name == "tx_cpu_usage_exceeded") {
                const double billed = data.value("billed", 0.0);
                const double billable = data.value("billable", 0.0);
                const double needed = (billed - billable) * multiplier;
                if (hasFeature(config, ChainFeature::PowerUp)) {
                    return powerup(context, resolved, account, resources, needed, 0);
                }
            } else if (name == "ram_usage_exceeded") {
                const double available = data.value("available", 0.0);
                const double needs = data.value("needs", 0.0);
                const double needed = (needs - available) * multiplier;
                if (hasFeature(config, ChainFeature::BuyRAM)) {
                    return buyram(context, resolved, account, resources, needed);
                }
            }
        }
        return request;
    }

    // Handling of exception when it is thrown (API error path)
    const Error& error = response.error();
    if (error.kind == ErrorKind::Api) {
        const std::string name = apierror::name(error);
        const json details = apierror::details(error);
        const std::string message =
            details.is_array() && !details.empty() ? details[0].value("message", "") : "";
        std::smatch match;
        if (name == "tx_net_usage_exceeded") {
            const std::regex pattern("transaction net usage is too high: (\\d+) > (\\d+)");
            if (std::regex_search(message, match, pattern) &&
                hasFeature(config, ChainFeature::PowerUp)) {
                const double needed = std::stod(match[1].str()) * multiplier;
                return powerup(context, resolved, account, resources, 0, needed);
            }
        } else if (name == "tx_cpu_usage_exceeded") {
            const std::regex pattern(
                "billed CPU time \\((\\d+) us\\) is greater than the maximum billable CPU time "
                "for the transaction");
            if (std::regex_search(message, match, pattern) &&
                hasFeature(config, ChainFeature::PowerUp)) {
                const double needed = std::stod(match[1].str()) * multiplier;
                return powerup(context, resolved, account, resources, needed, 0);
            }
        } else if (name == "ram_usage_exceeded") {
            const std::regex pattern(
                "account (\\w.+) has insufficient ram; needs (\\d+) bytes has (\\d+) bytes");
            if (std::regex_search(message, match, pattern) &&
                hasFeature(config, ChainFeature::BuyRAM)) {
                const double needed =
                    (std::stod(match[2].str()) - std::stod(match[3].str())) * multiplier + 2;
                return buyram(context, resolved, account, resources, needed);
            }
        }
    }
    // Return the request
    return request;
}

Result<SigningRequest> TransactPluginAutoCorrect::buyram(TransactContext& context,
                                                         const ResolvedSigningRequest& resolved,
                                                         api::v1::AccountObject& account,
                                                         const Resources& resources,
                                                         double needed) {
    const ChainConfig& config = chainConfigs().at(context.chain.id.hexString());
    DK_TRY(ram, resources.v1().ram.get_state());
    if (!sample_) {
        DK_TRY(usage, resources.getSampledUsage());
        sample_ = usage;
    }

    // Determine price of resources
    DK_TRY(ramPrice, ram.price_per(needed));
    DK_TRY(price, Asset::fromFloat(ramPrice.value(), configSymbol(config)));

    // Keep a running total of the price
    if (price_) {
        price_->units += price.units;
    } else {
        price_ = price;
    }

    // And which resources are being paid for by this fee
    resources_.push_back("RAM");

    // Create a new buyrambytes action to append
    const Buyrambytes data{.payer = resolved.signer.actor,
                           .receiver = resolved.signer.actor,
                           .bytes = static_cast<uint32_t>(needed)};
    Action newAction;
    newAction.account = Name::from("eosio");
    newAction.name = Name::from("buyrambytes");
    newAction.authorization = {resolved.signer};
    DK_TRY(encoded, Serializer::encode(data));
    newAction.data = encoded;

    // Create a new request based on this full transaction
    DK_TRY(newRequest, prependAction(resolved.request, newAction));

    // Attempt to correct the new request
    return correct(newRequest, context, account);
}

Result<SigningRequest> TransactPluginAutoCorrect::powerup(TransactContext& context,
                                                          const ResolvedSigningRequest& resolved,
                                                          api::v1::AccountObject& account,
                                                          const Resources& resources, double cpu,
                                                          double net) {
    const ChainConfig& config = chainConfigs().at(context.chain.id.hexString());
    DK_TRY(state, resources.v1().powerup.get_state());
    if (!sample_) {
        DK_TRY(usage, resources.getSampledUsage());
        sample_ = usage;
    }

    // If powering up, always set a minimum to avoid API speed variance
    if (cpu < 2500) {
        cpu = 2500;
    }
    if (net < 10000) {
        net = 10000;
    }

    // Determine price of resources; a failed price calculation falls back to
    // the smallest representable amount (the upstream try/catch)
    const double minPriceValue = Asset::fromUnits(1, configSymbol(config)).value();
    const auto cpuPriceResult = state.cpu.price_per(*sample_, cpu);
    const double cpuPrice = cpuPriceResult ? *cpuPriceResult : (cpu > 0 ? minPriceValue : 0);
    const auto netPriceResult = state.net.price_per(*sample_, net);
    const double netPrice = netPriceResult ? *netPriceResult : (net > 0 ? minPriceValue : 0);

    // note: upstream multiplies only the NET price here
    DK_TRY(price, Asset::fromFloat(cpuPrice + netPrice * multiplier, configSymbol(config)));

    // Keep a running total of the price
    if (price_) {
        price_->units += price.units;
    } else {
        price_ = price;
    }

    // And which resources are being paid for by this fee
    resources_.push_back("CPU");
    resources_.push_back("NET");

    // Create a new powerup action to append
    DK_TRY(netFrac, state.net.frac(*sample_, net));
    DK_TRY(cpuFrac, state.cpu.frac(*sample_, cpu));
    const Powerup data{.payer = resolved.signer.actor,
                       .receiver = resolved.signer.actor,
                       .days = 1,
                       .net_frac = netFrac,
                       .cpu_frac = cpuFrac,
                       .max_payment = price};
    Action newAction;
    newAction.account = Name::from("eosio");
    newAction.name = Name::from("powerup");
    newAction.authorization = {resolved.signer};
    DK_TRY(encoded, Serializer::encode(data));
    newAction.data = encoded;

    // Create a new request based on this full transaction
    DK_TRY(modifiedRequest, prependAction(resolved.request, newAction));

    // Determine if the account has enough RAM to powerup; 405 appears to be
    // the exact amount, but purchase an additional small buffer
    const int64_t ramNeeded = 410;
    if (account.ram_quota - static_cast<int64_t>(account.ram_usage) < ramNeeded) {
        DK_TRY(ram, resources.v1().ram.get_state());
        const Buyrambytes ramData{.payer = resolved.signer.actor,
                                  .receiver = resolved.signer.actor,
                                  .bytes = static_cast<uint32_t>(ramNeeded)};
        Action ramAction;
        ramAction.account = Name::from("eosio");
        ramAction.name = Name::from("buyrambytes");
        ramAction.authorization = {resolved.signer};
        DK_TRY(ramEncoded, Serializer::encode(ramData));
        ramAction.data = ramEncoded;

        // Modify the account object to prevent multiple purchases while
        // recursing
        account.ram_quota += ramNeeded;

        DK_TRY(withRam, prependAction(modifiedRequest, ramAction));
        modifiedRequest = std::move(withRam);

        // Determine price of resources
        DK_TRY(ramPriceAsset, ram.price_per(static_cast<double>(ramNeeded)));
        DK_TRY(ramPrice, Asset::fromFloat(ramPriceAsset.value(), configSymbol(config)));
        price_->units += ramPrice.units;

        // Notify that RAM is also being purchased
        resources_.push_back("RAM");
    }

    // Attempt to correct the new request
    return correct(modifiedRequest, context, account);
}

}  // namespace dwarfkit
