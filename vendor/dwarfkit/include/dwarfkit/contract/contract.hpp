// Port of contract src/contract.ts.
#pragma once

#include <dwarfkit/contract/table.hpp>
#include <dwarfkit/signing_request.hpp>

namespace dwarfkit {

struct ContractArgs {
    ABI abi;
    Name account;
    std::shared_ptr<APIClient> client;
};

struct ContractOptions {
    bool debug = false;
};

struct ActionOptions {
    std::vector<PermissionLevel> authorization;
};

struct ActionsArgs {
    Name name;
    // typed bytes (hex string) or the action data object
    json data;
    std::vector<PermissionLevel> authorization;
};

// A smart contract deployed to a specific blockchain: calling actions,
// reading tables, getting the contract's ABI.
class Contract {
public:
    Contract() = default;
    explicit Contract(const ContractArgs& args, const ContractOptions& options = {});

    ABI abi;
    Name account;
    std::shared_ptr<APIClient> client;
    bool debug = false;

    std::vector<std::string> tableNames() const;
    bool hasTable(const Name& name) const;
    Result<Table> table(const Name& name, const json& scope = {}) const;

    std::vector<std::string> actionNames() const;
    bool hasAction(const Name& name) const;
    Result<Action> action(const Name& name, const json& data,
                          const ActionOptions& options = {}) const;
    Result<std::vector<Action>> actions(const std::vector<ActionsArgs>& actions,
                                        const ActionOptions& options = {}) const;

    // Execute a read-only action and decode its action_results return value.
    Result<json> readonly(const Name& name, const json& data = json::object()) const;

    Result<std::string> ricardian(const Name& name) const;
};

}  // namespace dwarfkit
