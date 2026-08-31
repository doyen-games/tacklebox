// Port of wallet-plugin-anchor src/mode.ts. The interactive mode chooser
// (promptForMode/promptForRecovery) needs button click callbacks the C++
// PromptElement cannot carry; mode selection happens through plugin options or
// per-call arbitrary data instead (see DIVERGENCES.md).
#pragma once

#include <optional>
#include <string>

#include <dwarfkit/core/json.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit::anchor {

// Anchor transport used for login and signing: browser authenticator or
// native app.
enum class AnchorMode {
    web,
    app,
};

const char* modeString(AnchorMode mode);
std::optional<AnchorMode> parseMode(const json& value);

// Read the mode stored or inferred from a plugin's persisted data.
std::optional<AnchorMode> readMode(const json& data);
void writeMode(json& data, AnchorMode mode);
void clearMode(json& data);

// Per-call options from login arbitrary data: {"anchor": {"mode": "app"}}.
struct AnchorLoginOptions {
    std::optional<AnchorMode> mode;
};

// Read one plugin's entry out of the shared arbitrary bag. Errors on a bad
// value.
Result<AnchorLoginOptions> readLoginOptions(const std::string& id, const json& arbitrary);

// Upstream sniffs navigator.hid/navigator.usb; neither exists here.
inline bool ledgerTransportAvailable() {
    return false;
}

}  // namespace dwarfkit::anchor
