#pragma once

#include <nlohmann/json.hpp>

namespace dwarfkit {

// Plays the role of plain JS objects everywhere Wharfkit accepts or returns
// one. ordered_json keeps insertion order for stringify parity.
using json = nlohmann::ordered_json;

}  // namespace dwarfkit
