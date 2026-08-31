// Port of wharfkit/account-creation-plugin-anchor: purchase an account via
// create.anchor.link. The browser popup + postMessage flow becomes an
// embedder-provided dialog handler that opens the url and returns the
// service's response payload (see DIVERGENCES.md).
#pragma once

#include <functional>

#include <dwarfkit/session.hpp>
#include <dwarfkit/signing_request.hpp>

namespace dwarfkit {

struct AccountCreationOptions {
    Name scope;
    std::vector<ChainId> supportedChains;
    std::string creationServiceUrl = "https://create.anchor.link";
    // Opens the account creation url and returns the response payload posted
    // back by the service ({cid, sa, ...} or {error}).
    std::function<Result<json>(const std::string& url)> openDialog;
};

class AccountCreator {
public:
    explicit AccountCreator(AccountCreationOptions options);

    // The url the account creation service dialog should open.
    std::string createAccountUrl() const;

    // Run the dialog and hand back the payload.
    Result<json> createAccount() const;

    AccountCreationOptions options;
};

struct AccountCreationPluginAnchorOptions {
    std::optional<std::string> serviceUrl;
    std::vector<ChainDefinition> supportedChains;
    std::function<Result<json>(const std::string& url)> openDialog;
};

class AccountCreationPluginAnchor final : public AbstractAccountCreationPlugin {
public:
    explicit AccountCreationPluginAnchor(const AccountCreationPluginAnchorOptions& options = {});

    std::string id() const override { return "account-creation-plugin-anchor"; }
    std::string name() const override { return metadata_.name; }
    Result<CreateAccountResponse> create(CreateAccountContext& context) override;

    std::optional<std::string> serviceUrl;
    std::function<Result<json>(const std::string& url)> openDialog;
};

}  // namespace dwarfkit
