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

// Core-token position from a raw get_account response: liquid (the primary
// value everywhere), self stake, stake delegated to others, pending refund,
// and their sum. All strings are formatted assets in the core symbol.
struct StakeBreakdown {
    bool any = false;             // false: chain exposes no staking fields
    std::string available;        // core_liquid_balance ("0" asset if absent)
    std::string stakedSelf;       // self_delegated_bandwidth cpu+net
    std::string stakedDelegated;  // voter_info.staked minus self stake
    std::string refunding;        // refund_request cpu+net
    std::string total;            // available + all of the above
};
StakeBreakdown stakeBreakdown(const json& raw, const std::string& coreSymbol /*"4,EOS"*/);

// "20740.80682753 WAX" -> "20740.8068 WAX": trims the fraction to at most
// `maxDecimals` digits for display, truncating rather than rounding so a
// balance never reads higher than what the chain holds. Strings without a
// fraction, or with one already short enough, pass through unchanged.
std::string displayAsset(const std::string& asset, int maxDecimals = 4);

}  // namespace tb::acct
