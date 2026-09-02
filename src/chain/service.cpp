#include "chain/service.hpp"

#include <chrono>

#include <dwarfkit/atomicassets/endpoints.hpp>
#include <dwarfkit/resources.hpp>
#include <dwarfkit/transport/curl_fetch_provider.hpp>

#include "chain/prices.hpp"
#include "core/log.hpp"
#include "core/util.hpp"

namespace tb {

using dwarfkit::ErrorKind;

ChainService::ChainService(const NetworkDef& net)
    : net_(net),
      fetch_(std::make_shared<dwarfkit::CurlFetchProvider>(std::chrono::seconds(15))) {
    abiCache_ = std::make_shared<dwarfkit::ABICache>(client());
}

void ChainService::setNetwork(const NetworkDef& net) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool endpointChanged = net.activeEndpoint() != net_.activeEndpoint();
    net_ = net;
    if (endpointChanged) hashCache_.clear();
    // Stale pool entries are harmless (keyed by URL); no need to drop them.
}

dwarfkit::ChainDefinition ChainService::chainDef() const {
    std::lock_guard<std::mutex> lock(mutex_);
    dwarfkit::ChainDefinition::Args args;
    auto id = dwarfkit::Checksum256::from(net_.chainId);
    if (id) args.id = *id;
    args.url = net_.activeEndpoint();
    return dwarfkit::ChainDefinition::from(std::move(args));
}

std::shared_ptr<dwarfkit::APIClient> ChainService::clientFor(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pool_.find(url);
    if (it != pool_.end()) return it->second;
    dwarfkit::APIClientOptions options;
    options.url = url;
    options.fetch = fetch_;
    auto client = std::make_shared<dwarfkit::APIClient>(std::move(options));
    pool_[url] = client;
    return client;
}

std::shared_ptr<dwarfkit::APIClient> ChainService::client() const {
    auto* self = const_cast<ChainService*>(this);
    std::string url;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        url = net_.activeEndpoint();
    }
    return self->clientFor(url);
}

std::string ChainService::pickUrl(NodeType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    const EndpointList& list = net_.list(type);
    auto enabled = list.enabledSorted();
    if (enabled.empty()) return {};

    // Record this query into the sliding load window (display only).
    int slot = static_cast<int>(type);
    int64_t now = nowSec();
    auto& window = queryTimes_[slot];
    window.push_back(now);
    while (!window.empty() && now - window.front() > kQueryWindowSec)
        window.pop_front();

    // Round robin (a plain toggle) spreads queries across the whole pool.
    // Single-target interactions (broadcasting through client()) always go
    // through the top-priority node instead of this picker.
    if (!list.roundRobin || enabled.size() == 1) return enabled.front()->url;
    return enabled[rrCounter_[slot]++ % enabled.size()]->url;
}

int ChainService::recentQueries(NodeType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    int slot = static_cast<int>(type);
    int64_t now = nowSec();
    auto& window = queryTimes_[slot];
    while (!window.empty() && now - window.front() > kQueryWindowSec) window.pop_front();
    return static_cast<int>(window.size());
}


bool ChainService::inCooldown(const std::string& url, int64_t now) const {
    auto it = failedUntil_.find(url);
    return it != failedUntil_.end() && it->second > now;
}

void ChainService::markEndpointFailed(const std::string& url) {
    constexpr int kEndpointCooldownSec = 60;
    std::lock_guard<std::mutex> lock(mutex_);
    failedUntil_[url] = nowSec() + kEndpointCooldownSec;
}

std::vector<std::string> ChainService::candidateUrls(NodeType type) {
    std::string preferred = pickUrl(type);  // records the query + applies policy
    if (preferred.empty()) return {};
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t now = nowSec();
    std::vector<std::string> healthy{preferred}, coolingDown;
    for (const Endpoint* node : net_.list(type).enabledSorted()) {
        if (node->url == preferred) continue;
        (inCooldown(node->url, now) ? coolingDown : healthy).push_back(node->url);
    }
    // The preferred pick goes first even when cooling down (it may be the
    // only node); every other cooling node is tried last, not never.
    healthy.insert(healthy.end(), coolingDown.begin(), coolingDown.end());
    return healthy;
}

Result<json> ChainService::rpcCall(const std::string& path, const json& params) {
    auto candidates = candidateUrls(NodeType::Rpc);
    if (candidates.empty())
        return dwarfkit::err(ErrorKind::Transport, "no enabled RPC endpoint for " + net_.name);
    Result<json> last = dwarfkit::err(ErrorKind::Transport, "unreachable");
    for (const auto& url : candidates) {
        last = clientFor(url)->call({.path = path, .params = params});
        // Only transport-level failures justify moving to another node;
        // chain-level errors (asserts, missing rows) would repeat anywhere.
        if (last || last.error().kind != ErrorKind::Transport) return last;
        markEndpointFailed(url);
        Log::info("rpc failover: %s unreachable, trying next node", url.c_str());
    }
    return last;
}

Result<json> ChainService::getJson(NodeType type, const std::string& path) {
    auto candidates = candidateUrls(type);
    if (candidates.empty())
        return dwarfkit::err(ErrorKind::Unsupported,
                             std::string("no enabled ") + nodeTypeName(type) +
                                 " endpoint for " + net_.name);
    Result<json> last = dwarfkit::err(ErrorKind::Transport, "unreachable");
    for (std::string base : candidates) {
        while (!base.empty() && base.back() == '/') base.pop_back();
        dwarfkit::FetchRequest request;
        request.url = base + path;
        request.method = "GET";
        auto response = fetch_->fetch(request);
        if (!response) {
            // No HTTP answer at all: a dead node - cool it down and fail over.
            last = dwarfkit::err(response.error());
            markEndpointFailed(base);
            Log::info("%s failover: %s unreachable, trying next node", nodeTypeName(type),
                      base.c_str());
            continue;
        }
        if (response->status != 200)
            return dwarfkit::err(ErrorKind::Api,
                                 std::string(nodeTypeName(type)) + " answered HTTP " +
                                     std::to_string(response->status),
                                 response->status);
        json parsed = json::parse(response->body, nullptr, false);
        if (parsed.is_discarded())
            return dwarfkit::err(ErrorKind::Api, std::string(nodeTypeName(type)) +
                                                     " returned malformed JSON");
        return parsed;
    }
    return last;
}

Result<AccountSnapshot> ChainService::fetchAccount(const std::string& actor) {
    // Raw JSON keeps chain-specific account extensions (WAX/Telos voter info)
    // from tripping typed decoding; the UI only reads a stable subset.
    auto account = rpcCall("/v1/chain/get_account", json{{"account_name", actor}});
    if (!account) return dwarfkit::err(account.error());

    AccountSnapshot snap;
    snap.actor = actor;
    snap.raw = *account;
    snap.fetchedAt = nowSec();

    const json& a = snap.raw;
    auto readLimit = [](const json& node, ResourceUsage& out) {
        if (!node.is_object()) return;
        // nodeos emits numbers, or strings once values exceed 32 bits.
        auto grab = [&](const char* key) -> int64_t {
            if (!node.contains(key)) return 0;
            const json& v = node[key];
            if (v.is_number()) return v.get<int64_t>();
            if (v.is_string()) {
                try {
                    return std::stoll(v.get<std::string>());
                } catch (...) {
                    return 0;
                }
            }
            return 0;
        };
        out.used = grab("used");
        out.max = grab("max");
    };
    if (a.contains("cpu_limit")) readLimit(a["cpu_limit"], snap.cpuUs);
    if (a.contains("net_limit")) readLimit(a["net_limit"], snap.netBytes);
    if (a.contains("ram_usage") && a["ram_usage"].is_number())
        snap.ramBytes.used = a["ram_usage"].get<int64_t>();
    if (a.contains("ram_quota") && a["ram_quota"].is_number())
        snap.ramBytes.max = a["ram_quota"].get<int64_t>();

    // Core balance via eosio.token; tolerate empty (fresh accounts).
    auto balances = rpcCall("/v1/chain/get_currency_balance",
                            json{{"code", "eosio.token"},
                                 {"account", actor},
                                 {"symbol", net_.coreSymbolCode()}});
    if (balances && balances->is_array())
        for (const auto& b : *balances)
            if (b.is_string()) snap.balances.push_back({"eosio.token", b.get<std::string>()});
    if (snap.balances.empty()) {
        // core_liquid_balance from get_account as a fallback.
        if (a.contains("core_liquid_balance") && a["core_liquid_balance"].is_string())
            snap.balances.push_back(
                {"eosio.token", a["core_liquid_balance"].get<std::string>()});
        else
            snap.balances.push_back({"eosio.token", "0 " + net_.coreSymbolCode()});
    }

    // Every token in one shot when a light API node is configured; the
    // manual registry stays as the fallback path.
    if (hasLightApi()) {
        if (auto all = fetchAllBalances(actor); all && !all->empty()) {
            snap.balances = *all;
            return snap;
        }
    }

    // Registered non-core tokens.
    std::vector<TokenDef> tokens;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tokens = net_.tokens;
    }
    for (const auto& token : tokens) {
        auto extra = rpcCall("/v1/chain/get_currency_balance",
                             json{{"code", token.contract},
                                  {"account", actor},
                                  {"symbol", token.code}});
        if (extra && extra->is_array() && !extra->empty() && (*extra)[0].is_string())
            snap.balances.push_back({token.contract, (*extra)[0].get<std::string>()});
        else
            snap.balances.push_back({token.contract, "0 " + token.code});
    }
    return snap;
}

// --- resources / governance ---------------------------------------------------

Result<RamMarket> ChainService::fetchRamMarket() {
    dwarfkit::ResourcesOptions options;
    options.api = client();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        options.symbol = net_.coreSymbol;
    }
    auto resources = dwarfkit::Resources::make(options);
    if (!resources) return dwarfkit::err(resources.error());
    auto ram = resources->v1().ram.get_state();
    if (!ram) return dwarfkit::err(ram.error());

    RamMarket market;
    auto priced = ram->price_per_kb(1.0);
    if (priced) market.pricePerKb = priced->toString();
    market.baseBytes = ram->base.balance.units;
    market.quoteBalance = ram->quote.balance.toString();
    market.fetchedAt = nowSec();
    return market;
}

Result<PowerUpQuote> ChainService::quotePowerUp(double cpuMs, double netKb) {
    dwarfkit::ResourcesOptions options;
    options.api = client();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        options.symbol = net_.coreSymbol;
    }
    auto resources = dwarfkit::Resources::make(options);
    if (!resources) return dwarfkit::err(resources.error());
    auto usage = resources->getSampledUsage();
    if (!usage)
        return dwarfkit::err(dwarfkit::ErrorKind::Unsupported,
                             "could not sample usage (chain may not run PowerUp): " +
                                 usage.error().message);
    auto view = resources->v1();
    auto state = view.powerup.get_state();
    if (!state)
        return dwarfkit::err(dwarfkit::ErrorKind::Unsupported,
                             "this chain does not expose PowerUp state");

    PowerUpQuote quote;
    auto cpuFrac = state->cpu.frac_by_ms(*usage, cpuMs);
    auto netFrac = state->net.frac_by_kb(*usage, netKb);
    if (!cpuFrac || !netFrac)
        return dwarfkit::err(dwarfkit::ErrorKind::Internal, "PowerUp frac math failed");
    quote.cpuFrac = *cpuFrac;
    quote.netFrac = *netFrac;
    auto cpuPrice = state->cpu.price_per_ms(*usage, cpuMs);
    auto netPrice = state->net.price_per_kb(*usage, netKb);
    quote.costCore = (cpuPrice ? *cpuPrice : 0.0) + (netPrice ? *netPrice : 0.0);
    return quote;
}

Result<json> ChainService::fetchProducers(int limit) {
    return rpcCall("/v1/chain/get_producers", json{{"json", true}, {"limit", limit}});
}

Result<std::vector<ProxyInfo>> ChainService::fetchProxies(size_t maxProxies) {
    // The community proxy registry (regproxyinfo) is itself an on-chain
    // table, so this stays inside the chain-endpoint policy.
    DK_TRY(reg, fetchTableRows(json{{"code", "regproxyinfo"},
                                    {"scope", "regproxyinfo"},
                                    {"table", "proxies"},
                                    {"limit", 200},
                                    {"json", true}}));
    std::vector<ProxyInfo> proxies;
    if (reg.contains("rows") && reg["rows"].is_array())
        for (const auto& row : reg["rows"]) {
            ProxyInfo p;
            p.account = row.value("owner", std::string());
            p.name = row.value("name", std::string());
            p.slogan = row.value("slogan", std::string());
            p.website = row.value("website", std::string());
            if (!p.account.empty()) proxies.push_back(std::move(p));
            if (proxies.size() >= maxProxies) break;
        }
    if (proxies.empty())
        return dwarfkit::err(dwarfkit::ErrorKind::NotFound,
                             "this chain has no regproxyinfo registry; enter a proxy "
                             "account by hand");
    // Rank by live proxied vote weight from the system voters table. A miss
    // just leaves that proxy unranked - the registry row still shows.
    for (auto& p : proxies) {
        auto voter = fetchTableRows(json{{"code", "eosio"},
                                         {"scope", "eosio"},
                                         {"table", "voters"},
                                         {"lower_bound", p.account},
                                         {"limit", 1},
                                         {"json", true}});
        if (!voter || !voter->contains("rows") || !(*voter)["rows"].is_array() ||
            (*voter)["rows"].empty())
            continue;
        const json& row = (*voter)["rows"][0];
        if (row.value("owner", std::string()) != p.account) continue;
        p.active = row.value("is_proxy", 0) != 0;
        if (row.contains("proxied_vote_weight")) {
            const json& w = row["proxied_vote_weight"];
            p.weight = w.is_number() ? w.get<double>()
                       : w.is_string() ? std::atof(w.get<std::string>().c_str())
                                       : 0.0;
        }
    }
    std::stable_sort(proxies.begin(), proxies.end(),
                     [](const ProxyInfo& a, const ProxyInfo& b) {
                         return a.weight > b.weight;
                     });
    return proxies;
}

Result<json> ChainService::fetchDelegations(const std::string& actor) {
    return fetchTableRows(json{{"code", "eosio"},
                               {"scope", actor},
                               {"table", "delband"},
                               {"limit", 200},
                               {"json", true}});
}

Result<dwarfkit::ABI> ChainService::fetchAbi(const std::string& contract) {
    return abiCache_->getAbi(dwarfkit::Name::from(contract));
}

Result<ContractHashes> ChainService::fetchContractHashes(const std::string& contract,
                                                         int maxAgeSec) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = hashCache_.find(contract);
        if (it != hashCache_.end() && maxAgeSec > 0 &&
            nowSec() - it->second.fetchedAt <= maxAgeSec)
            return it->second;
    }
    std::string pickedUrl = pickUrl(NodeType::Rpc);
    auto raw = clientFor(pickedUrl.empty() ? net_.activeEndpoint() : pickedUrl)
                   ->v1.chain.get_raw_abi(dwarfkit::Name::from(contract));
    if (!raw) return dwarfkit::err(raw.error());
    ContractHashes hashes;
    hashes.codeHash = raw->code_hash.hexString();
    hashes.abiHash = raw->abi_hash.hexString();
    hashes.fetchedAt = nowSec();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hashCache_[contract] = hashes;
    }
    return hashes;
}

Result<json> ChainService::fetchTableRows(const json& params) {
    return rpcCall("/v1/chain/get_table_rows", params);
}

EndpointHealth ChainService::probe(NodeType type, const std::string& url) {
    EndpointHealth health;
    health.type = type;
    health.url = url;

    auto start = std::chrono::steady_clock::now();
    auto finish = [&] {
        health.latencyMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start)
                .count());
    };

    if (type == NodeType::Rpc) {
        dwarfkit::APIClientOptions options;
        options.url = url;
        options.fetch = fetch_;
        dwarfkit::APIClient probeClient(std::move(options));
        auto info =
            probeClient.call({.path = "/v1/chain/get_info", .params = json::object()});
        finish();
        if (!info) {
            health.error = info.error().message;
            return health;
        }
        health.ok = true;
        std::string chainId = info->value("chain_id", "");
        health.chainIdMatches = (chainId == net_.chainId);
        health.headBlockTime = info->value("head_block_time", "");
        if (!health.chainIdMatches)
            health.error =
                "endpoint serves a different chain (" + middleEllipsis(chainId, 8, 6) + ")";
        return health;
    }

    // REST node types: a cheap GET that proves the service is what it claims.
    std::string base = url;
    while (!base.empty() && base.back() == '/') base.pop_back();
    const char* path = type == NodeType::Hyperion ? "/v2/health"
                       : type == NodeType::Atomic ? "/health"
                                                  : "/api/networks";
    dwarfkit::FetchRequest request;
    request.url = base + path;
    request.method = "GET";
    auto response = fetch_->fetch(request);
    finish();
    if (!response) {
        health.error = response.error().message;
        return health;
    }
    // Health endpoints may report degraded states with non-200 but valid JSON;
    // reachability = an HTTP answer with parseable JSON.
    json parsed = json::parse(response->body, nullptr, false);
    health.ok = response->status == 200 && !parsed.is_discarded();
    health.chainIdMatches = health.ok;  // no id check for REST services
    if (!health.ok)
        health.error = "HTTP " + std::to_string(response->status);
    return health;
}

Result<bool> ChainService::accountExists(const std::string& actor) {
    auto res = rpcCall("/v1/chain/get_account", json{{"account_name", actor}});
    if (res) return true;
    // nodeos answers 500 with "unknown key" internals for missing accounts.
    const auto& e = res.error();
    if (e.kind == ErrorKind::Api) return false;
    return dwarfkit::err(e);  // transport-level failure: caller should not conclude
}

Result<std::string> ChainService::fetchBalance(const std::string& contract,
                                               const std::string& actor,
                                               const std::string& code) {
    auto balances = rpcCall("/v1/chain/get_currency_balance",
                            json{{"code", contract}, {"account", actor}, {"symbol", code}});
    if (!balances) return dwarfkit::err(balances.error());
    if (!balances->is_array() || balances->empty() || !(*balances)[0].is_string())
        return dwarfkit::err(ErrorKind::NotFound,
                             actor + " holds no " + code + " on " + contract);
    return (*balances)[0].get<std::string>();
}

Result<json> ChainService::discoverByKeys(const std::vector<std::string>& publicKeys) {
    if (publicKeys.empty()) return json{{"accounts", json::array()}};
    json keys = json::array();
    for (const auto& key : publicKeys) keys.push_back(key);
    return rpcCall("/v1/chain/get_accounts_by_authorizers",
                   json{{"accounts", json::array()}, {"keys", keys}});
}

// --- explorer ---------------------------------------------------------------

Result<json> ChainService::fetchInfo() {
    return rpcCall("/v1/chain/get_info", json::object());
}

Result<json> ChainService::fetchBlock(const json& numOrId) {
    return rpcCall("/v1/chain/get_block", json{{"block_num_or_id", numOrId}});
}

Result<json> ChainService::fetchTxStatus(const std::string& id) {
    return rpcCall("/v1/chain/get_transaction_status", json{{"id", id}});
}

Result<json> ChainService::fetchActions(const std::string& actor, int32_t pos, int32_t offset) {
    // Hyperion first (most public nodes dropped v1 history), then legacy v1.
    if (!net_.hyperion.empty()) {
        int limit = offset < 0 ? -offset : offset;
        if (limit <= 0 || limit > 100) limit = 30;
        auto v2 = getJson(NodeType::Hyperion, "/v2/history/get_actions?account=" + actor +
                                                  "&limit=" + std::to_string(limit) +
                                                  "&sort=desc");
        if (v2 && v2->contains("actions")) return v2;
    }
    return rpcCall("/v1/history/get_actions",
                   json{{"account_name", actor}, {"pos", pos}, {"offset", offset}});
}

Result<json> ChainService::fetchTransaction(const std::string& id,
                                            std::optional<uint32_t> blockHint) {
    if (!net_.hyperion.empty()) {
        auto v2 = getJson(NodeType::Hyperion, "/v2/history/get_transaction?id=" + id);
        if (v2 && (v2->contains("actions") || v2->contains("trx_id"))) return v2;
    }
    json params{{"id", id}};
    if (blockHint) params["block_num_hint"] = *blockHint;
    return rpcCall("/v1/history/get_transaction", params);
}

// --- Atomic Assets ----------------------------------------------------------

Result<json> ChainService::fetchAtomicAssets(const json& options) {
    std::string url = pickUrl(NodeType::Atomic);
    if (url.empty())
        return dwarfkit::err(ErrorKind::Unsupported,
                             "no Atomic Assets API endpoint configured for this network");
    dwarfkit::atomic::AtomicAssetsAPIClient aa(clientFor(url));
    return aa.atomicassets.v1.get_assets(options);
}

Result<json> ChainService::fetchAtomicAsset(uint64_t assetId) {
    std::string url = pickUrl(NodeType::Atomic);
    if (url.empty())
        return dwarfkit::err(ErrorKind::Unsupported,
                             "no Atomic Assets API endpoint configured for this network");
    dwarfkit::atomic::AtomicAssetsAPIClient aa(clientFor(url));
    return aa.atomicassets.v1.get_asset(assetId);
}

// --- Light API ---------------------------------------------------------------

Result<std::vector<BalanceView>> ChainService::fetchAllBalances(const std::string& actor) {
    std::string slug;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        slug = net_.lightSlug;
    }
    if (slug.empty())
        return dwarfkit::err(ErrorKind::Unsupported,
                             "no light API network slug configured for this network");
    auto data = getJson(NodeType::Light, "/api/balances/" + slug + "/" + actor);
    if (!data) return dwarfkit::err(data.error());

    std::vector<BalanceView> out;
    std::string coreCode;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        coreCode = net_.coreSymbolCode();
    }
    for (const auto& row : data->value("balances", json::array())) {
        std::string contract = row.value("contract", "");
        std::string currency = row.value("currency", "");
        std::string amount = row.value("amount", "");
        if (contract.empty() || currency.empty() || amount.empty()) continue;
        BalanceView view{contract, amount + " " + currency};
        // Core token leads the list, matching the RPC path's convention.
        if (contract == "eosio.token" && currency == coreCode)
            out.insert(out.begin(), view);
        else
            out.push_back(view);
    }
    if (out.empty())
        return dwarfkit::err(ErrorKind::NotFound, "light API returned no balances");
    return out;
}

Result<json> ChainService::getUrlJson(const std::string& url) {
    if (url.rfind("https://", 0) != 0)
        return dwarfkit::err(ErrorKind::Invalid, "oracle endpoints must be https");
    dwarfkit::FetchRequest request;
    request.url = url;
    request.method = "GET";
    auto response = fetch_->fetch(request);
    if (!response) return dwarfkit::err(response.error());
    if (response->status != 200)
        return dwarfkit::err(ErrorKind::Api, "oracle returned HTTP " +
                                                 std::to_string(response->status));
    json parsed = json::parse(response->body, nullptr, false);
    if (parsed.is_discarded())
        return dwarfkit::err(ErrorKind::Invalid, "oracle returned invalid JSON");
    return parsed;
}

Result<std::map<std::string, double>> ChainService::fetchPrices(const OracleConfig& cfg) {
    std::map<std::string, double> prices;
    const std::string coreKey = priceKey("eosio.token", net_.coreSymbolCode());
    std::string base = cfg.url;
    while (!base.empty() && base.back() == '/') base.pop_back();

    switch (static_cast<OracleProvider>(cfg.provider)) {
        case OracleProvider::Off:
            return prices;
        case OracleProvider::Alcor: {
            DK_TRY(body, getUrlJson(base + "/api/v2/tokens"));
            prices = parseAlcorTokens(body);
            if (prices.empty())
                return dwarfkit::err(ErrorKind::Api, "no prices in the Alcor response");
            return prices;
        }
        case OracleProvider::CoinGecko: {
            if (cfg.coreId.empty())
                return dwarfkit::err(ErrorKind::Invalid, "set the CoinGecko id first");
            DK_TRY(body, getUrlJson(base + "/api/v3/simple/price?ids=" + cfg.coreId +
                                    "&vs_currencies=usd"));
            auto usd = parseCoinGecko(body, cfg.coreId);
            if (!usd)
                return dwarfkit::err(ErrorKind::Api,
                                     "CoinGecko has no usd price for '" + cfg.coreId + "'");
            prices[coreKey] = *usd;
            return prices;
        }
        case OracleProvider::Delphi: {
            if (cfg.coreId.empty())
                return dwarfkit::err(ErrorKind::Invalid, "set the Delphi pair first");
            DK_TRY(pairs, rpcCall("/v1/chain/get_table_rows",
                                  json{{"code", "delphioracle"},
                                       {"scope", "delphioracle"},
                                       {"table", "pairs"},
                                       {"json", true},
                                       {"limit", 100}}));
            int precision = -1;
            for (const json& row : pairs.value("rows", json::array()))
                if (row.value("name", "") == cfg.coreId)
                    precision = row.value("quoted_precision", -1);
            if (precision < 0)
                return dwarfkit::err(ErrorKind::Api,
                                     "delphioracle has no pair '" + cfg.coreId + "'");
            DK_TRY(points, rpcCall("/v1/chain/get_table_rows",
                                   json{{"code", "delphioracle"},
                                        {"scope", cfg.coreId},
                                        {"table", "datapoints"},
                                        {"json", true},
                                        {"limit", 30}}));
            auto usd = parseDelphiDatapoints(points.value("rows", json::array()), precision);
            if (!usd)
                return dwarfkit::err(ErrorKind::Api,
                                     "no datapoints for '" + cfg.coreId + "'");
            prices[coreKey] = *usd;
            return prices;
        }
    }
    return prices;
}

Result<std::vector<uint8_t>> ChainService::fetchUrl(const std::string& url, size_t maxBytes) {
    if (url.rfind("https://", 0) != 0)
        return dwarfkit::err(ErrorKind::Invalid, "media fetch requires https");
    dwarfkit::FetchRequest request;
    request.url = url;
    request.method = "GET";
    auto response = fetch_->fetch(request);
    if (!response) return dwarfkit::err(response.error());
    if (response->status != 200)
        return dwarfkit::err(ErrorKind::Api, "media fetch failed", response->status);
    if (response->body.size() > maxBytes)
        return dwarfkit::err(ErrorKind::Invalid, "media exceeds the size cap");
    const auto* data = reinterpret_cast<const uint8_t*>(response->body.data());
    return std::vector<uint8_t>(data, data + response->body.size());
}

}  // namespace tb
