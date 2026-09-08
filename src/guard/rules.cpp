#include "guard/rules.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>

#include "core/util.hpp"

namespace tb::guard {

const char* constraintKindName(ConstraintKind k) {
    switch (k) {
        case ConstraintKind::Any: return "any";
        case ConstraintKind::Exact: return "exact";
        case ConstraintKind::OneOf: return "one-of";
        case ConstraintKind::Range: return "range";
    }
    return "any";
}

const char* ruleStatusName(RuleStatus s) {
    switch (s) {
        case RuleStatus::Active: return "active";
        case RuleStatus::Stale: return "stale";
        case RuleStatus::Disabled: return "disabled";
    }
    return "active";
}

// --- value comparison -------------------------------------------------------

static std::optional<double> asNumber(const json& v) {
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        const auto& s = v.get_ref<const std::string&>();
        if (s.empty()) return std::nullopt;
        double out{};
        if (parseDouble(s, out)) return out;
    }
    if (v.is_boolean()) return v.get<bool>() ? 1.0 : 0.0;
    return std::nullopt;
}

std::optional<AssetValue> parseAsset(const std::string& s) {
    // "12.3456 SYM": integer part, optional fraction, single space, 1-7 A-Z.
    size_t space = s.find(' ');
    if (space == std::string::npos || space == 0 || space + 1 >= s.size()) return std::nullopt;
    std::string amountStr = s.substr(0, space);
    std::string code = s.substr(space + 1);
    if (code.empty() || code.size() > 7) return std::nullopt;
    for (char c : code)
        if (c < 'A' || c > 'Z') return std::nullopt;

    bool negative = false;
    size_t i = 0;
    if (amountStr[0] == '-') {
        negative = true;
        i = 1;
    }
    int64_t units = 0;
    uint8_t precision = 0;
    bool inFraction = false;
    bool sawDigit = false;
    for (; i < amountStr.size(); ++i) {
        char c = amountStr[i];
        if (c == '.') {
            if (inFraction) return std::nullopt;
            inFraction = true;
            continue;
        }
        if (c < '0' || c > '9') return std::nullopt;
        sawDigit = true;
        if (units > (INT64_MAX - 9) / 10) return std::nullopt;  // overflow guard
        units = units * 10 + (c - '0');
        if (inFraction) {
            if (++precision > 18) return std::nullopt;
        }
    }
    if (!sawDigit) return std::nullopt;
    return AssetValue{negative ? -units : units, precision, code};
}

bool jsonEquiv(const json& a, const json& b) {
    if (a == b) return true;
    // Serializer emits >32-bit integers as strings; a user typing 100 should
    // match "100" coming off the wire (and vice versa).
    auto na = asNumber(a);
    auto nb = asNumber(b);
    if (na && nb) return *na == *nb;
    return false;
}

// Compare a value against a bound. Handles assets (symbol must agree) and
// numbers. Returns nullopt when the pair is not comparable.
static std::optional<int> compareForRange(const json& value, const json& bound,
                                          std::string& why) {
    if (value.is_string() && bound.is_string()) {
        auto va = parseAsset(value.get<std::string>());
        auto ba = parseAsset(bound.get<std::string>());
        if (va && ba) {
            if (va->code != ba->code || va->precision != ba->precision) {
                why = "asset symbol mismatch (" + value.get<std::string>() + " vs bound " +
                      bound.get<std::string>() + ")";
                return std::nullopt;
            }
            if (va->amount < ba->amount) return -1;
            if (va->amount > ba->amount) return 1;
            return 0;
        }
    }
    auto nv = asNumber(value);
    auto nb = asNumber(bound);
    if (nv && nb) {
        if (*nv < *nb) return -1;
        if (*nv > *nb) return 1;
        return 0;
    }
    why = "value is not comparable to the configured range";
    return std::nullopt;
}

bool ParamConstraint::check(const json& value, std::string& why) const {
    switch (kind) {
        case ConstraintKind::Any:
            return true;
        case ConstraintKind::Exact:
            if (values.empty()) {
                why = "rule has no expected value";
                return false;
            }
            if (jsonEquiv(value, values[0])) return true;
            why = "expected " + values[0].dump() + ", got " + value.dump();
            return false;
        case ConstraintKind::OneOf: {
            for (const auto& v : values)
                if (jsonEquiv(value, v)) return true;
            why = "value " + value.dump() + " is not in the approved set";
            return false;
        }
        case ConstraintKind::Range: {
            if (min) {
                auto c = compareForRange(value, *min, why);
                if (!c) return false;
                if (*c < 0) {
                    why = "value " + value.dump() + " is below the approved minimum " +
                          min->dump();
                    return false;
                }
            }
            if (max) {
                auto c = compareForRange(value, *max, why);
                if (!c) return false;
                if (*c > 0) {
                    why = "value " + value.dump() + " is above the approved maximum " +
                          max->dump();
                    return false;
                }
            }
            return true;
        }
    }
    why = "unknown constraint";
    return false;
}

static std::string valueBrief(const json& v) {
    std::string s = v.is_string() ? v.get<std::string>() : v.dump();
    if (s.size() > 24) s = s.substr(0, 21) + "...";
    return s;
}

std::string ParamConstraint::describe() const {
    switch (kind) {
        case ConstraintKind::Any:
            return "any";
        case ConstraintKind::Exact:
            return values.empty() ? "= ?" : "= " + valueBrief(values[0]);
        case ConstraintKind::OneOf: {
            std::string out = "in {";
            for (size_t i = 0; i < values.size() && i < 3; ++i) {
                if (i) out += ", ";
                out += valueBrief(values[i]);
            }
            if (values.size() > 3) out += ", ...";
            return out + "}";
        }
        case ConstraintKind::Range: {
            std::string lo = min ? valueBrief(*min) : "-inf";
            std::string hi = max ? valueBrief(*max) : "inf";
            return lo + " .. " + hi;
        }
    }
    return "any";
}

json ParamConstraint::toJSON() const {
    json j{{"kind", constraintKindName(kind)}};
    if (!values.empty()) j["values"] = values;
    if (min) j["min"] = *min;
    if (max) j["max"] = *max;
    return j;
}

ParamConstraint ParamConstraint::fromJSON(const json& j) {
    ParamConstraint c;
    std::string kind = j.value("kind", "any");
    if (kind == "exact") c.kind = ConstraintKind::Exact;
    else if (kind == "one-of") c.kind = ConstraintKind::OneOf;
    else if (kind == "range") c.kind = ConstraintKind::Range;
    else c.kind = ConstraintKind::Any;
    if (j.contains("values") && j["values"].is_array())
        c.values = j["values"].get<std::vector<json>>();
    if (j.contains("min")) c.min = j["min"];
    if (j.contains("max")) c.max = j["max"];
    return c;
}

// --- rule -------------------------------------------------------------------

static bool globEq(const std::string& pattern, const std::string& value) {
    return pattern == "*" || pattern == value;
}

bool WhitelistRule::matchesIdentity(const std::string& chain, const std::string& signerActor,
                                    const std::string& signerPermission,
                                    const std::string& contractName,
                                    const std::string& actionName) const {
    if (!globEq(chainId, chain)) return false;
    std::string ruleActor = "*", rulePerm = "*";
    if (size_t at = signer.find('@'); at != std::string::npos) {
        ruleActor = signer.substr(0, at);
        rulePerm = signer.substr(at + 1);
    } else if (!signer.empty()) {
        ruleActor = signer;
    }
    if (!globEq(ruleActor, signerActor)) return false;
    if (!globEq(rulePerm, signerPermission)) return false;
    if (contract != contractName) return false;  // never wildcard the contract
    if (!globEq(action, actionName)) return false;
    return true;
}

json WhitelistRule::toJSON() const {
    json p = json::object();
    for (const auto& [k, v] : params) p[k] = v.toJSON();
    json j{{"id", id},
           {"chain", chainId},
           {"signer", signer},
           {"contract", contract},
           {"action", action},
           {"params", p},
           {"unlistedParamsAny", unlistedParamsAny},
           {"status", ruleStatusName(status)},
           {"autoSign", autoSign},
           {"note", note},
           {"createdAt", createdAt},
           {"lastUsedAt", lastUsedAt},
           {"useCount", useCount}};
    if (pin)
        j["pin"] = json{{"codeHash", pin->codeHash},
                        {"abiHash", pin->abiHash},
                        {"pinnedAt", pin->pinnedAt}};
    if (!observedCodeHash.empty()) j["observedCodeHash"] = observedCodeHash;
    if (!observedAbiHash.empty()) j["observedAbiHash"] = observedAbiHash;
    return j;
}

dwarfkit::Result<WhitelistRule> WhitelistRule::fromJSON(const json& j) {
    using dwarfkit::ErrorKind;
    if (!j.is_object()) return dwarfkit::err(ErrorKind::Invalid, "whitelist rule is not an object");
    WhitelistRule r;
    r.id = j.value("id", "");
    r.chainId = j.value("chain", "*");
    r.signer = j.value("signer", "*@*");
    r.contract = j.value("contract", "");
    r.action = j.value("action", "*");
    if (r.id.empty() || r.contract.empty())
        return dwarfkit::err(ErrorKind::Invalid, "whitelist rule missing id or contract");
    if (j.contains("params") && j["params"].is_object())
        for (auto it = j["params"].begin(); it != j["params"].end(); ++it)
            r.params[it.key()] = ParamConstraint::fromJSON(it.value());
    r.unlistedParamsAny = j.value("unlistedParamsAny", true);
    std::string status = j.value("status", "active");
    r.status = status == "stale" ? RuleStatus::Stale
               : status == "disabled" ? RuleStatus::Disabled
                                      : RuleStatus::Active;
    r.autoSign = j.value("autoSign", false);
    r.note = j.value("note", "");
    r.createdAt = j.value("createdAt", int64_t(0));
    r.lastUsedAt = j.value("lastUsedAt", int64_t(0));
    r.useCount = j.value("useCount", uint64_t(0));
    if (j.contains("pin") && j["pin"].is_object()) {
        ContractPin pin;
        pin.codeHash = j["pin"].value("codeHash", "");
        pin.abiHash = j["pin"].value("abiHash", "");
        pin.pinnedAt = j["pin"].value("pinnedAt", int64_t(0));
        r.pin = pin;
    }
    r.observedCodeHash = j.value("observedCodeHash", "");
    r.observedAbiHash = j.value("observedAbiHash", "");
    return r;
}

}  // namespace tb::guard
