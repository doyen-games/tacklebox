#include "chain/netreg.hpp"

#include <dwarfkit/common/chains.hpp>

#include "core/util.hpp"

namespace tb {

namespace {

// Extra public nodes beyond the catalog default, per node type. Keyed by
// chain id: the catalog's display names shift (the EOS entry renders as
// "Vaulta" since the rebrand), ids do not. All best-effort defaults - health
// probes and the endpoints editor are the source of truth at runtime.
void seedExtraNodes(NetworkDef& net) {
    namespace Chains = dwarfkit::Chains;
    const std::string& id = net.chainId;
    auto add = [](EndpointList& list, const char* url, const char* nick) {
        list.nodes.push_back({url, nick, static_cast<int>(list.nodes.size()), true});
    };
    if (id == Chains::EOS().id.hexString()) {
        add(net.rpc, "https://eos.greymass.com", "Greymass");
        add(net.rpc, "https://mainnet.eosamsterdam.net", "EOS Amsterdam");
        add(net.hyperion, "https://eos.hyperion.eosrio.io", "EOS Rio");
        add(net.light, "https://lightapi.eosamsterdam.net", "EOS Amsterdam");
        net.lightSlug = "eos";
    } else if (id == Chains::Jungle4().id.hexString()) {
        add(net.rpc, "https://jungle4.greymass.com", "Greymass");
        add(net.rpc, "https://jungle4.api.eosnation.io", "EOS Nation");
        add(net.hyperion, "https://jungle.eosusa.io", "EOSUSA");
        net.lightSlug = "jungle4";
    } else if (id == Chains::WAX().id.hexString()) {
        add(net.rpc, "https://wax.greymass.com", "Greymass");
        add(net.rpc, "https://api.waxsweden.org", "WAX Sweden");
        add(net.rpc, "https://wax.eosphere.io", "EOSphere");
        add(net.atomic, "https://wax-atomic.alcor.exchange", "Alcor");
        add(net.hyperion, "https://wax.eosusa.io", "EOSUSA");
        add(net.light, "https://wax.light-api.net", "Light API");
        net.lightSlug = "wax";
    } else if (id == Chains::Telos().id.hexString()) {
        add(net.rpc, "https://telos.greymass.com", "Greymass");
        add(net.rpc, "https://mainnet.telos.net", "Telos Foundation");
        add(net.hyperion, "https://mainnet.telos.net", "Telos Foundation");
        net.lightSlug = "telos";
    }
    // Mainnets with a known Alcor deployment default to it (prices every
    // listed token); testnets and unknown chains stay off until the user
    // opts in.
    if (!net.testnet) {
        OracleConfig oracle = oracleDefaults(id, OracleProvider::Alcor);
        if (!oracle.url.empty()) net.oracle = oracle;
    }
}

NetworkDef fromCatalog(const dwarfkit::ChainDefinition& chain, bool testnet) {
    NetworkDef net;
    net.chainId = chain.id.hexString();
    net.name = chain.name();
    net.testnet = testnet;
    net.rpc.nodes.push_back({chain.url, "catalog", -1, true});
    seedExtraNodes(net);
    // The catalog URL often repeats in the seeds; keep the nicknamed copy.
    for (size_t i = 1; i < net.rpc.nodes.size(); ++i)
        if (net.rpc.nodes[i].url == chain.url) {
            net.rpc.nodes[i].priority = -1;
            net.rpc.nodes.erase(net.rpc.nodes.begin());
            break;
        }
    if (chain.systemToken) {
        const auto& sym = chain.systemToken->symbol;
        net.coreSymbol =
            std::to_string(sym.precision()) + "," + sym.code().toString();
    }
    if (chain.explorer) net.explorerTx = chain.explorer->prefix + "{txid}" + chain.explorer->suffix;
    return net;
}

// The catalog can alias one chain under several entries (EOS and Vaulta share
// a chain id since the rebrand). One chain = one preset; first entry wins, so
// list the mechanically-correct one first.
std::vector<NetworkDef> dedupeByChainId(std::vector<NetworkDef> nets) {
    std::vector<NetworkDef> out;
    std::set<std::string> seen;
    for (auto& net : nets)
        if (seen.insert(net.chainId).second) out.push_back(std::move(net));
    return out;
}

}  // namespace

std::vector<NetworkDef> defaultNetworks() {
    using namespace dwarfkit;
    return dedupeByChainId({
        fromCatalog(Chains::EOS(), false),
        fromCatalog(Chains::WAX(), false),
        fromCatalog(Chains::Telos(), false),
        fromCatalog(Chains::Jungle4(), true),
    });
}

std::vector<NetworkDef> allPresets() {
    using namespace dwarfkit;
    // EOS before Vaulta: same chain id, but the EOS entry carries the 4,EOS
    // system token that staking/RAM math runs on (Vaulta's "A" is a
    // core.vaulta token, trackable via the token registry). The surviving
    // preset still displays the catalog name, "Vaulta".
    return dedupeByChainId({
        fromCatalog(Chains::EOS(), false),
        fromCatalog(Chains::Vaulta(), false),
        fromCatalog(Chains::WAX(), false),
        fromCatalog(Chains::Telos(), false),
        fromCatalog(Chains::Libre(), false),
        fromCatalog(Chains::Proton(), false),
        fromCatalog(Chains::UX(), false),
        fromCatalog(Chains::FIO(), false),
        fromCatalog(Chains::Jungle4(), true),
        fromCatalog(Chains::TelosTestnet(), true),
        fromCatalog(Chains::WAXTestnet(), true),
        fromCatalog(Chains::LibreTestnet(), true),
        fromCatalog(Chains::ProtonTestnet(), true),
    });
}

OracleConfig oracleDefaults(const std::string& chainId, OracleProvider provider) {
    namespace Chains = dwarfkit::Chains;
    OracleConfig cfg;
    cfg.provider = static_cast<int>(provider);
    const bool eos = chainId == Chains::EOS().id.hexString();
    const bool wax = chainId == Chains::WAX().id.hexString();
    const bool telos = chainId == Chains::Telos().id.hexString();
    switch (provider) {
        case OracleProvider::Off:
            cfg.provider = 0;
            break;
        case OracleProvider::Alcor:
            if (eos) cfg.url = "https://eos.alcor.exchange";
            if (wax) cfg.url = "https://wax.alcor.exchange";
            if (telos) cfg.url = "https://telos.alcor.exchange";
            break;
        case OracleProvider::CoinGecko:
            cfg.url = "https://api.coingecko.com";
            if (eos) cfg.coreId = "eos";
            if (wax) cfg.coreId = "wax";
            if (telos) cfg.coreId = "telos";
            break;
        case OracleProvider::Delphi:
            // Reads delphioracle through the RPC pool; no external URL.
            if (eos) cfg.coreId = "eosusd";
            if (wax) cfg.coreId = "waxpusd";
            break;
    }
    return cfg;
}

bool endpointAllowed(const std::string& url, std::string* why) {
    std::string u = trim(url);
    if (u.find(' ') != std::string::npos) {
        if (why) *why = "URL contains whitespace";
        return false;
    }
    if (startsWith(u, "https://")) return true;
    // Plain http only for loopback development nodes.
    if (startsWith(u, "http://localhost") || startsWith(u, "http://127.0.0.1")) return true;
    if (why) *why = "endpoints must use https (plain http is allowed only for localhost)";
    return false;
}

}  // namespace tb
