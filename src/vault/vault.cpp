#include "vault/vault.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>

#include <dwarfkit/antelope/chain/private_key.hpp>

#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/rng.hpp"
#include "core/util.hpp"
#include "vault/cipher.hpp"

namespace tb {

using dwarfkit::ErrorKind;

static constexpr const char* kMagic = "TACKLEBOX_VAULT";
static constexpr int kVersion = 1;
static constexpr size_t kAuditMax = 500;

const char* nodeTypeName(NodeType type) {
    switch (type) {
        case NodeType::Rpc: return "RPC";
        case NodeType::Atomic: return "Atomic";
        case NodeType::Hyperion: return "Hyperion";
        case NodeType::Light: return "Light";
    }
    return "RPC";
}

const char* oracleProviderName(OracleProvider provider) {
    switch (provider) {
        case OracleProvider::Off: return "off";
        case OracleProvider::Alcor: return "Alcor DEX";
        case OracleProvider::CoinGecko: return "CoinGecko";
        case OracleProvider::Delphi: return "Delphi (on-chain)";
    }
    return "off";
}

std::vector<const Endpoint*> EndpointList::enabledSorted() const {
    std::vector<const Endpoint*> out;
    for (const auto& node : nodes)
        if (node.enabled && !node.url.empty()) out.push_back(&node);
    std::stable_sort(out.begin(), out.end(),
                     [](const Endpoint* a, const Endpoint* b) {
                         return a->priority < b->priority;
                     });
    return out;
}

std::string EndpointList::primaryUrl() const {
    auto sorted = enabledSorted();
    return sorted.empty() ? std::string() : sorted.front()->url;
}

namespace {

json endpointListToJson(const EndpointList& list) {
    json nodes = json::array();
    for (const auto& node : list.nodes)
        nodes.push_back({{"url", node.url},
                         {"nick", node.nickname},
                         {"priority", node.priority},
                         {"enabled", node.enabled}});
    return json{{"nodes", nodes},
                {"mode", list.mode},
                {"autoThresholdQueries", list.autoThresholdQueries},
                {"autoWindowSec", list.autoWindowSec}};
}

EndpointList endpointListFromJson(const json& j) {
    EndpointList list;
    if (!j.is_object()) return list;
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& n : j["nodes"]) {
            Endpoint node;
            node.url = n.value("url", "");
            node.nickname = n.value("nick", "");
            node.priority = n.value("priority", 0);
            node.enabled = n.value("enabled", true);
            if (!node.url.empty()) list.nodes.push_back(std::move(node));
        }
    } else if (j.contains("urls") && j["urls"].is_array()) {
        // Transitional shape: bare url list, index order = priority.
        int priority = 0;
        for (const auto& url : j["urls"])
            if (url.is_string())
                list.nodes.push_back({url.get<std::string>(), "", priority++, true});
    }
    list.mode = j.value("mode", 0);
    if (list.mode < 0 || list.mode > 2) list.mode = 0;
    list.autoThresholdQueries = j.value("autoThresholdQueries", 10);
    list.autoWindowSec = j.value("autoWindowSec", 60);
    if (list.autoThresholdQueries < 1) list.autoThresholdQueries = 1;
    if (list.autoWindowSec < 5) list.autoWindowSec = 5;
    return list;
}

}  // namespace

json networkToJson(const NetworkDef& net) {
    json tokens = json::array();
    for (const auto& t : net.tokens)
        tokens.push_back({{"contract", t.contract}, {"code", t.code}});
    return json{{"chain", net.chainId},
                {"name", net.name},
                {"rpc", endpointListToJson(net.rpc)},
                {"atomic", endpointListToJson(net.atomic)},
                {"hyperion", endpointListToJson(net.hyperion)},
                {"light", endpointListToJson(net.light)},
                {"lightSlug", net.lightSlug},
                {"coreSymbol", net.coreSymbol},
                {"testnet", net.testnet},
                {"explorerTx", net.explorerTx},
                {"tokens", tokens},
                {"oracle", json{{"provider", net.oracle.provider},
                                {"url", net.oracle.url},
                                {"coreId", net.oracle.coreId}}}};
}

NetworkDef networkFromJson(const json& n) {
    NetworkDef net;
    if (!n.is_object()) return net;
    net.chainId = n.value("chain", "");
    net.name = n.value("name", "");
    if (n.contains("rpc")) {
        // Current shape: typed endpoint lists.
        net.rpc = endpointListFromJson(n["rpc"]);
        if (n.contains("atomic")) net.atomic = endpointListFromJson(n["atomic"]);
        if (n.contains("hyperion")) net.hyperion = endpointListFromJson(n["hyperion"]);
        if (n.contains("light")) net.light = endpointListFromJson(n["light"]);
    } else {
        // Pre-endpoint-section vaults: endpoints/active + single aaEndpoint.
        // The formerly-active URL becomes the top-priority node.
        size_t active = n.value("active", size_t(0));
        int priority = 1;
        if (n.contains("endpoints") && n["endpoints"].is_array()) {
            size_t index = 0;
            for (const auto& url : n["endpoints"]) {
                if (!url.is_string()) continue;
                bool wasActive = index == active;
                net.rpc.nodes.push_back(
                    {url.get<std::string>(), "", wasActive ? 0 : priority++, true});
                ++index;
            }
        }
        std::string aa = n.value("aaEndpoint", "");
        if (!aa.empty()) net.atomic.nodes.push_back({aa, "", 0, true});
    }
    net.lightSlug = n.value("lightSlug", "");
    net.coreSymbol = n.value("coreSymbol", "4,EOS");
    net.testnet = n.value("testnet", false);
    net.explorerTx = n.value("explorerTx", "");
    for (const auto& t : n.value("tokens", json::array()))
        net.tokens.push_back({t.value("contract", ""), t.value("code", "")});
    if (n.contains("oracle") && n["oracle"].is_object()) {
        const json& o = n["oracle"];
        net.oracle.provider = o.value("provider", 0);
        if (net.oracle.provider < 0 || net.oracle.provider > 3) net.oracle.provider = 0;
        net.oracle.url = o.value("url", "");
        net.oracle.coreId = o.value("coreId", "");
    }
    return net;
}

namespace {
// RFC-4180: quote fields containing commas, quotes or newlines; double
// embedded quotes.
std::string csvField(const std::string& value) {
    if (value.find_first_of(",\"\n") == std::string::npos) return value;
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}
}  // namespace

std::string auditCsv(const std::vector<AuditEntry>& entries) {
    std::string out = "time_utc,unix,chain,signer,summary,tx_id,verdict,approved\n";
    for (const auto& e : entries) {
        char stamp[32] = "";
        time_t t = static_cast<time_t>(e.time);
        if (std::tm* utc = std::gmtime(&t))
            std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", utc);
        out += std::string(stamp) + "," + std::to_string(e.time) + "," +
               csvField(e.chainId) + "," + csvField(e.signer) + "," +
               csvField(e.summary) + "," + csvField(e.txId) + "," +
               csvField(e.verdict) + "," + (e.approved ? "yes" : "no") + "\n";
    }
    return out;
}

bool Vault::fileExists() {
    std::error_code ec;
    return std::filesystem::exists(vaultFile(), ec);
}

std::string Vault::buildAad() const {
    std::ostringstream aad;
    aad << "TBX|v" << kVersion << "|scrypt|" << kdfParams_.logN << "," << kdfParams_.r << ","
        << kdfParams_.p << "|" << toHex(salt_);
    return aad.str();
}

// --- payload (de)serialization ----------------------------------------------

json Vault::serializePayload() const {
    json keys = json::array();
    for (const auto& k : keys_)
        keys.push_back({{"pub", k.pub},
                        {"wif", k.wif},
                        {"label", k.label},
                        {"created", k.created},
                        {"backedUp", k.backedUp}});

    json contacts = json::array();
    for (const auto& c : contacts_)
        contacts.push_back({{"actor", c.actor}, {"label", c.label}, {"chain", c.chainId}});

    json accounts = json::array();
    for (const auto& a : accounts_)
        accounts.push_back({{"chain", a.chainId},
                            {"actor", a.actor},
                            {"permission", a.permission},
                            {"pub", a.pubKey},
                            {"watch", a.watch}});

    json networks = json::array();
    for (const auto& n : networks_) networks.push_back(networkToJson(n));

    json pins = json::array();
    for (const auto& p : pinned_)
        pins.push_back({{"id", p.id},
                        {"chain", p.chainId},
                        {"label", p.label},
                        {"contract", p.contract},
                        {"table", p.table},
                        {"scope", p.scope},
                        {"fieldPath", p.fieldPath},
                        {"refreshSec", p.refreshSec}});

    json schedules = json::array();
    for (const auto& s : schedules_)
        schedules.push_back({{"id", s.id},
                             {"label", s.label},
                             {"chain", s.chainId},
                             {"actor", s.actor},
                             {"permission", s.permission},
                             {"contract", s.contract},
                             {"action", s.action},
                             {"data", s.data},
                             {"intervalSec", s.intervalSec},
                             {"enabled", s.enabled},
                             {"runMissedOnUnlock", s.runMissedOnUnlock},
                             {"lastRunAt", s.lastRunAt},
                             {"nextRunAt", s.nextRunAt},
                             {"lastResult", s.lastResult},
                             {"amountMode", s.amountMode},
                             {"amountField", s.amountField},
                             {"amountPercent", s.amountPercent},
                             {"amountTokenContract", s.amountTokenContract},
                             {"amountTokenCode", s.amountTokenCode},
                             {"amountReserve", s.amountReserve}});

    json links = json::array();
    for (const auto& l : links_)
        links.push_back({{"id", l.id},
                         {"appName", l.appName},
                         {"chain", l.chainId},
                         {"actor", l.actor},
                         {"permission", l.permission},
                         {"requestKey", l.requestKeyWif},
                         {"channelId", l.channelId},
                         {"serviceUrl", l.serviceUrl},
                         {"createdAt", l.createdAt},
                         {"lastUsedAt", l.lastUsedAt}});

    json rules = json::array();
    for (const auto& r : rules_) rules.push_back(r.toJSON());

    json audit = json::array();
    for (const auto& e : audit_)
        audit.push_back({{"time", e.time},
                         {"chain", e.chainId},
                         {"signer", e.signer},
                         {"summary", e.summary},
                         {"txId", e.txId},
                         {"verdict", e.verdict},
                         {"approved", e.approved}});

    json dashboard = json::array();
    for (const auto& tile : dashboard_)
        dashboard.push_back({{"kind", tile.kind}, {"span", tile.span}});

    return json{{"keys", keys},
                {"accounts", accounts},
                {"networks", networks},
                {"rules", rules},
                {"audit", audit},
                {"pinned", pins},
                {"schedules", schedules},
                {"dashboard", dashboard},
                {"contacts", contacts},
                {"lastBackupAt", lastBackupAt_},
                {"links", links},
                {"lastAccount", lastAccount_},
                {"lastChain", lastChain_},
                {"security",
                 {{"autoLockMinutes", security_.autoLockMinutes},
                  {"requirePasswordPerSign", security_.requirePasswordPerSign},
                  {"allowAutoSign", security_.allowAutoSign},
                  {"clipboardClearSec", security_.clipboardClearSec},
                  {"blockOnCriticalRisk", security_.blockOnCriticalRisk},
                  {"useResourceProvider", security_.useResourceProvider},
                  {"lockOnBackground", security_.lockOnBackground},
                  {"autopilotStandby", security_.autopilotStandby},
                  {"bgAccountRefresh", security_.bgAccountRefresh},
                  {"bgPinnedRefresh", security_.bgPinnedRefresh},
                  {"bgPriceRefresh", security_.bgPriceRefresh},
                  {"bgUpdateCheck", security_.bgUpdateCheck},
                  {"multicoreWorkers", security_.multicoreWorkers}}}};
}

Result<void> Vault::parsePayload(const json& p) {
    if (!p.is_object()) return dwarfkit::err(ErrorKind::Invalid, "vault payload is not an object");
    keys_.clear();
    accounts_.clear();
    networks_.clear();
    rules_.clear();
    audit_.clear();

    for (const auto& k : p.value("keys", json::array()))
        keys_.push_back({k.value("pub", ""), k.value("wif", ""), k.value("label", ""),
                         k.value("created", int64_t(0)), k.value("backedUp", false)});
    for (const auto& c : p.value("contacts", json::array()))
        contacts_.push_back(
            {c.value("actor", ""), c.value("label", ""), c.value("chain", "")});
    lastBackupAt_ = p.value("lastBackupAt", int64_t(0));

    for (const auto& a : p.value("accounts", json::array()))
        accounts_.push_back({a.value("chain", ""), a.value("actor", ""),
                             a.value("permission", "active"), a.value("pub", ""),
                             a.value("watch", false)});

    {
        std::set<std::string> seenChains;  // heal any historical duplicates
        for (const auto& n : p.value("networks", json::array())) {
            NetworkDef net = networkFromJson(n);
            if (!net.chainId.empty() && seenChains.insert(net.chainId).second)
                networks_.push_back(std::move(net));
        }
    }

    for (const auto& pj : p.value("pinned", json::array())) {
        PinnedQuery pin;
        pin.id = pj.value("id", "");
        pin.chainId = pj.value("chain", "");
        pin.label = pj.value("label", "");
        pin.contract = pj.value("contract", "");
        pin.table = pj.value("table", "");
        pin.scope = pj.value("scope", "");
        pin.fieldPath = pj.value("fieldPath", "");
        pin.refreshSec = pj.value("refreshSec", 60);
        if (!pin.id.empty() && !pin.contract.empty()) pinned_.push_back(std::move(pin));
    }

    if (p.contains("dashboard") && p["dashboard"].is_array()) {
        dashboard_.clear();
        for (const auto& t : p["dashboard"]) {
            DashTile tile;
            tile.kind = t.value("kind", "");
            tile.span = t.value("span", 1);
            if (tile.span < 1 || tile.span > 2) tile.span = 1;
            if (!tile.kind.empty()) dashboard_.push_back(std::move(tile));
        }
    } else {
        // Pre-board vaults: the classic layout plus a tile per existing pin.
        dashboard_ = defaultDashboard();
        for (const auto& pin : pinned_) dashboard_.push_back({"pin:" + pin.id, 1});
    }

    for (const auto& sj : p.value("schedules", json::array())) {
        Schedule sched;
        sched.id = sj.value("id", "");
        sched.label = sj.value("label", "");
        sched.chainId = sj.value("chain", "");
        sched.actor = sj.value("actor", "");
        sched.permission = sj.value("permission", "active");
        sched.contract = sj.value("contract", "");
        sched.action = sj.value("action", "");
        sched.data = sj.contains("data") ? sj["data"] : json::object();
        sched.intervalSec = sj.value("intervalSec", int64_t(24 * 3600));
        sched.enabled = sj.value("enabled", true);
        sched.runMissedOnUnlock = sj.value("runMissedOnUnlock", true);
        sched.lastRunAt = sj.value("lastRunAt", int64_t(0));
        sched.nextRunAt = sj.value("nextRunAt", int64_t(0));
        sched.lastResult = sj.value("lastResult", "");
        sched.amountMode = sj.value("amountMode", 0);
        sched.amountField = sj.value("amountField", "");
        sched.amountPercent = sj.value("amountPercent", 0.0);
        sched.amountTokenContract = sj.value("amountTokenContract", "");
        sched.amountTokenCode = sj.value("amountTokenCode", "");
        sched.amountReserve = sj.value("amountReserve", "");
        if (!sched.id.empty()) schedules_.push_back(std::move(sched));
    }

    for (const auto& lj : p.value("links", json::array())) {
        LinkSession link;
        link.id = lj.value("id", "");
        link.appName = lj.value("appName", "");
        link.chainId = lj.value("chain", "");
        link.actor = lj.value("actor", "");
        link.permission = lj.value("permission", "");
        link.requestKeyWif = lj.value("requestKey", "");
        link.channelId = lj.value("channelId", "");
        link.serviceUrl = lj.value("serviceUrl", "https://cb.anchor.link");
        link.createdAt = lj.value("createdAt", int64_t(0));
        link.lastUsedAt = lj.value("lastUsedAt", int64_t(0));
        if (!link.id.empty()) links_.push_back(std::move(link));
    }

    lastAccount_ = p.value("lastAccount", "");
    lastChain_ = p.value("lastChain", "");

    for (const auto& r : p.value("rules", json::array())) {
        auto rule = guard::WhitelistRule::fromJSON(r);
        if (rule)
            rules_.push_back(std::move(*rule));
        else
            Log::warn("vault: skipping malformed whitelist rule: %s",
                      rule.error().message.c_str());
    }

    for (const auto& e : p.value("audit", json::array()))
        audit_.push_back({e.value("time", int64_t(0)), e.value("chain", ""),
                          e.value("signer", ""), e.value("summary", ""), e.value("txId", ""),
                          e.value("verdict", ""), e.value("approved", false)});

    if (p.contains("security")) {
        const auto& s = p["security"];
        security_.autoLockMinutes = s.value("autoLockMinutes", 15);
        security_.requirePasswordPerSign = s.value("requirePasswordPerSign", false);
        security_.allowAutoSign = s.value("allowAutoSign", false);
        security_.clipboardClearSec = s.value("clipboardClearSec", 20);
        security_.blockOnCriticalRisk = s.value("blockOnCriticalRisk", true);
        security_.useResourceProvider = s.value("useResourceProvider", false);
        security_.lockOnBackground = s.value("lockOnBackground", true);
        security_.autopilotStandby = s.value("autopilotStandby", false);
        security_.bgAccountRefresh = s.value("bgAccountRefresh", true);
        security_.bgPinnedRefresh = s.value("bgPinnedRefresh", true);
        security_.bgPriceRefresh = s.value("bgPriceRefresh", true);
        security_.bgUpdateCheck = s.value("bgUpdateCheck", true);
        security_.multicoreWorkers = s.value("multicoreWorkers", true);
    }
    return {};
}

// --- lifecycle ---------------------------------------------------------------

Result<void> Vault::create(const SecureBytes& password) {
    if (password.size() < 8)
        return dwarfkit::err(ErrorKind::Invalid, "password must be at least 8 characters");
    wipeState();
    kdfParams_ = ScryptParams{};
    salt_.resize(32);
    randomBytes(salt_.data(), salt_.size());
    keyBlock_ = deriveVaultKeys(password, salt_, kdfParams_);
    if (keyBlock_.empty()) return dwarfkit::err(ErrorKind::Internal, "key derivation failed");
    unlocked_ = true;
    return save();
}

Result<void> Vault::loadEnvelope(json& envelope) const {
    std::ifstream in(vaultFile(), std::ios::binary);
    if (!in) return dwarfkit::err(ErrorKind::Storage, "cannot open vault file");
    std::stringstream buffer;
    buffer << in.rdbuf();
    envelope = json::parse(buffer.str(), nullptr, false);
    if (envelope.is_discarded())
        return dwarfkit::err(ErrorKind::Storage, "vault file is not valid JSON");
    if (envelope.value("magic", "") != kMagic)
        return dwarfkit::err(ErrorKind::Storage, "not a TackleBox vault file");
    if (envelope.value("version", 0) != kVersion)
        return dwarfkit::err(ErrorKind::Storage, "unsupported vault version");
    return {};
}

Result<void> Vault::unlock(const SecureBytes& password) {
    json envelope;
    DK_CHECK(loadEnvelope(envelope));

    const auto& kdf = envelope["kdf"];
    ScryptParams params;
    params.logN = kdf.value("logN", 15u);
    params.r = kdf.value("r", 8u);
    params.p = kdf.value("p", 1u);
    if (!params.valid()) return dwarfkit::err(ErrorKind::Storage, "invalid KDF parameters");
    auto salt = fromHex(kdf.value("salt", ""));
    if (!salt || salt->size() < 16)
        return dwarfkit::err(ErrorKind::Storage, "invalid KDF salt");

    auto iv = fromHex(envelope["cipher"].value("iv", ""));
    auto ct = fromHex(envelope.value("ciphertext", ""));
    auto mac = fromHex(envelope.value("mac", ""));
    if (!iv || iv->size() != 16 || !ct || ct->empty() || !mac || mac->size() != 32)
        return dwarfkit::err(ErrorKind::Storage, "malformed vault envelope");

    wipeState();
    kdfParams_ = params;
    salt_ = *salt;
    keyBlock_ = deriveVaultKeys(password, salt_, kdfParams_);
    if (keyBlock_.empty()) return dwarfkit::err(ErrorKind::Internal, "key derivation failed");

    SealedBox box;
    std::copy(iv->begin(), iv->end(), box.iv.begin());
    box.ciphertext = std::move(*ct);
    std::copy(mac->begin(), mac->end(), box.mac.begin());

    std::string aad = buildAad();
    auto plain = open(keyBlock_, box,
                      std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(aad.data()),
                                               aad.size()));
    if (!plain) {
        keyBlock_.clear();
        return dwarfkit::err(ErrorKind::Invalid, "wrong password (or the vault file was tampered with)");
    }

    json payload = json::parse(plain->view(), nullptr, false);
    plain->clear();
    if (payload.is_discarded()) {
        keyBlock_.clear();
        return dwarfkit::err(ErrorKind::Storage, "vault payload is corrupt");
    }
    auto parsed = parsePayload(payload);
    payload = json();  // drop plaintext copies held by the DOM
    if (!parsed) {
        keyBlock_.clear();
        return dwarfkit::err(parsed.error());
    }
    unlocked_ = true;
    Log::info("vault unlocked: %zu key(s), %zu account(s), %zu rule(s)", keys_.size(),
              accounts_.size(), rules_.size());
    return {};
}

void Vault::lock() {
    wipeState();
    Log::info("vault locked");
}

void Vault::wipeState() {
    for (auto& k : keys_) secureWipe(k.wif.data(), k.wif.size());
    for (auto& l : links_) secureWipe(l.requestKeyWif.data(), l.requestKeyWif.size());
    keys_.clear();
    accounts_.clear();
    networks_.clear();
    rules_.clear();
    audit_.clear();
    pinned_.clear();
    schedules_.clear();
    links_.clear();
    lastAccount_.clear();
    security_ = SecurityPrefs{};
    keyBlock_.clear();
    unlocked_ = false;
}

Result<void> Vault::changePassword(const SecureBytes& current, const SecureBytes& next) {
    if (!unlocked_) return dwarfkit::err(ErrorKind::Invalid, "vault is locked");
    if (next.size() < 8)
        return dwarfkit::err(ErrorKind::Invalid, "password must be at least 8 characters");
    // Verify the current password by re-deriving against the stored salt.
    SecureBytes check = deriveVaultKeys(current, salt_, kdfParams_);
    if (check.empty() || !constTimeEq(check.span(), keyBlock_.span()))
        return dwarfkit::err(ErrorKind::Invalid, "current password is incorrect");
    salt_.assign(32, 0);
    randomBytes(salt_.data(), salt_.size());
    kdfParams_ = ScryptParams{};
    keyBlock_ = deriveVaultKeys(next, salt_, kdfParams_);
    if (keyBlock_.empty()) return dwarfkit::err(ErrorKind::Internal, "key derivation failed");
    return save();
}

Result<void> Vault::save() {
    if (!unlocked_) return dwarfkit::err(ErrorKind::Invalid, "vault is locked");

    std::string plain = serializePayload().dump();
    std::string aad = buildAad();
    SealedBox box = seal(keyBlock_,
                         std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(plain.data()),
                                                  plain.size()),
                         std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(aad.data()),
                                                  aad.size()));
    secureWipe(plain.data(), plain.size());

    json envelope{{"magic", kMagic},
                  {"version", kVersion},
                  {"kdf",
                   {{"algo", "scrypt"},
                    {"logN", kdfParams_.logN},
                    {"r", kdfParams_.r},
                    {"p", kdfParams_.p},
                    {"salt", toHex(salt_)}}},
                  {"cipher", {{"algo", "aes-256-cbc+hmac-sha256"}, {"iv", toHex(box.iv)}}},
                  {"ciphertext", toHex(box.ciphertext)},
                  {"mac", toHex(box.mac)},
                  {"modified", formatIsoUtc(nowSec())}};

    if (!atomicWrite(vaultFile(), envelope.dump(2), /*keepBackup=*/true))
        return dwarfkit::err(ErrorKind::Storage, "failed to write vault file");
    return {};
}

// --- keys --------------------------------------------------------------------

Result<std::string> Vault::importKey(const std::string& wif, const std::string& label) {
    if (!unlocked_) return dwarfkit::err(ErrorKind::Invalid, "vault is locked");
    auto key = dwarfkit::PrivateKey::from(trim(wif));
    if (!key) return dwarfkit::err(ErrorKind::Invalid, "not a valid private key: " + key.error().message);
    auto pub = key->toPublic();
    if (!pub) return dwarfkit::err(pub.error());
    std::string pubStr = pub->toString();
    for (const auto& existing : keys_)
        if (existing.pub == pubStr)
            return dwarfkit::err(ErrorKind::Invalid, "key already in vault (" + pubStr + ")");
    keys_.push_back({pubStr, trim(wif), label, nowSec()});
    DK_CHECK(save());
    return pubStr;
}

Result<std::string> Vault::generateKey(const std::string& label) {
    if (!unlocked_) return dwarfkit::err(ErrorKind::Invalid, "vault is locked");
    auto key = dwarfkit::PrivateKey::generate(dwarfkit::KeyType::K1);
    if (!key) return dwarfkit::err(key.error());
    auto pub = key->toPublic();
    if (!pub) return dwarfkit::err(pub.error());
    keys_.push_back({pub->toString(), key->toString(), label, nowSec()});
    DK_CHECK(save());
    return pub->toString();
}

bool Vault::removeKey(const std::string& pub) {
    if (!unlocked_) return false;
    auto it = std::find_if(keys_.begin(), keys_.end(),
                           [&](const KeyEntry& k) { return k.pub == pub; });
    if (it == keys_.end()) return false;
    secureWipe(it->wif.data(), it->wif.size());
    keys_.erase(it);
    for (auto& a : accounts_)
        if (a.pubKey == pub) {
            a.pubKey.clear();
            a.watch = true;  // account degrades to watch-only
        }
    return bool(save());
}

std::string Vault::wifFor(const std::string& pub) const {
    for (const auto& k : keys_)
        if (k.pub == pub) return k.wif;
    return {};
}

// --- accounts ----------------------------------------------------------------

void Vault::addAccount(const AccountRef& account) {
    for (const auto& a : accounts_)
        if (a.chainId == account.chainId && a.actor == account.actor &&
            a.permission == account.permission)
            return;
    accounts_.push_back(account);
    (void)save();
}

bool Vault::removeAccount(const std::string& chainId, const std::string& actor,
                          const std::string& permission) {
    auto it = std::remove_if(accounts_.begin(), accounts_.end(), [&](const AccountRef& a) {
        return a.chainId == chainId && a.actor == actor && a.permission == permission;
    });
    if (it == accounts_.end()) return false;
    accounts_.erase(it, accounts_.end());
    return bool(save());
}

// --- networks ----------------------------------------------------------------

NetworkDef* Vault::network(const std::string& chainId) {
    for (auto& n : networks_)
        if (n.chainId == chainId) return &n;
    return nullptr;
}

void Vault::upsertNetwork(const NetworkDef& net) {
    for (auto& n : networks_)
        if (n.chainId == net.chainId) {
            n = net;
            (void)save();
            return;
        }
    networks_.push_back(net);
    (void)save();
}

bool Vault::removeNetwork(const std::string& chainId) {
    auto it = std::remove_if(networks_.begin(), networks_.end(),
                             [&](const NetworkDef& n) { return n.chainId == chainId; });
    if (it == networks_.end()) return false;
    networks_.erase(it, networks_.end());
    return bool(save());
}

// --- rules -------------------------------------------------------------------

guard::WhitelistRule* Vault::rule(const std::string& id) {
    for (auto& r : rules_)
        if (r.id == id) return &r;
    return nullptr;
}

void Vault::upsertRule(const guard::WhitelistRule& rule) {
    for (auto& r : rules_)
        if (r.id == rule.id) {
            r = rule;
            (void)save();
            return;
        }
    rules_.push_back(rule);
    (void)save();
}

bool Vault::removeRule(const std::string& id) {
    auto it = std::remove_if(rules_.begin(), rules_.end(),
                             [&](const guard::WhitelistRule& r) { return r.id == id; });
    if (it == rules_.end()) return false;
    rules_.erase(it, rules_.end());
    return bool(save());
}

// --- audit -------------------------------------------------------------------

void Vault::appendAudit(AuditEntry entry) {
    audit_.insert(audit_.begin(), std::move(entry));
    if (audit_.size() > kAuditMax) audit_.resize(kAuditMax);
    (void)save();
}

// --- pinned queries ----------------------------------------------------------

void Vault::upsertPinnedQuery(const PinnedQuery& query) {
    bool existed = false;
    for (auto& existing : pinned_)
        if (existing.id == query.id) {
            existing = query;
            existed = true;
        }
    if (!existed) pinned_.push_back(query);
    // Every pin lives on the board as its own tile.
    std::string kind = "pin:" + query.id;
    bool present = false;
    for (const auto& tile : dashboard_) present |= tile.kind == kind;
    if (!present) dashboard_.push_back({kind, 1});
    (void)save();
}

void Vault::stampBackup() {
    lastBackupAt_ = nowSec();
    (void)save();
}

bool Vault::markKeyBackedUp(const std::string& pub) {
    for (auto& key : keys_)
        if (key.pub == pub && !key.backedUp) {
            key.backedUp = true;
            return bool(save());
        }
    return false;
}

void Vault::upsertContact(const Contact& contact) {
    for (auto& existing : contacts_)
        if (existing.actor == contact.actor && existing.chainId == contact.chainId) {
            existing = contact;
            (void)save();
            return;
        }
    contacts_.push_back(contact);
    (void)save();
}

bool Vault::removeContact(const std::string& actor, const std::string& chainId) {
    size_t before = contacts_.size();
    std::erase_if(contacts_, [&](const Contact& c) {
        return c.actor == actor && c.chainId == chainId;
    });
    if (contacts_.size() == before) return false;
    return bool(save());
}

bool Vault::removePinnedQuery(const std::string& id) {
    auto it = std::remove_if(pinned_.begin(), pinned_.end(),
                             [&](const PinnedQuery& q) { return q.id == id; });
    if (it == pinned_.end()) return false;
    pinned_.erase(it, pinned_.end());
    std::erase_if(dashboard_, [&](const DashTile& t) { return t.kind == "pin:" + id; });
    return bool(save());
}

// --- dashboard board ---------------------------------------------------------

std::vector<DashTile> defaultDashboard() {
    return {{"balance", 2}, {"resources", 1}, {"guard", 1}, {"activity", 2}};
}

void Vault::setDashboardTiles(std::vector<DashTile> tiles) {
    dashboard_ = std::move(tiles);
    (void)save();
}

bool Vault::reorderSchedules(size_t from, size_t to) {
    if (from >= schedules_.size() || to >= schedules_.size() || from == to) return false;
    Schedule moved = std::move(schedules_[from]);
    schedules_.erase(schedules_.begin() + static_cast<ptrdiff_t>(from));
    schedules_.insert(schedules_.begin() + static_cast<ptrdiff_t>(to), std::move(moved));
    return bool(save());
}

bool Vault::reorderAccounts(size_t from, size_t to) {
    if (from >= accounts_.size() || to >= accounts_.size() || from == to) return false;
    AccountRef moved = accounts_[from];
    accounts_.erase(accounts_.begin() + static_cast<ptrdiff_t>(from));
    accounts_.insert(accounts_.begin() + static_cast<ptrdiff_t>(to), moved);
    return bool(save());
}

// --- schedules ---------------------------------------------------------------

Schedule* Vault::schedule(const std::string& id) {
    for (auto& s : schedules_)
        if (s.id == id) return &s;
    return nullptr;
}

void Vault::upsertSchedule(const Schedule& schedule) {
    for (auto& existing : schedules_)
        if (existing.id == schedule.id) {
            existing = schedule;
            (void)save();
            return;
        }
    schedules_.push_back(schedule);
    (void)save();
}

bool Vault::removeSchedule(const std::string& id) {
    auto it = std::remove_if(schedules_.begin(), schedules_.end(),
                             [&](const Schedule& s) { return s.id == id; });
    if (it == schedules_.end()) return false;
    schedules_.erase(it, schedules_.end());
    return bool(save());
}

// --- link sessions -----------------------------------------------------------

LinkSession* Vault::linkSession(const std::string& id) {
    for (auto& l : links_)
        if (l.id == id) return &l;
    return nullptr;
}

void Vault::upsertLinkSession(const LinkSession& session) {
    for (auto& existing : links_)
        if (existing.id == session.id) {
            existing = session;
            (void)save();
            return;
        }
    links_.push_back(session);
    (void)save();
}

bool Vault::removeLinkSession(const std::string& id) {
    auto it = std::find_if(links_.begin(), links_.end(),
                           [&](const LinkSession& l) { return l.id == id; });
    if (it == links_.end()) return false;
    secureWipe(it->requestKeyWif.data(), it->requestKeyWif.size());
    links_.erase(it);
    return bool(save());
}

// --- ergonomics --------------------------------------------------------------

void Vault::setLastAccount(const std::string& key) {
    if (lastAccount_ == key) return;
    lastAccount_ = key;
    (void)save();
}

void Vault::setLastChain(const std::string& chainId) {
    if (lastChain_ == chainId) return;
    lastChain_ = chainId;
    (void)save();
}

// --- portability -------------------------------------------------------------

Result<void> Vault::exportTo(const std::filesystem::path& dest) {
    std::error_code ec;
    if (!std::filesystem::exists(vaultFile(), ec))
        return dwarfkit::err(ErrorKind::Storage, "there is no vault file to export");
    std::filesystem::path target = dest;
    if (std::filesystem::is_directory(target, ec)) target /= "tacklebox-vault.tbx";
    if (target.extension() != ".tbx") target += ".tbx";
    std::filesystem::create_directories(target.parent_path(), ec);
    std::filesystem::copy_file(vaultFile(), target,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
        return dwarfkit::err(ErrorKind::Storage, "export failed: " + ec.message());
    Log::info("vault exported to %s", target.string().c_str());
    return {};
}

Result<void> Vault::importFrom(const std::filesystem::path& src) {
    std::error_code ec;
    if (!std::filesystem::exists(src, ec))
        return dwarfkit::err(ErrorKind::Storage, "file not found: " + src.string());

    // Validate the envelope before touching anything.
    std::ifstream in(src, std::ios::binary);
    if (!in) return dwarfkit::err(ErrorKind::Storage, "cannot read " + src.string());
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string content = buffer.str();
    json envelope = json::parse(content, nullptr, false);
    if (envelope.is_discarded() || !envelope.is_object())
        return dwarfkit::err(ErrorKind::Invalid, "not a TackleBox vault file (bad JSON)");
    if (envelope.value("magic", "") != kMagic)
        return dwarfkit::err(ErrorKind::Invalid, "not a TackleBox vault file (magic mismatch)");
    if (envelope.value("version", 0) != kVersion)
        return dwarfkit::err(ErrorKind::Invalid, "unsupported vault version");
    if (!envelope.contains("kdf") || !envelope.contains("cipher") ||
        envelope.value("ciphertext", "").empty() || envelope.value("mac", "").empty())
        return dwarfkit::err(ErrorKind::Invalid, "vault envelope is incomplete");

    // Keep whatever vault is currently installed.
    if (std::filesystem::exists(vaultFile(), ec)) {
        std::filesystem::path keep = vaultFile();
        keep += ".replaced-" + std::to_string(nowSec());
        std::filesystem::copy_file(vaultFile(), keep, ec);
        if (ec)
            return dwarfkit::err(ErrorKind::Storage,
                                 "could not back up the current vault: " + ec.message());
        Log::info("existing vault preserved as %s", keep.filename().string().c_str());
    }

    if (!atomicWrite(vaultFile(), content, /*keepBackup=*/false))
        return dwarfkit::err(ErrorKind::Storage, "failed to install the imported vault");
    Log::info("vault imported from %s", src.string().c_str());
    return {};
}

}  // namespace tb
