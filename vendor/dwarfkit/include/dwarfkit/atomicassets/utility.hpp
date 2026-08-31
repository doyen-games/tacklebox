// Port of atomicassets src/utility.ts.
#pragma once

#include <dwarfkit/atomicassets/endpoints.hpp>
#include <dwarfkit/common/chains.hpp>
#include <dwarfkit/contract/contract.hpp>

namespace dwarfkit::atomic {

struct KitOptions {
    std::optional<Contract> assetsContract;
    std::optional<Contract> marketContract;
    std::optional<Contract> toolsContract;
    std::shared_ptr<APIClient> client;
    std::shared_ptr<AtomicAssetsAPIClient> atomicClient;
};

class KitUtility {
public:
    KitUtility(std::string url, ChainDefinition chain, const KitOptions& options = {});

    std::string url;
    ChainDefinition chain;
    // chain API client (actions, ABIs)
    std::shared_ptr<APIClient> client;
    // eosio-contract-api client
    std::shared_ptr<AtomicAssetsAPIClient> atomicClient;
    Contract assetsContract;
    Contract marketContract;
    Contract toolsContract;
};

}  // namespace dwarfkit::atomic
