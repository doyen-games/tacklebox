#include <dwarfkit/contract/kit.hpp>

namespace dwarfkit {

ContractKit::ContractKit(const ContractKitArgs& args, const ContractKitOptions& options)
    : client(args.client), debug(options.debug) {
    // Use either the specified cache or create one
    if (options.abiCache) {
        abiCache = options.abiCache;
    } else {
        abiCache = std::make_shared<ABICache>(client);
    }
    // If any ABIs are provided during construction, inject them into the cache
    for (const auto& def : options.abis) {
        abiCache->setAbi(def.name, def.abi);
    }
}

Result<Contract> ContractKit::load(const Name& contract) const {
    DK_TRY(abi, abiCache->getAbi(contract));
    return Contract({.abi = abi, .account = contract, .client = client}, {.debug = debug});
}

}  // namespace dwarfkit
