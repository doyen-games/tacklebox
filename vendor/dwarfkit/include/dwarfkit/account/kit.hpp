// Port of account src/kit.ts. The Data template parameter mirrors the
// upstream AccountObject generic; upstream reads the type from
// chain.accountDataType, dwarfkit takes it as a template argument (see
// DIVERGENCES.md).
#pragma once

#include <dwarfkit/account/account.hpp>
#include <dwarfkit/common/chains.hpp>

namespace dwarfkit {

struct AccountKitOptions {
    std::optional<Contract> contract;
    std::shared_ptr<APIClient> client;
};

template <typename Data = api::v1::AccountObject>
class AccountKit {
public:
    explicit AccountKit(ChainDefinition chain, AccountKitOptions options = {})
        : chain(std::move(chain)),
          contract(std::move(options.contract)),
          client(options.client
                     ? std::move(options.client)
                     : std::make_shared<APIClient>(APIClientOptions{.url = this->chain.url})) {}

    ChainDefinition chain;
    std::optional<Contract> contract;
    std::shared_ptr<APIClient> client;

    Result<Account<Data>> load(const Name& accountName) const {
        DK_TRY(data, client->template call<Data>(
                         {.path = "/v1/chain/get_account",
                          .params = json{{"account_name", accountName.toString()}}}));
        return Account<Data>({.client = client, .contract = contract, .data = std::move(data)});
    }
};

}  // namespace dwarfkit
