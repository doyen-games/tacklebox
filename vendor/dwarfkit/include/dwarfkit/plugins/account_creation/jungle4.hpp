// Port of wharfkit/account-creation-plugin-jungle4: creates a free Jungle 4
// testnet account through the jungle4.greymass.com faucet. The clipboard
// button's onClick has no native equivalent; the prompt carries the key in
// its elements instead (see DIVERGENCES.md).
#pragma once

#include <dwarfkit/session.hpp>

namespace dwarfkit {

class AccountCreationPluginJungle4 final : public AbstractAccountCreationPlugin {
public:
    explicit AccountCreationPluginJungle4(std::shared_ptr<FetchProvider> fetch = nullptr);

    std::string id() const override { return "account-creation-plugin-jungle4"; }
    std::string name() const override { return metadata_.name; }
    Result<CreateAccountResponse> create(CreateAccountContext& context) override;

    // exposed for tests: a random 9-char base31 name with the .gm suffix
    static std::string generateRandomAccountName();

private:
    std::shared_ptr<FetchProvider> fetch_;
};

}  // namespace dwarfkit
