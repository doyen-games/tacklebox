// Port of contract src/kit.ts.
#pragma once

#include <dwarfkit/abicache.hpp>
#include <dwarfkit/contract/contract.hpp>

namespace dwarfkit {

struct ContractKitArgs {
    std::shared_ptr<APIClient> client;
};

struct ABIDefinition {
    Name name;
    ABI abi;
};

struct ContractKitOptions {
    std::shared_ptr<ABICache> abiCache;
    std::vector<ABIDefinition> abis;
    bool debug = false;
};

class ContractKit {
public:
    explicit ContractKit(const ContractKitArgs& args, const ContractKitOptions& options = {});

    std::shared_ptr<ABICache> abiCache;
    std::shared_ptr<APIClient> client;
    bool debug = false;

    // Load a contract by name from an API endpoint.
    Result<Contract> load(const Name& contract) const;
};

}  // namespace dwarfkit
