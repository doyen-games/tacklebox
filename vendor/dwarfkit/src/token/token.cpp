#include <dwarfkit/token/token.hpp>

namespace dwarfkit {

Token::Token(const TokenOptions& options)
    : client(options.client),
      contractKit({.client = options.client}),
      contract(options.contract ? *options.contract : system_token::contract(options.client)) {}

Result<Contract> Token::getContract(const std::optional<Name>& contractName) const {
    if (contractName) {
        return contractKit.load(*contractName);
    }
    return contract;
}

Result<Action> Token::transfer(const Name& from, const Name& to, const Asset& amount,
                               const std::string& memo) const {
    DK_TRY(tokenContract, getContract());
    return tokenContract.action(Name::from("transfer"), json{{"from", from.toString()},
                                                             {"to", to.toString()},
                                                             {"quantity", amount.toString()},
                                                             {"memo", memo}});
}

Result<Action> Token::transfer(const Name& from, const Name& to, std::string_view amount,
                               const std::string& memo) const {
    DK_TRY(quantity, Asset::from(amount));
    return transfer(from, to, quantity, memo);
}

Result<Asset> Token::balance(const Name& accountName,
                             const std::optional<std::string>& symbolCode,
                             const std::optional<Name>& contractName) const {
    const auto wrap = [&](const std::string& message) {
        return err(ErrorKind::Api,
                   "Failed to fetch balance for " + accountName.toString() + ": " + message);
    };
    DK_TRY(tokenContract, getContract(contractName));
    DK_TRY(table, tokenContract.table(Name::from("accounts"), accountName.toString()));
    std::optional<Asset::SymbolCode> code;
    Result<std::optional<json>> row = std::optional<json>();
    if (symbolCode) {
        DK_TRY(parsed, Asset::SymbolCode::from(*symbolCode));
        code = parsed;
        QueryParams params;
        params.index_position = "primary";
        row = table.get(json(parsed.value), params);
    } else {
        row = table.get();
    }
    if (!row) {
        return wrap(row.error().message);
    }
    if (!row->has_value()) {
        return wrap("Account " + accountName.toString() + " does not exist.");
    }
    DK_TRY(accountBalance, Asset::from((**row)["balance"].get_ref<const std::string&>()));
    if (code && !accountBalance.symbol.code().equals(*code)) {
        return wrap("Symbol '" + *symbolCode + "' does not exist.");
    }
    return accountBalance;
}

}  // namespace dwarfkit
