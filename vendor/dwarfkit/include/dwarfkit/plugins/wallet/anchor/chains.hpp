// Port of wallet-plugin-anchor src/chains.ts.
#pragma once

#include <map>
#include <optional>
#include <string>

#include <dwarfkit/antelope/chain/checksum.hpp>

namespace dwarfkit::anchor {

// Built-in web-authenticator URLs keyed by Antelope chain ID.
const std::map<std::string, std::string>& DEFAULT_WEB_AUTHENTICATOR_URLS();

// Resolve a chain's web authenticator URL: overrides first, then defaults,
// case-insensitive on the chain id, trailing slashes trimmed. Unsupported
// chains (and no chain) resolve to nullopt.
std::optional<std::string> resolveWebAuthenticatorUrl(
    const std::optional<std::string>& chainId,
    const std::map<std::string, std::string>& overrides = {});

}  // namespace dwarfkit::anchor
