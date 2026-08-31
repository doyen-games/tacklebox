// Account-creation building blocks, kept pure so the sorting and validation
// rules (which the chain enforces byte-for-byte) are unit-testable.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <dwarfkit/core/json.hpp>

namespace tb::acct {

using dwarfkit::json;

// One weighted key / account entry of a permission draft.
struct KeyEntry {
    std::string pub;  // PUB_K1_... or legacy EOS...
    int weight = 1;
};
struct AccountEntry {
    std::string actor;
    std::string permission;  // "active", "eosio.code", ...
    int weight = 1;
};

struct AuthorityDraft {
    int threshold = 1;
    std::vector<KeyEntry> keys;
    std::vector<AccountEntry> accounts;
};

// "" when the name is a valid 12-char account; otherwise the reason. Names
// shorter than 12 chars are valid but need a suffix auth - `premium` flags it.
std::string validateAccountName(const std::string& name, bool* premium = nullptr);

// eosio `authority` json with keys and accounts in the chain-required sort
// order (keys ascending by normalized key, accounts by actor then permission
// name value). Empty when a key/actor fails to parse or weights cannot reach
// the threshold.
std::optional<json> makeAuthority(const AuthorityDraft& draft, std::string* why = nullptr);

// "1.25" + "8,WAX" -> "1.25000000 WAX". Empty/invalid/negative -> nullopt;
// zero stays formattable (callers skip zero stakes).
std::optional<std::string> formatStake(const std::string& text, const std::string& coreSymbol);

}  // namespace tb::acct
