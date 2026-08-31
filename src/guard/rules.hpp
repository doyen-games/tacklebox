// Whitelist rule model. A rule says: this signer, on this chain, may run this
// contract action, with each parameter constrained to a set or range, and only
// while the contract's code and ABI still hash to what was approved.
//
// Rules are stored inside the encrypted vault: an attacker who can edit the
// rule store could otherwise whitelist their own drain transaction.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <dwarfkit/core/json.hpp>
#include <dwarfkit/core/result.hpp>

namespace tb::guard {

using dwarfkit::json;

enum class ConstraintKind {
    Any,    // wildcard: any value passes
    Exact,  // value must equal values[0]
    OneOf,  // value must equal one of values
    Range,  // numeric or asset amount within [min, max], bounds optional
};

const char* constraintKindName(ConstraintKind k);

struct ParamConstraint {
    ConstraintKind kind = ConstraintKind::Any;
    std::vector<json> values;  // Exact uses [0]; OneOf uses all entries
    std::optional<json> min;   // Range bounds; number, "123", or "1.0000 EOS"
    std::optional<json> max;

    // Does `value` (decoded action data field) satisfy this constraint?
    // On failure `why` receives a human-readable reason.
    bool check(const json& value, std::string& why) const;

    json toJSON() const;
    static ParamConstraint fromJSON(const json& j);

    // One-line description for list views ("= alice", "in {a, b}", "0 .. 100").
    std::string describe() const;
};

// Loose equivalence for decoded ABI values: numbers match numeric strings
// (the serializer emits >32-bit integers as strings), everything else must
// match exactly. Exposed for tests.
bool jsonEquiv(const json& a, const json& b);

// Parse "12.3456 SYM" into amount units and symbol. Exposed for tests.
struct AssetValue {
    int64_t amount = 0;  // in smallest units
    uint8_t precision = 0;
    std::string code;
};
std::optional<AssetValue> parseAsset(const std::string& s);

struct ContractPin {
    std::string codeHash;  // sha256 of the deployed wasm, hex
    std::string abiHash;   // sha256 of the raw abi, hex
    int64_t pinnedAt = 0;
};

enum class RuleStatus {
    Active,    // in force
    Stale,     // contract code/abi changed since approval; inert until re-pinned
    Disabled,  // switched off by the user; kept for history
};

const char* ruleStatusName(RuleStatus s);

struct WhitelistRule {
    std::string id;         // uuid
    std::string chainId;    // chain id hex, or "*" for any chain
    std::string signer;     // "actor@permission"; either side may be "*"
    std::string contract;   // contract account name (exact)
    std::string action;     // action name, or "*" for any action on the contract
    std::map<std::string, ParamConstraint> params;  // field path -> constraint
    bool unlistedParamsAny = true;  // fields with no entry: pass (true) or fail (false)
    std::optional<ContractPin> pin;
    RuleStatus status = RuleStatus::Active;
    bool autoSign = false;  // eligible for signing without a click (see engine)
    std::string note;
    int64_t createdAt = 0;
    int64_t lastUsedAt = 0;
    uint64_t useCount = 0;
    // Set when a pin check fails so the UI can show old vs new.
    std::string observedCodeHash;
    std::string observedAbiHash;

    bool matchesIdentity(const std::string& chain, const std::string& signerActor,
                         const std::string& signerPermission, const std::string& contractName,
                         const std::string& actionName) const;

    json toJSON() const;
    static dwarfkit::Result<WhitelistRule> fromJSON(const json& j);
};

}  // namespace tb::guard
