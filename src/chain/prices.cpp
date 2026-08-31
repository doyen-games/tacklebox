#include "chain/prices.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace tb {

namespace {

// Providers ship numbers as numbers or strings; accept both.
std::optional<double> asDouble(const json& value) {
    if (value.is_number()) return value.get<double>();
    if (value.is_string()) {
        const std::string& s = value.get_ref<const std::string&>();
        char* end = nullptr;
        double parsed = std::strtod(s.c_str(), &end);
        if (end != s.c_str() && std::isfinite(parsed)) return parsed;
    }
    return std::nullopt;
}

}  // namespace

std::string priceKey(const std::string& contract, const std::string& symCode) {
    std::string code = symCode;
    for (char& c : code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return contract + "/" + code;
}

std::map<std::string, double> parseAlcorTokens(const json& body) {
    std::map<std::string, double> prices;
    if (!body.is_array()) return prices;
    for (const json& entry : body) {
        if (!entry.is_object()) continue;
        std::string contract = entry.value("contract", "");
        std::string symbol = entry.value("symbol", "");
        if (contract.empty() || symbol.empty() || !entry.contains("usd_price")) continue;
        if (auto usd = asDouble(entry["usd_price"]); usd && *usd > 0)
            prices[priceKey(contract, symbol)] = *usd;
    }
    return prices;
}

std::optional<double> parseCoinGecko(const json& body, const std::string& id) {
    if (!body.is_object() || !body.contains(id) || !body[id].is_object() ||
        !body[id].contains("usd"))
        return std::nullopt;
    auto usd = asDouble(body[id]["usd"]);
    if (usd && *usd > 0) return usd;
    return std::nullopt;
}

std::optional<double> parseDelphiDatapoints(const json& rows, int quotedPrecision) {
    if (!rows.is_array() || rows.empty() || quotedPrecision < 0 || quotedPrecision > 18)
        return std::nullopt;
    // The table is a circular buffer updated in place; pick the newest row by
    // its ISO timestamp (lexicographic order is chronological).
    const json* newest = nullptr;
    std::string newestStamp;
    for (const json& row : rows) {
        if (!row.is_object() || !row.contains("median")) continue;
        std::string stamp = row.value("timestamp", "");
        if (!newest || stamp > newestStamp) {
            newest = &row;
            newestStamp = std::move(stamp);
        }
    }
    if (!newest) return std::nullopt;
    auto median = asDouble((*newest)["median"]);
    if (!median || *median <= 0) return std::nullopt;
    return *median / std::pow(10.0, quotedPrecision);
}

std::string formatUsd(double usd) {
    if (!std::isfinite(usd) || usd <= 0) return "-";
    char buf[48];
    if (usd >= 1000) {
        // Thousands separator for large values, 2 decimals.
        std::snprintf(buf, sizeof buf, "%.2f", usd);
        std::string digits = buf;
        auto dot = digits.find('.');
        for (int i = static_cast<int>(dot) - 3; i > 0; i -= 3)
            digits.insert(static_cast<size_t>(i), ",");
        return "$" + digits;
    }
    if (usd >= 1)
        std::snprintf(buf, sizeof buf, "$%.2f", usd);
    else if (usd >= 0.01)
        std::snprintf(buf, sizeof buf, "$%.4f", usd);
    else
        std::snprintf(buf, sizeof buf, "$%.6f", usd);
    return buf;
}

}  // namespace tb
