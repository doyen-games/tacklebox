#include <dwarfkit/atomicassets/utility.hpp>

#include <dwarfkit/atomicassets/contracts/atomicassets.gen.hpp>
#include <dwarfkit/atomicassets/contracts/atomicmarket.gen.hpp>
#include <dwarfkit/atomicassets/contracts/atomictoolsx.gen.hpp>

namespace dwarfkit::atomic {

KitUtility::KitUtility(std::string urlIn, ChainDefinition chainIn, const KitOptions& options)
    : url(std::move(urlIn)), chain(std::move(chainIn)) {
    client = options.client
                 ? options.client
                 : std::make_shared<APIClient>(APIClientOptions{.url = chain.url});
    atomicClient = options.atomicClient
                       ? options.atomicClient
                       : std::make_shared<AtomicAssetsAPIClient>(
                             std::make_shared<APIClient>(APIClientOptions{.url = url}));
    assetsContract = options.assetsContract
                         ? *options.assetsContract
                         : Contract({.abi = gen::atomicassets::abi(),
                                     .account = Name::from("atomicassets"),
                                     .client = client});
    marketContract = options.marketContract
                         ? *options.marketContract
                         : Contract({.abi = gen::atomicmarket::abi(),
                                     .account = Name::from("atomicmarket"),
                                     .client = client});
    toolsContract = options.toolsContract
                        ? *options.toolsContract
                        : Contract({.abi = gen::atomictoolsx::abi(),
                                    .account = Name::from("atomictoolsx"),
                                    .client = client});
}

}  // namespace dwarfkit::atomic
