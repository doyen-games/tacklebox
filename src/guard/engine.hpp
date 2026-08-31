// Whitelist evaluation. Pure logic: the caller supplies decoded actions plus
// freshly fetched contract hashes; the engine returns a verdict per action and
// which rules went stale. Persisting stale status and prompting for
// re-approval is the controller's job.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "guard/rules.hpp"

namespace tb::guard {

// One action of a transaction as the engine sees it.
struct ActionInput {
    std::string contract;
    std::string action;
    json data;  // decoded (JSON) action parameters
    // Current on-chain hashes for `contract` (hex). Empty when the fetch
    // failed; pinned rules then refuse to fast-path.
    std::string codeHash;
    std::string abiHash;
};

enum class VerdictLevel {
    TrustedAuto,     // whitelisted, rule allows signing without a click
    Trusted,         // whitelisted, still one-click confirm
    StalePin,        // rule matched but contract code/ABI changed: BLOCKED until re-approved
    ConstraintFail,  // rule matched name-wise but a parameter is out of bounds
    Unlisted,        // no rule covers this action
};

const char* verdictLevelName(VerdictLevel v);

struct ActionVerdict {
    VerdictLevel level = VerdictLevel::Unlisted;
    std::string ruleId;    // matched (or best-failing) rule, if any
    std::string ruleNote;
    std::string detail;    // human-readable reason (which field failed, hash diff)
};

struct StaleObservation {
    std::string ruleId;
    std::string observedCodeHash;
    std::string observedAbiHash;
};

struct Evaluation {
    std::vector<ActionVerdict> actions;
    // Weakest level across actions decides the flow the UI takes.
    VerdictLevel overall = VerdictLevel::Unlisted;
    // Rules whose pins no longer match; controller persists Stale status.
    std::vector<StaleObservation> stale;
    // Rules that fully matched; controller bumps use counters.
    std::vector<std::string> usedRuleIds;
};

Evaluation evaluate(const std::vector<WhitelistRule>& rules, const std::string& chainId,
                    const std::string& signerActor, const std::string& signerPermission,
                    const std::vector<ActionInput>& actions);

// Field lookup used for constraint paths ("to", "deposit.quantity").
const json* lookupField(const json& data, const std::string& path);

}  // namespace tb::guard
