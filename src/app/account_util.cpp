#include "app/account_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <dwarfkit/antelope/chain/name.hpp>
#include <dwarfkit/antelope/chain/public_key.hpp>

namespace tb::acct {

std::string validateAccountName(const std::string& name, bool* premium) {
    if (premium) *premium = false;
    if (name.empty()) return "enter an account name";
    if (name.size() > 12) return "account names are at most 12 characters";
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '1' && c <= '5') || c == '.'))
            return "only a-z, 1-5 and . are allowed";
    if (name.front() == '.' || name.back() == '.') return "names cannot start or end with .";
    // Round-trip through Name to catch anything the encoding mangles.
    if (dwarfkit::Name::from(name).toString() != name) return "not a valid account name";
    if (premium && (name.size() < 12 || name.find('.') != std::string::npos))
        *premium = true;  // needs a bid or a suffix owner to create
    return "";
}

std::optional<json> makeAuthority(const AuthorityDraft& draft, std::string* why) {
    auto fail = [&](const std::string& reason) -> std::optional<json> {
        if (why) *why = reason;
        return std::nullopt;
    };
    if (draft.threshold < 1) return fail("threshold must be at least 1");
    if (draft.keys.empty() && draft.accounts.empty())
        return fail("an authority needs at least one key or account");

    // Normalize + sort keys ascending by their canonical string (the chain
    // rejects unsorted or duplicate entries).
    std::vector<std::pair<std::string, int>> keys;
    for (const auto& entry : draft.keys) {
        auto parsed = dwarfkit::PublicKey::from(entry.pub);
        if (!parsed) return fail("invalid public key: " + entry.pub);
        if (entry.weight < 1 || entry.weight > 65535)
            return fail("key weights must be 1..65535");
        keys.push_back({parsed->toString(), entry.weight});
    }
    std::sort(keys.begin(), keys.end());
    for (size_t i = 1; i < keys.size(); ++i)
        if (keys[i].first == keys[i - 1].first) return fail("duplicate key in authority");

    // Accounts ascending by (actor, permission) name values.
    struct Acct {
        uint64_t actor, perm;
        std::string actorStr, permStr;
        int weight;
    };
    std::vector<Acct> accounts;
    for (const auto& entry : draft.accounts) {
        dwarfkit::Name actor = dwarfkit::Name::from(entry.actor);
        dwarfkit::Name perm = dwarfkit::Name::from(entry.permission);
        if (actor.toString() != entry.actor)
            return fail("invalid authority account: " + entry.actor);
        if (perm.toString() != entry.permission)
            return fail("invalid permission name: " + entry.permission);
        if (entry.weight < 1 || entry.weight > 65535)
            return fail("account weights must be 1..65535");
        accounts.push_back(
            {actor.value, perm.value, entry.actor, entry.permission, entry.weight});
    }
    std::sort(accounts.begin(), accounts.end(), [](const Acct& a, const Acct& b) {
        return a.actor != b.actor ? a.actor < b.actor : a.perm < b.perm;
    });
    for (size_t i = 1; i < accounts.size(); ++i)
        if (accounts[i].actor == accounts[i - 1].actor &&
            accounts[i].perm == accounts[i - 1].perm)
            return fail("duplicate account in authority");

    // The threshold must be reachable, or the permission can never sign.
    long long reachable = 0;
    for (const auto& [pub, weight] : keys) reachable += weight;
    for (const auto& acct : accounts) reachable += acct.weight;
    if (reachable < draft.threshold)
        return fail("weights sum to " + std::to_string(reachable) +
                    ", below the threshold of " + std::to_string(draft.threshold));

    json keysJson = json::array();
    for (const auto& [pub, weight] : keys)
        keysJson.push_back({{"key", pub}, {"weight", weight}});
    json accountsJson = json::array();
    for (const auto& acct : accounts)
        accountsJson.push_back(
            {{"permission", {{"actor", acct.actorStr}, {"permission", acct.permStr}}},
             {"weight", acct.weight}});
    return json{{"threshold", draft.threshold},
                {"keys", keysJson},
                {"accounts", accountsJson},
                {"waits", json::array()}};
}

std::optional<std::string> formatStake(const std::string& text, const std::string& coreSymbol) {
    auto comma = coreSymbol.find(',');
    if (comma == std::string::npos) return std::nullopt;
    int precision = std::atoi(coreSymbol.substr(0, comma).c_str());
    std::string code = coreSymbol.substr(comma + 1);
    if (precision < 0 || precision > 18 || code.empty()) return std::nullopt;

    const char* start = text.c_str();
    char* end = nullptr;
    double amount = std::strtod(start, &end);
    if (end == start || !std::isfinite(amount) || amount < 0) return std::nullopt;
    while (*end == ' ') ++end;
    if (*end != '\0' && code != end) return std::nullopt;  // stray trailing text

    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f %s", precision, amount, code.c_str());
    return std::string(buf);
}

}  // namespace tb::acct
