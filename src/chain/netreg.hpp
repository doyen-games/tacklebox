// Built-in network presets. Identity data (chain id, explorer, system token)
// comes from dwarfkit's chain catalog; TackleBox adds fallback endpoints and
// the default enabled set. Everything is user-editable once copied into the
// vault, this is only the factory state.
#pragma once

#include <vector>

#include "vault/vault.hpp"

namespace tb {

// Factory network list for a new vault (mainnets + Jungle 4 testnet).
std::vector<NetworkDef> defaultNetworks();

// All presets available in "add network" (includes extra testnets).
std::vector<NetworkDef> allPresets();

// Endpoint URL sanity check: https (or http://localhost) and no spaces.
bool endpointAllowed(const std::string& url, std::string* why = nullptr);

// Sensible oracle url/coreId for a provider on a known chain (empty fields
// for unknown chains - the user fills them in). Used to seed presets and to
// prefill the Settings editor when the provider changes.
OracleConfig oracleDefaults(const std::string& chainId, OracleProvider provider);

}  // namespace tb
