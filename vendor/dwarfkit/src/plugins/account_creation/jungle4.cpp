#include <dwarfkit/plugins/account_creation/jungle4.hpp>

namespace dwarfkit {

AccountCreationPluginJungle4::AccountCreationPluginJungle4(std::shared_ptr<FetchProvider> fetch)
    : fetch_(std::move(fetch)) {
    config_ = {.requiresChainSelect = false, .supportedChains = {Chains::Jungle4()}};
    metadata_ = AccountCreationPluginMetadata::from(
        json{{"name", "Free Jungle4 Testnet Account"},
             {"description", "Create an account for testing purposes on the Jungle4 testnet."},
             {"homepage", "https://someplace.com"}});
}

std::string AccountCreationPluginJungle4::generateRandomAccountName() {
    // a random 12-character account name using the allowed characters for
    // Antelope accounts (9 random + the .gm suffix)
    static const char* characters = "abcdefghijklmnopqrstuvwxyz12345";
    const auto random = secureRandom(9).value_or(std::vector<uint8_t>(9, 0));
    std::string rv;
    for (int i = 0; i < 9; i++) {
        rv += characters[random[static_cast<size_t>(i)] % 31];
    }
    return rv + ".gm";
}

Result<CreateAccountResponse> AccountCreationPluginJungle4::create(CreateAccountContext& context) {
    const auto t = context.ui->getTranslate(id());

    if (!context.chain) {
        return err(ErrorKind::Plugin, "No chain selected");
    }
    const ChainDefinition chain = *context.chain;

    DK_TRY(privateKey, PrivateKey::generate(KeyType::K1));
    DK_TRY(publicKey, privateKey.toPublic());

    // Default to "jungle4"
    const std::string chainUrl = "https://jungle4.greymass.com";

    // Generate a random account name
    const std::string accountName = generateRandomAccountName();

    // Prepare and send the POST request to create the account
    const std::shared_ptr<FetchProvider> fetch = fetch_ ? fetch_ : context.fetch;
    if (!fetch) {
        return err(ErrorKind::Plugin, "Missing fetch provider");
    }
    FetchRequest request;
    request.url = chainUrl + "/account/create";
    request.method = "POST";
    request.headers = {{"Content-Type", "application/json"}};
    request.body = json{{"accountName", accountName},
                        {"activeKey", publicKey.toString()},
                        {"ownerKey", publicKey.toString()},
                        {"network", chain.id.hexString()}}
                       .dump();
    DK_TRY(response, fetch->fetch(request));

    // If JSON was returned, it contains an error
    const auto contentType =
        std::find_if(response.headers.begin(), response.headers.end(), [](const auto& header) {
            std::string key = header.first;
            for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return key == "content-type";
        });
    if (contentType != response.headers.end() &&
        contentType->second.find("application/json") != std::string::npos) {
        const json body = json::parse(response.body, nullptr, false);
        const std::string message =
            body.is_object() ? body.value("message", response.body) : response.body;
        return err(ErrorKind::Plugin,
                   "There was an error creating this account (" + message + ")");
    }

    // Prompt the user with the results
    PromptArgs prompt;
    prompt.title = t("title", {.defaultValue = "Testnet Account Created!", .values = {}});
    prompt.body =
        t("body",
          {.defaultValue = "Your account has been created. Please save the private key below "
                           "someplace safe and import it into your wallet.",
           .values = {}});
    PromptElement qr;
    qr.type = PromptElementType::qr;
    qr.data = privateKey.toString();
    prompt.elements.push_back(std::move(qr));
    // the upstream copy-to-clipboard button carries an onClick closure; the
    // native prompt exposes the key as element data for the UI to copy
    PromptElement copyButton;
    copyButton.type = PromptElementType::button;
    copyButton.data = json{{"label", "Copy to Clipboard"},
                           {"value", privateKey.toString()},
                           {"variant", "secondary"}};
    prompt.elements.push_back(std::move(copyButton));
    DK_CHECK(context.ui->prompt(prompt, CancelToken()));

    return CreateAccountResponse{chain, Name::from(accountName)};
}

}  // namespace dwarfkit
