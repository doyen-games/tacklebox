#include <dwarfkit/contract/contract.hpp>

#include <algorithm>

namespace dwarfkit {

Contract::Contract(const ContractArgs& args, const ContractOptions& options)
    : abi(args.abi), account(args.account), client(args.client), debug(options.debug) {}

std::vector<std::string> Contract::tableNames() const {
    std::vector<std::string> rv;
    for (const auto& table : abi.tables) {
        rv.push_back(table.name.toString());
    }
    return rv;
}

bool Contract::hasTable(const Name& name) const {
    return std::any_of(abi.tables.begin(), abi.tables.end(),
                       [&](const auto& table) { return table.name == name; });
}

Result<Table> Contract::table(const Name& name, const json& scope) const {
    if (!hasTable(name)) {
        return err(ErrorKind::NotFound, "Contract (" + account.toString() +
                                            ") does not have a table named (" + name.toString() +
                                            ")");
    }
    TableParams params;
    params.abi = abi;
    params.account = account;
    params.client = client;
    params.debug = debug;
    params.defaultScope = scope;
    params.name = name;
    return Table::from(params);
}

std::vector<std::string> Contract::actionNames() const {
    std::vector<std::string> rv;
    for (const auto& action : abi.actions) {
        rv.push_back(action.name.toString());
    }
    return rv;
}

bool Contract::hasAction(const Name& name) const {
    return std::any_of(abi.actions.begin(), abi.actions.end(),
                       [&](const auto& action) { return action.name == name; });
}

Result<Action> Contract::action(const Name& name, const json& data,
                                const ActionOptions& options) const {
    if (!hasAction(name)) {
        return err(ErrorKind::NotFound, "Contract (" + account.toString() +
                                            ") does not have an action named (" +
                                            name.toString() + ")");
    }
    std::vector<PermissionLevel> authorization = {PlaceholderAuth};
    if (!options.authorization.empty()) {
        authorization = options.authorization;
    }
    json auths = json::array();
    for (const auto& auth : authorization) {
        auths.push_back(Serializer::objectify(auth));
    }
    return Action::from(json{{"account", account.toString()},
                             {"name", name.toString()},
                             {"authorization", auths},
                             {"data", data}},
                        abi);
}

Result<std::vector<Action>> Contract::actions(const std::vector<ActionsArgs>& actionArgs,
                                              const ActionOptions& options) const {
    std::vector<Action> rv;
    for (const auto& args : actionArgs) {
        ActionOptions actionOptions;
        actionOptions.authorization =
            args.authorization.empty() ? options.authorization : args.authorization;
        DK_TRY(built, action(args.name, args.data, actionOptions));
        rv.push_back(std::move(built));
    }
    return rv;
}

Result<json> Contract::readonly(const Name& name, const json& data) const {
    // Generate the action with no authorizations
    DK_TRY(built, action(name, data.is_null() ? json::object() : data));
    built.authorization.clear();
    // Assemble the readonly transaction
    Transaction transaction;
    transaction.actions = {built};
    SignedTransaction signed_;
    static_cast<Transaction&>(signed_) = transaction;
    // Execute and retrieve the response
    DK_TRY(response, client->v1.chain.send_read_only_transaction(signed_));
    if (response.contains("processed") && response["processed"].contains("except") &&
        !response["processed"]["except"].is_null()) {
        return err(ErrorKind::Api, formatExceptionMessage(response["processed"]["except"]));
    }
    // Decode and return results
    const std::string hexData =
        response["processed"]["action_traces"][0].value("return_value_hex_data", "");
    const auto returnType =
        std::find_if(abi.action_results.begin(), abi.action_results.end(),
                     [&](const auto& result) { return result.name == name; });
    if (returnType == abi.action_results.end()) {
        return err(ErrorKind::NotFound,
                   "Return type for " + name.toString() + " not defined in the ABI.");
    }
    DK_TRY(bytes, Bytes::from(hexData));
    return Serializer::decode(bytes.array, returnType->result_type, abi);
}

Result<std::string> Contract::ricardian(const Name& name) const {
    if (!hasAction(name)) {
        return err(ErrorKind::NotFound, "Contract (" + account.toString() +
                                            ") does not have an action named (" +
                                            name.toString() + ")");
    }
    const auto action = std::find_if(abi.actions.begin(), abi.actions.end(),
                                     [&](const auto& a) { return a.name == name; });
    if (action == abi.actions.end() || action->ricardian_contract.empty()) {
        return err(ErrorKind::NotFound, "Contract (" + account.toString() + ") action named (" +
                                            name.toString() +
                                            ") does not have a defined ricardian contract");
    }
    return action->ricardian_contract;
}

}  // namespace dwarfkit
