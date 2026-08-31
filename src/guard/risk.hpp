// Always-on static risk analysis of decoded actions, independent of the
// whitelist. Flags feed the signing modal so the human sees what a
// transaction actually does before approving it.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <dwarfkit/core/json.hpp>

namespace tb::guard {

enum class RiskSeverity { Info, Warn, Critical };

const char* riskSeverityName(RiskSeverity s);

struct RiskFlag {
    RiskSeverity severity;
    std::string code;     // stable identifier ("perm-change", "large-transfer")
    std::string message;  // human-readable
    size_t actionIndex;   // which action triggered it
};

struct RiskContext {
    std::string signerActor;
    // Core-token balance of the signer, e.g. "123.4567 EOS", if known.
    std::optional<std::string> coreBalance;
    // Contracts the signer has interacted with before (rules + audit log).
    std::set<std::string> knownContracts;
};

struct RiskActionInput {
    std::string contract;
    std::string action;
    dwarfkit::json data;
};

std::vector<RiskFlag> analyze(const std::vector<RiskActionInput>& actions,
                              const RiskContext& context);

}  // namespace tb::guard
