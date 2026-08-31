#include "guard/engine.hpp"

#include <algorithm>

namespace tb::guard {

const char* verdictLevelName(VerdictLevel v) {
    switch (v) {
        case VerdictLevel::TrustedAuto: return "trusted-auto";
        case VerdictLevel::Trusted: return "trusted";
        case VerdictLevel::StalePin: return "stale-pin";
        case VerdictLevel::ConstraintFail: return "constraint-fail";
        case VerdictLevel::Unlisted: return "unlisted";
    }
    return "unlisted";
}

const json* lookupField(const json& data, const std::string& path) {
    const json* cur = &data;
    size_t start = 0;
    while (start <= path.size()) {
        size_t dot = path.find('.', start);
        std::string key =
            dot == std::string::npos ? path.substr(start) : path.substr(start, dot - start);
        if (!cur->is_object() || !cur->contains(key)) return nullptr;
        cur = &(*cur)[key];
        if (dot == std::string::npos) return cur;
        start = dot + 1;
    }
    return nullptr;
}

namespace {

// Severity order for picking the transaction-wide verdict: the weakest action
// dominates. StalePin outranks ConstraintFail in urgency (integrity alarm).
int weakness(VerdictLevel v) {
    switch (v) {
        case VerdictLevel::TrustedAuto: return 0;
        case VerdictLevel::Trusted: return 1;
        case VerdictLevel::Unlisted: return 2;
        case VerdictLevel::ConstraintFail: return 3;
        case VerdictLevel::StalePin: return 4;
    }
    return 4;
}

struct RuleOutcome {
    enum Kind { Pass, Stale, ParamFail } kind = ParamFail;
    std::string detail;
    std::string observedCode, observedAbi;
};

RuleOutcome applyRule(const WhitelistRule& rule, const ActionInput& action) {
    RuleOutcome out;

    // Integrity pin first: a changed contract voids every other judgment.
    if (rule.pin) {
        if (action.codeHash.empty() || action.abiHash.empty()) {
            out.kind = RuleOutcome::Stale;
            out.detail =
                "could not verify the contract's current code hash; refusing the fast path";
            return out;
        }
        if (action.codeHash != rule.pin->codeHash || action.abiHash != rule.pin->abiHash) {
            out.kind = RuleOutcome::Stale;
            out.detail = "contract changed since approval (code " +
                         rule.pin->codeHash.substr(0, 12) + " -> " + action.codeHash.substr(0, 12) +
                         ", abi " + rule.pin->abiHash.substr(0, 12) + " -> " +
                         action.abiHash.substr(0, 12) + ")";
            out.observedCode = action.codeHash;
            out.observedAbi = action.abiHash;
            return out;
        }
    }

    // Every constrained field must pass.
    for (const auto& [path, constraint] : rule.params) {
        const json* value = lookupField(action.data, path);
        if (!value) {
            if (constraint.kind == ConstraintKind::Any) continue;
            out.kind = RuleOutcome::ParamFail;
            out.detail = "field '" + path + "' missing from action data";
            return out;
        }
        std::string why;
        if (!constraint.check(*value, why)) {
            out.kind = RuleOutcome::ParamFail;
            out.detail = "field '" + path + "': " + why;
            return out;
        }
    }

    // Strict rules reject fields they have no entry for.
    if (!rule.unlistedParamsAny && action.data.is_object()) {
        for (auto it = action.data.begin(); it != action.data.end(); ++it) {
            if (!rule.params.count(it.key())) {
                out.kind = RuleOutcome::ParamFail;
                out.detail = "field '" + it.key() + "' is not covered by this strict rule";
                return out;
            }
        }
    }

    out.kind = RuleOutcome::Pass;
    return out;
}

}  // namespace

Evaluation evaluate(const std::vector<WhitelistRule>& rules, const std::string& chainId,
                    const std::string& signerActor, const std::string& signerPermission,
                    const std::vector<ActionInput>& actions) {
    Evaluation eval;
    eval.actions.reserve(actions.size());

    for (const auto& action : actions) {
        // Candidates, exact-action rules before wildcard-action rules.
        std::vector<const WhitelistRule*> candidates;
        for (const auto& rule : rules) {
            if (rule.status == RuleStatus::Disabled) continue;
            if (rule.matchesIdentity(chainId, signerActor, signerPermission, action.contract,
                                     action.action))
                candidates.push_back(&rule);
        }
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const WhitelistRule* a, const WhitelistRule* b) {
                             return (a->action != "*") > (b->action != "*");
                         });

        ActionVerdict verdict;
        bool sawStale = false, sawParamFail = false;
        for (const WhitelistRule* rule : candidates) {
            RuleOutcome outcome = applyRule(*rule, action);
            if (outcome.kind == RuleOutcome::Pass) {
                // A rule persisted as Stale must be re-approved even if the
                // hashes drifted back: the user has not confirmed the change.
                if (rule->status == RuleStatus::Stale) {
                    verdict.level = VerdictLevel::StalePin;
                    verdict.ruleId = rule->id;
                    verdict.ruleNote = rule->note;
                    verdict.detail =
                        "rule was invalidated by a contract update and needs re-approval";
                    sawStale = true;
                    break;
                }
                verdict.level =
                    rule->autoSign ? VerdictLevel::TrustedAuto : VerdictLevel::Trusted;
                verdict.ruleId = rule->id;
                verdict.ruleNote = rule->note;
                verdict.detail.clear();
                eval.usedRuleIds.push_back(rule->id);
                break;
            }
            if (outcome.kind == RuleOutcome::Stale) {
                sawStale = true;
                if (verdict.ruleId.empty() || verdict.level != VerdictLevel::StalePin) {
                    verdict.level = VerdictLevel::StalePin;
                    verdict.ruleId = rule->id;
                    verdict.ruleNote = rule->note;
                    verdict.detail = outcome.detail;
                }
                if (!outcome.observedCode.empty())
                    eval.stale.push_back({rule->id, outcome.observedCode, outcome.observedAbi});
                continue;
            }
            // ParamFail: remember the first explanation, keep trying others.
            sawParamFail = true;
            if (verdict.ruleId.empty()) {
                verdict.level = VerdictLevel::ConstraintFail;
                verdict.ruleId = rule->id;
                verdict.ruleNote = rule->note;
                verdict.detail = outcome.detail;
            }
        }

        if (verdict.level != VerdictLevel::Trusted && verdict.level != VerdictLevel::TrustedAuto) {
            if (!sawStale && !sawParamFail) {
                verdict.level = VerdictLevel::Unlisted;
                verdict.detail = "no whitelist rule covers this action";
            }
        }
        eval.actions.push_back(std::move(verdict));
    }

    // Overall: the weakest action wins. Empty transactions never fast-path.
    eval.overall = VerdictLevel::Unlisted;
    if (!eval.actions.empty()) {
        eval.overall = VerdictLevel::TrustedAuto;
        for (const auto& v : eval.actions)
            if (weakness(v.level) > weakness(eval.overall)) eval.overall = v.level;
    }
    return eval;
}

}  // namespace tb::guard
