// The TackleBox vault: one encrypted file holding everything sensitive or
// integrity-critical - private keys, account list, network endpoints,
// whitelist rules, security preferences, and the local signing audit log.
//
// Networks and rules live inside the ciphertext on purpose: endpoints decide
// which node the wallet believes, and rules decide what signs without review.
// Neither may be editable by anything but the unlocked wallet.
//
// File format (vault.tbx), JSON envelope:
//   { "magic": "TACKLEBOX_VAULT", "version": 1,
//     "kdf": {"algo": "scrypt", "logN": 15, "r": 8, "p": 1, "salt": hex},
//     "cipher": {"algo": "aes-256-cbc+hmac-sha256", "iv": hex},
//     "ciphertext": hex, "mac": hex, "modified": iso8601 }
// MAC covers aad("TBX|v1|scrypt|logN,r,p|salt") || iv || ciphertext.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <dwarfkit/core/json.hpp>
#include <dwarfkit/core/result.hpp>

#include "core/secure.hpp"
#include "guard/rules.hpp"
#include "vault/kdf.hpp"

namespace tb {

using dwarfkit::json;
template <class T>
using Result = dwarfkit::Result<T>;

struct KeyEntry {
    std::string pub;    // "PUB_K1_..." / "PUB_R1_..."
    std::string wif;    // private key string; wiped when the vault locks
    std::string label;
    int64_t created = 0;
};

struct AccountRef {
    std::string chainId;     // hex
    std::string actor;
    std::string permission;  // usually "active"
    std::string pubKey;      // which vault key authorizes it; empty for watch
    bool watch = false;      // watch-only: visible, cannot sign

    std::string display() const { return actor + "@" + permission; }
};

// A non-core token the wallet tracks on a network.
struct TokenDef {
    std::string contract;  // e.g. "alien.worlds"
    std::string code;      // e.g. "TLM"
};

// One node in an endpoint pool.
struct Endpoint {
    std::string url;
    std::string nickname;  // "Greymass", "my home node", ...
    int priority = 0;      // lower = preferred
    bool enabled = true;
};

// Selection policy for a pool.
enum class SelectMode {
    Priority = 0,   // always the best-priority enabled node
    RoundRobin = 1, // rotate every request across enabled nodes
    Auto = 2,       // Priority normally; RoundRobin IF the recent query volume
                    // exceeds autoThresholdQueries within autoWindowSec
};

// One node type's endpoint pool + its selection policy. Rotation state lives
// in the chain service (runtime), only the policy persists here.
struct EndpointList {
    std::vector<Endpoint> nodes;
    int mode = static_cast<int>(SelectMode::Priority);
    int autoThresholdQueries = 10;  // "use round robin IF > N queries..."
    int autoWindowSec = 60;         // "...within this many seconds"

    bool empty() const { return nodes.empty(); }
    // Enabled nodes, best priority first (stable for equal priorities).
    std::vector<const Endpoint*> enabledSorted() const;
    // The display/default node: best-priority enabled URL ("" when none).
    std::string primaryUrl() const;
};

// The node types a chain can be reached through.
enum class NodeType { Rpc, Atomic, Hyperion, Light };
const char* nodeTypeName(NodeType type);  // "RPC" / "Atomic" / "Hyperion" / "Light"

struct NetworkDef {
    std::string chainId;  // hex
    std::string name;
    EndpointList rpc;       // nodeos chain API; the one list that must not be empty
    EndpointList atomic;    // Atomic Assets API (NFTs); empty = feature off
    EndpointList hyperion;  // /v2 history API; empty = legacy v1 history only
    EndpointList light;     // light API (all-token balances); empty = registry only
    std::string lightSlug;  // network slug the light API expects ("wax", "eos", ...)
    std::string coreSymbol = "4,EOS";  // precision,CODE
    bool testnet = false;
    std::string explorerTx;  // URL template containing {txid}, may be empty
    std::vector<TokenDef> tokens;  // tracked tokens beyond the core symbol

    EndpointList& list(NodeType type) {
        switch (type) {
            case NodeType::Atomic: return atomic;
            case NodeType::Hyperion: return hyperion;
            case NodeType::Light: return light;
            default: return rpc;
        }
    }
    const EndpointList& list(NodeType type) const {
        return const_cast<NetworkDef*>(this)->list(type);
    }
    // Display/default RPC node (per-request selection lives in ChainService).
    std::string activeEndpoint() const { return rpc.primaryUrl(); }
    std::string coreSymbolCode() const {
        auto comma = coreSymbol.find(',');
        return comma == std::string::npos ? coreSymbol : coreSymbol.substr(comma + 1);
    }
};

// Network (de)serialization, exposed for the migration tests. Reads both the
// current shape and the pre-endpoint-section one (endpoints/active/aaEndpoint).
dwarfkit::json networkToJson(const NetworkDef& net);
NetworkDef networkFromJson(const dwarfkit::json& j);

struct SecurityPrefs {
    int autoLockMinutes = 15;        // 0 = never
    bool requirePasswordPerSign = false;
    bool allowAutoSign = false;      // master switch for rule autoSign
    int clipboardClearSec = 20;      // 0 = never clear
    bool blockOnCriticalRisk = true; // critical risk flags force hold-to-sign
    bool useResourceProvider = false;  // fuel-style cosigning (may quote fees)
    bool lockOnBackground = true;    // mobile: lock the instant the app hides
};

// A saved table query rendered on the dashboard.
struct PinnedQuery {
    std::string id;
    std::string chainId;
    std::string label;
    std::string contract;
    std::string table;
    std::string scope;      // empty = contract
    std::string fieldPath;  // dotted path into row[0]; empty = show the row
    int refreshSec = 60;
};

// A recurring transaction. Executes ONLY through the guard's auto-sign fast
// path: without a matching pinned auto-sign rule the run is skipped and the
// failure recorded - schedules are a clock, never a fourth way to sign.
//
// Dynamic amounts (auto-stacking/auto-staking): with amountMode == Percent,
// `amountField` inside `data` is overwritten at run time with
// floor(percent% of the actor's fresh liquid balance of amountToken, minus
// amountReserve). String values in `data` support run-time placeholders:
// {actor} {amount} {balance} {date} {time}.
struct Schedule {
    enum AmountMode { AmountFixed = 0, AmountPercent = 1 };

    std::string id;
    std::string label;
    std::string chainId;
    std::string actor;
    std::string permission = "active";
    std::string contract;
    std::string action;
    dwarfkit::json data;      // action parameters (decoded json)
    int64_t intervalSec = 24 * 3600;
    bool enabled = true;
    bool runMissedOnUnlock = true;  // fire immediately when a due run was missed
    int64_t lastRunAt = 0;
    int64_t nextRunAt = 0;
    std::string lastResult;   // human-readable outcome of the last attempt

    int amountMode = AmountFixed;
    std::string amountField;           // dotted field in data, e.g. "quantity"
    double amountPercent = 0.0;        // 0 < percent <= 100
    std::string amountTokenContract;   // e.g. "eosio.token"
    std::string amountTokenCode;       // e.g. "WAX"
    std::string amountReserve;         // optional "1.0000 WAX" left untouched
};

// A dapp link (anchor-link style): the dapp pushes signing requests to our
// buoy channel, sealed to the session request key.
struct LinkSession {
    std::string id;
    std::string appName;
    std::string chainId;
    std::string actor;
    std::string permission;
    std::string requestKeyWif;  // wallet-side session key; wiped on lock
    std::string channelId;      // our buoy channel uuid
    std::string serviceUrl = "https://cb.anchor.link";
    int64_t createdAt = 0;
    int64_t lastUsedAt = 0;
};

struct AuditEntry {
    int64_t time = 0;
    std::string chainId;
    std::string signer;   // actor@permission
    std::string summary;  // "eosio.token::transfer alice -> bob 1.0000 EOS"
    std::string txId;     // empty when not broadcast
    std::string verdict;  // guard verdict at signing time
    bool approved = false;
};

class Vault {
public:
    // --- lifecycle ---------------------------------------------------------
    static bool fileExists();

    // Create a brand-new vault protected by `password` and write it to disk.
    Result<void> create(const SecureBytes& password);

    // Load + decrypt. Wrong password and tampering are indistinguishable by
    // design (MAC failure): both return an error.
    Result<void> unlock(const SecureBytes& password);

    void lock();  // wipe keys and derived key material
    bool unlocked() const { return unlocked_; }

    // Re-encrypt everything under a new password (fresh salt).
    Result<void> changePassword(const SecureBytes& current, const SecureBytes& next);

    // Persist current state. No-op when locked.
    Result<void> save();

    // --- keys --------------------------------------------------------------
    const std::vector<KeyEntry>& keys() const { return keys_; }
    // Import a WIF/PVT key; returns the derived public key.
    Result<std::string> importKey(const std::string& wif, const std::string& label);
    // Generate a fresh K1 key; returns the public key.
    Result<std::string> generateKey(const std::string& label);
    bool removeKey(const std::string& pub);  // also detaches matching accounts
    // Private key for a public key; empty when absent.
    std::string wifFor(const std::string& pub) const;

    // --- accounts ----------------------------------------------------------
    const std::vector<AccountRef>& accounts() const { return accounts_; }
    void addAccount(const AccountRef& account);
    bool removeAccount(const std::string& chainId, const std::string& actor,
                       const std::string& permission);

    // --- networks ----------------------------------------------------------
    const std::vector<NetworkDef>& networks() const { return networks_; }
    NetworkDef* network(const std::string& chainId);
    void upsertNetwork(const NetworkDef& net);
    bool removeNetwork(const std::string& chainId);

    // --- whitelist rules ---------------------------------------------------
    const std::vector<guard::WhitelistRule>& rules() const { return rules_; }
    guard::WhitelistRule* rule(const std::string& id);
    void upsertRule(const guard::WhitelistRule& rule);
    bool removeRule(const std::string& id);

    // --- security prefs ----------------------------------------------------
    SecurityPrefs& security() { return security_; }
    const SecurityPrefs& security() const { return security_; }

    // --- pinned queries ----------------------------------------------------
    const std::vector<PinnedQuery>& pinnedQueries() const { return pinned_; }
    void upsertPinnedQuery(const PinnedQuery& query);
    bool removePinnedQuery(const std::string& id);

    // --- schedules -----------------------------------------------------------
    const std::vector<Schedule>& schedules() const { return schedules_; }
    Schedule* schedule(const std::string& id);
    void upsertSchedule(const Schedule& schedule);
    bool removeSchedule(const std::string& id);

    // --- link sessions -------------------------------------------------------
    const std::vector<LinkSession>& linkSessions() const { return links_; }
    LinkSession* linkSession(const std::string& id);
    void upsertLinkSession(const LinkSession& session);
    bool removeLinkSession(const std::string& id);

    // --- ergonomics ----------------------------------------------------------
    const std::string& lastAccount() const { return lastAccount_; }
    void setLastAccount(const std::string& key);  // "chainId|actor|permission"
    const std::string& lastChain() const { return lastChain_; }
    void setLastChain(const std::string& chainId);

    // --- audit log ---------------------------------------------------------
    const std::vector<AuditEntry>& audit() const { return audit_; }
    void appendAudit(AuditEntry entry);  // bounded; newest first

    // --- portability ---------------------------------------------------------
    // Copy the sealed vault file to `dest` verbatim (never decrypts).
    static Result<void> exportTo(const std::filesystem::path& dest);
    // Validate `src` as a TackleBox vault envelope and install it as the
    // active vault file (existing vault is kept as a timestamped backup).
    // The caller must lock first; the imported vault opens with ITS password.
    static Result<void> importFrom(const std::filesystem::path& src);

private:
    Result<void> loadEnvelope(json& envelope) const;
    std::string buildAad() const;
    json serializePayload() const;
    Result<void> parsePayload(const json& payload);
    void wipeState();

    bool unlocked_ = false;
    ScryptParams kdfParams_;
    std::vector<uint8_t> salt_;
    SecureBytes keyBlock_;  // 64-byte scrypt output kept while unlocked

    std::vector<KeyEntry> keys_;
    std::vector<AccountRef> accounts_;
    std::vector<NetworkDef> networks_;
    std::vector<guard::WhitelistRule> rules_;
    SecurityPrefs security_;
    std::vector<AuditEntry> audit_;
    std::vector<PinnedQuery> pinned_;
    std::vector<Schedule> schedules_;
    std::vector<LinkSession> links_;
    std::string lastAccount_;
    std::string lastChain_;
};

}  // namespace tb
