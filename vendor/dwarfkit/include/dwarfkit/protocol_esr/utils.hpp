// Port of protocol-esr src/utils.ts. The browser sniffing helpers behind
// generateReturnUrl have no meaning outside a browser; it returns nullopt on
// native (see DIVERGENCES.md).
#pragma once

#include <functional>
#include <optional>
#include <string>

namespace dwarfkit {

// PascalCase version of a snake_case string.
std::string snakeToPascal(const std::string& name);

// camelCase version of a snake_case string.
std::string snakeToCamel(const std::string& name);

// Warning sink used by logWarn; swap it to capture output (defaults to
// stderr), mirroring how the upstream test swaps console.warn.
std::function<void(const std::string&)>& logWarnSink();

// Print a warning message, prefixed "[anchor-link]" like upstream.
void logWarn(const std::string& message);

// Generate a UUID (v4 layout, CSPRNG-backed).
std::string uuid();

// A return url that Anchor would redirect back to; native builds have no
// browser location, so this is always nullopt.
std::optional<std::string> generateReturnUrl();

}  // namespace dwarfkit
