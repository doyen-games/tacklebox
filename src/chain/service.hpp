// Per-network chain access: one shared curl transport, an APIClient and
// ABICache per network, plus the small caches the UI reads (raw-ABI hashes,
// account snapshots). All calls here block; run them on the TaskRunner.
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <dwarfkit/abicache.hpp>
#include <dwarfkit/antelope.hpp>
#include <dwarfkit/common/chains.hpp>
#include <dwarfkit/transport/fetch_provider.hpp>

#include "vault/vault.hpp"

namespace tb {

struct ContractHashes {
    std::string codeHash;  // hex
    std::string abiHash;   // hex
    int64_t fetchedAt = 0;
};

struct ResourceUsage {
    int64_t used = 0;
    int64_t max = 0;  // -1 = unlimited
    double pct() const {
        return max > 0 ? static_cast<double>(used) / static_cast<double>(max) : 0.0;
    }
};

struct BalanceView {
    std::string contract;  // token contract ("eosio.token" for core)
    std::string quantity;  // "1.0000 EOS"
};

struct AccountSnapshot {
    std::string actor;
    json raw;  // full get_account response
    std::vector<BalanceView> balances;  // core first, then registered tokens
    ResourceUsage cpuUs;
    ResourceUsage netBytes;
    ResourceUsage ramBytes;
    int64_t fetchedAt = 0;

    std::string coreBalance() const {
        return balances.empty() ? std::string() : balances[0].quantity;
    }
};

// One registered vote proxy, enriched with its live voters-table standing.
struct ProxyInfo {
    std::string account;
    std::string name;       // registry display name
    std::string slogan;
    std::string website;
    double weight = 0.0;    // proxied_vote_weight (chain-native decay units)
    double coreTokens = 0.0;  // weight converted back to core tokens
    int votingFor = 0;      // producers on the proxy's own vote
    bool active = false;    // voter row still has is_proxy set
};

// RAM Bancor market snapshot.
struct RamMarket {
    std::string pricePerKb;   // "0.0134 WAX" (before the 0.5% fee)
    int64_t baseBytes = 0;    // connector base balance (bytes for sale)
    std::string quoteBalance; // connector quote balance
    int64_t fetchedAt = 0;
};

// PowerUp rental quote for a CPU ms / NET kb ask.
struct PowerUpQuote {
    int64_t cpuFrac = 0;
    int64_t netFrac = 0;
    double costCore = 0.0;  // estimated core tokens
    std::string error;      // non-empty when the chain has no powerup
};

struct EndpointHealth {
    NodeType type = NodeType::Rpc;
    std::string url;
    std::string nickname;
    bool ok = false;
    bool chainIdMatches = false;  // RPC only; others report reachability
    int latencyMs = -1;
    std::string headBlockTime;
    std::string error;
};

class ChainService {
public:
    explicit ChainService(const NetworkDef& net);

    const NetworkDef& net() const { return net_; }
    void setNetwork(const NetworkDef& net);  // endpoint switch etc.

    dwarfkit::ChainDefinition chainDef() const;
    // Primary RPC client (sessions, ABI cache). Per-request selection uses
    // pickUrl + the pool instead.
    std::shared_ptr<dwarfkit::APIClient> client() const;
    std::shared_ptr<dwarfkit::ABICache> abiCache() const { return abiCache_; }
    std::shared_ptr<dwarfkit::FetchProvider> fetch() const { return fetch_; }

    // --- endpoint selection --------------------------------------------------
    // Pick a node: round-robin across the enabled pool when the toggle is
    // on, else always the top-priority node. "" when the pool is empty.
    std::string pickUrl(NodeType type);
    // Queries recorded for this type inside the display window.
    static constexpr int kQueryWindowSec = 60;
    int recentQueries(NodeType type);
    // Probe one node of a given type (health panel).
    EndpointHealth probe(NodeType type, const std::string& url);

    // --- blocking calls (worker thread) ------------------------------------
    Result<AccountSnapshot> fetchAccount(const std::string& actor);
    Result<dwarfkit::ABI> fetchAbi(const std::string& contract);
    // Fresh code/abi hashes. maxAgeSec=0 forces a network round trip; the
    // guard uses 0 so pins are always checked against live state.
    Result<ContractHashes> fetchContractHashes(const std::string& contract, int maxAgeSec);
    Result<json> fetchTableRows(const json& params);
    // Whether an account exists (for transfer recipient validation).
    Result<bool> accountExists(const std::string& actor);
    // Fresh liquid balance of one token, e.g. "12.3456 WAX" (autopilot
    // percent amounts). Missing row = error.
    Result<std::string> fetchBalance(const std::string& contract, const std::string& actor,
                                     const std::string& code);
    // Accounts controlled by any of `publicKeys` (get_accounts_by_authorizers):
    // rows of {account_name, permission_name, authorizing_key}. Anchor-import
    // discovery.
    Result<json> discoverByKeys(const std::vector<std::string>& publicKeys);

    // --- resources / governance ---------------------------------------------
    Result<RamMarket> fetchRamMarket();
    Result<PowerUpQuote> quotePowerUp(double cpuMs, double netKb);
    // Raw get_producers rows (sorted by vote weight by the node).
    Result<json> fetchProducers(int limit);
    // Registered vote proxies from the on-chain regproxyinfo registry
    // (names/slogans only - no weights yet). The controller fans out one
    // fetchProxyStanding per row across the worker pool.
    Result<std::vector<ProxyInfo>> fetchProxyRegistry(size_t maxProxies);
    // One proxy's voters-table standing: proxied weight (also converted to
    // core tokens), producers voted for, and whether is_proxy still holds.
    Result<ProxyInfo> fetchProxyStanding(const std::string& account);
    // Outgoing CPU/NET delegations: eosio delband rows scoped to the actor.
    Result<json> fetchDelegations(const std::string& actor);

    // --- explorer calls -----------------------------------------------------
    Result<json> fetchInfo();                       // raw get_info
    Result<json> fetchBlock(const json& numOrId);   // raw get_block
    Result<json> fetchTxStatus(const std::string& id);
    // v1/history; many public nodes disable these - callers degrade gracefully.
    Result<json> fetchActions(const std::string& actor, int32_t pos, int32_t offset);
    Result<json> fetchTransaction(const std::string& id, std::optional<uint32_t> blockHint);

    // --- Atomic Assets API ---------------------------------------------------
    bool hasAtomicApi() const { return !net_.atomic.empty(); }
    Result<json> fetchAtomicAssets(const json& options);   // /atomicassets/v1/assets
    Result<json> fetchAtomicAsset(uint64_t assetId);

    // --- Light API (all-token balances in one call) --------------------------
    bool hasLightApi() const { return !net_.light.empty() && !net_.lightSlug.empty(); }
    Result<std::vector<BalanceView>> fetchAllBalances(const std::string& actor);

    // Raw GET of an arbitrary https URL (NFT media). Size-capped.
    Result<std::vector<uint8_t>> fetchUrl(const std::string& url, size_t maxBytes);

    // --- price oracle (display-only) -----------------------------------------
    // USD prices per the given oracle config (passed in so Settings can test
    // unsaved edits). Keys are priceKey(contract, SYM); Alcor prices every
    // listed token, CoinGecko/Delphi only the core symbol.
    Result<std::map<std::string, double>> fetchPrices(const OracleConfig& cfg);

private:
    // GET an https URL and parse the body as JSON (oracle endpoints).
    Result<json> getUrlJson(const std::string& url);
    // Pooled client per base URL (round-robin picks vary per request).
    std::shared_ptr<dwarfkit::APIClient> clientFor(const std::string& url);
    // POST an RPC call to a policy-picked node, failing over across the
    // enabled pool on transport errors.
    Result<json> rpcCall(const std::string& path, const json& params);
    // GET <picked base><path> for the REST-style node types, with the same
    // failover behavior.
    Result<json> getJson(NodeType type, const std::string& path);
    // Enabled URLs in the order the policy wants them tried this request:
    // preferred pick first, then the remaining fallbacks. Nodes inside their
    // failure cooldown sort last instead of being dropped, so a fully-dark
    // pool still gets retried rather than erroring out instantly.
    std::vector<std::string> candidateUrls(NodeType type);
    // Record a transport-level failure: the node is deprioritized for
    // kEndpointCooldownSec so live traffic stops hammering a dead endpoint.
    void markEndpointFailed(const std::string& url);
    bool inCooldown(const std::string& url, int64_t now) const;

    mutable std::mutex mutex_;
    NetworkDef net_;
    std::shared_ptr<dwarfkit::FetchProvider> fetch_;
    mutable std::map<std::string, std::shared_ptr<dwarfkit::APIClient>> pool_;
    std::shared_ptr<dwarfkit::ABICache> abiCache_;
    std::map<std::string, ContractHashes> hashCache_;
    // Selection runtime: rotation counters + query timestamps per node type.
    size_t rrCounter_[4] = {0, 0, 0, 0};
    std::deque<int64_t> queryTimes_[4];
    std::map<std::string, int64_t> failedUntil_;  // url -> cooldown expiry
};

}  // namespace tb
