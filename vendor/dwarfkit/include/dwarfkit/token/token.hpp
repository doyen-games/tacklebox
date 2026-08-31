// Port of token src/index.ts.
#pragma once

#include <dwarfkit/contract/kit.hpp>
#include <dwarfkit/token/system_token.hpp>

namespace dwarfkit {

struct TokenOptions {
    std::shared_ptr<APIClient> client;
    std::optional<Contract> contract;
};

class Token {
public:
    explicit Token(const TokenOptions& options);

    std::shared_ptr<APIClient> client;
    ContractKit contractKit;
    Contract contract;

    // The token contract to operate on: a named contract loaded through the
    // kit, or the default (system token) contract.
    Result<Contract> getContract(const std::optional<Name>& contractName = {}) const;

    Result<Action> transfer(const Name& from, const Name& to, const Asset& amount,
                            const std::string& memo = "") const;
    Result<Action> transfer(const Name& from, const Name& to, std::string_view amount,
                            const std::string& memo = "") const;

    Result<Asset> balance(const Name& accountName,
                          const std::optional<std::string>& symbolCode = {},
                          const std::optional<Name>& contractName = {}) const;
};

}  // namespace dwarfkit
