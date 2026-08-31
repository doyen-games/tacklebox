#include <dwarfkit/plugins/account_creation/anchor.hpp>

namespace dwarfkit {

namespace {

// minimal application/x-www-form-urlencoded escaping for the query string
std::string urlEncode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string rv;
    for (const unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            rv += static_cast<char>(c);
        } else {
            rv += '%';
            rv += hex[c >> 4];
            rv += hex[c & 15];
        }
    }
    return rv;
}

}  // namespace

AccountCreator::AccountCreator(AccountCreationOptions creatorOptions)
    : options(std::move(creatorOptions)) {}

std::string AccountCreator::createAccountUrl() const {
    std::string qs;
    if (!options.supportedChains.empty()) {
        std::string joined;
        for (size_t i = 0; i < options.supportedChains.size(); i++) {
            joined += (i ? "," : "") + options.supportedChains[i].hexString();
        }
        qs += "supported_chains=" + urlEncode(joined);
    }
    if (options.scope.value != 0) {
        qs += (qs.empty() ? "" : "&");
        qs += "scope=" + urlEncode(options.scope.toString());
    }
    return options.creationServiceUrl + "/create?" + qs;
}

Result<json> AccountCreator::createAccount() const {
    if (!options.openDialog) {
        return err(ErrorKind::Unsupported,
                   "The Anchor account creation flow needs a dialog handler: provide "
                   "openDialog to open the url and return the service response.");
    }
    return options.openDialog(createAccountUrl());
}

AccountCreationPluginAnchor::AccountCreationPluginAnchor(
    const AccountCreationPluginAnchorOptions& options)
    : serviceUrl(options.serviceUrl), openDialog(options.openDialog) {
    config_.requiresChainSelect = false;
    config_.supportedChains = options.supportedChains.empty()
                                  ? std::vector<ChainDefinition>{Chains::EOS(), Chains::Telos(),
                                                                 Chains::WAX()}
                                  : options.supportedChains;
    metadata_ = AccountCreationPluginMetadata::from(
        json{{"name", "Purchase account with Anchor"},
             {"description", "Purchase an account and set up an Anchor wallet!"},
             {"homepage", "https://create.anchor.link"}});
}

Result<CreateAccountResponse> AccountCreationPluginAnchor::create(CreateAccountContext& context) {
    AccountCreationOptions creatorOptions;
    creatorOptions.scope = Name::from("wallet");
    if (context.chain) {
        creatorOptions.supportedChains = {ChainId::from(context.chain->id)};
    } else {
        const auto& chains = context.chains.empty() ? config_.supportedChains : context.chains;
        for (const auto& chain : chains) {
            creatorOptions.supportedChains.push_back(ChainId::from(chain.id));
        }
    }
    if (serviceUrl) {
        creatorOptions.creationServiceUrl = *serviceUrl;
    }
    creatorOptions.openDialog = openDialog;
    const AccountCreator accountCreator(creatorOptions);

    // Open the dialog prompting the user to create an account
    DK_TRY(payload, accountCreator.createAccount());

    if (payload.contains("error") && payload["error"].is_string()) {
        const Error error{ErrorKind::Plugin, payload["error"].get<std::string>()};
        (void)context.ui->onError(error);
        return err(error);
    }
    if (!payload.contains("cid") || !payload["cid"].is_string()) {
        const Error error{ErrorKind::Plugin,
                          "No chain ID was returned by the account creation service."};
        (void)context.ui->onError(error);
        return err(error);
    }

    const std::string cid = payload["cid"].get<std::string>();
    const auto& indices = chainIdsToIndices();
    const auto indice = indices.find(cid);
    const ChainDefinition* chain =
        indice == indices.end() ? nullptr : Chains::byIndice(indice->second);
    if (!chain) {
        const Error error{ErrorKind::Plugin, "The chain ID \"" + cid +
                                                 "\" is not supported by this account creation "
                                                 "plugin."};
        (void)context.ui->onError(error);
        return err(error);
    }

    return CreateAccountResponse{*chain, Name::from(payload.value("sa", ""))};
}

}  // namespace dwarfkit
