// Price-oracle response parsing, split from the service so it unit-tests
// without a network. Prices are display-only: nothing in the guard, risk or
// signing paths may ever read one.
#pragma once

#include <map>
#include <optional>
#include <string>

#include <dwarfkit/core/json.hpp>

namespace tb {

using dwarfkit::json;

// Canonical key for a priced token: "contract/SYMCODE" (code uppercased).
std::string priceKey(const std::string& contract, const std::string& symCode);

// Alcor /api/v2/tokens: array of {contract, symbol, usd_price} objects.
// Returns priceKey -> usd for every entry that parses; skips the rest.
std::map<std::string, double> parseAlcorTokens(const json& body);

// CoinGecko /api/v3/simple/price?ids=<id>&vs_currencies=usd: {id: {usd: N}}.
std::optional<double> parseCoinGecko(const json& body, const std::string& id);

// delphioracle datapoints rows (circular buffer, unordered): the median of
// the newest row, scaled by the pair's quoted precision.
std::optional<double> parseDelphiDatapoints(const json& rows, int quotedPrecision);

// "$1,234.56" / "$0.0312" / "$0.000094" - decimals scale with magnitude.
std::string formatUsd(double usd);

}  // namespace tb
