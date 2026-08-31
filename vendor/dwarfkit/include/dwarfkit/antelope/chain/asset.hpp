// Port of antelope src/chain/asset.ts
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <dwarfkit/antelope/chain/name.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

namespace detail {

constexpr uint64_t toRawSymbolCode(std::string_view name) {
    // truncates to 7 characters exactly like upstream
    uint64_t raw = 0;
    const size_t length = name.size() < 7 ? name.size() : 7;
    for (size_t i = 0; i < length; i++) {
        raw |= static_cast<uint64_t>(static_cast<uint8_t>(name[i])) << (8 * i);
    }
    return raw;
}

constexpr uint64_t toRawSymbol(std::string_view name, uint8_t precision) {
    return (toRawSymbolCode(name) << 8) | precision;
}

constexpr bool isValidSymbolName(uint64_t code) {
    // ^[A-Z]{0,7}$ over the bytes of the code, matching toSymbolName + pattern
    while (code) {
        const char c = static_cast<char>(code & 0xff);
        if (c < 'A' || c > 'Z') return false;
        code >>= 8;
    }
    return true;
}

void invalidAssetLiteral();

}  // namespace detail

class Asset {
public:
    static constexpr std::string_view abiName = "asset";

    class SymbolCode {
    public:
        static constexpr std::string_view abiName = "symbol_code";
        static constexpr std::string_view pattern = "^[A-Z]{0,7}$";

        uint64_t value = 0;

        constexpr SymbolCode() = default;
        constexpr explicit SymbolCode(uint64_t value) : value(value) {}

        static Result<SymbolCode> from(uint64_t value);
        static Result<SymbolCode> from(std::string_view name);
        static SymbolCode abiDefault() { return SymbolCode(detail::toRawSymbolCode("SYS")); }

        constexpr bool equals(const SymbolCode& other) const { return value == other.value; }
        constexpr bool operator==(const SymbolCode&) const = default;

        std::string toString() const;
        json toJSON() const { return toString(); }
    };

    class Symbol {
    public:
        static constexpr std::string_view abiName = "symbol";
        static constexpr int maxPrecision = 18;

        uint64_t value = 0;

        constexpr Symbol() = default;
        constexpr explicit Symbol(uint64_t value) : value(value) {}

        static Result<Symbol> from(uint64_t value);
        static Result<Symbol> from(std::string_view value);
        static Result<Symbol> fromParts(std::string_view name, int precision);
        static Symbol abiDefault() { return Symbol(detail::toRawSymbol("SYS", 4)); }

        constexpr bool equals(const Symbol& other) const { return value == other.value; }
        constexpr bool operator==(const Symbol&) const = default;

        std::string name() const;
        constexpr int precision() const { return static_cast<int>(value & 0xff); }
        constexpr SymbolCode code() const { return SymbolCode(value >> 8); }

        // Convert units to floating point number according to symbol precision.
        // Errors if the given units can't be represented in 53 bits.
        Result<double> convertUnits(int64_t units) const;

        // Convert floating point to units according to symbol precision.
        // Note that the value will be rounded to closest precision.
        Result<int64_t> convertFloat(double value) const;

        std::string toString() const;
        json toJSON() const { return toString(); }
    };

    int64_t units = 0;
    Symbol symbol;

    constexpr Asset() = default;
    constexpr Asset(int64_t units, Symbol symbol) : units(units), symbol(symbol) {}

    static Result<Asset> from(std::string_view value);
    static Result<Asset> from(double value, const Symbol& symbol);
    static Result<Asset> from(double value, std::string_view symbol);
    static Result<Asset> fromString(std::string_view value);
    static Result<Asset> fromFloat(double value, const Symbol& symbol);
    static constexpr Asset fromUnits(int64_t value, const Symbol& symbol) {
        return Asset(value, symbol);
    }
    static Result<Asset> fromUnits(int64_t value, std::string_view symbol);
    static Asset abiDefault() { return Asset(0, Symbol::abiDefault()); }

    static std::string formatUnits(int64_t units, int precision);

    constexpr bool equals(const Asset& other) const {
        return symbol.value == other.symbol.value && units == other.units;
    }
    constexpr bool operator==(const Asset&) const = default;

    // Unlike upstream's value getter this cannot error; the strict 53-bit
    // behavior lives in Symbol::convertUnits.
    double value() const;
    Result<void> setValue(double newValue);

    std::string quantity() const { return formatUnits(units, symbol.precision()); }

    std::string toString() const { return quantity() + " " + symbol.name(); }
    json toJSON() const { return toString(); }
};

class ExtendedAsset {
public:
    static constexpr std::string_view abiName = "extended_asset";

    Asset quantity;
    Name contract;

    constexpr ExtendedAsset() = default;
    constexpr ExtendedAsset(Asset quantity, Name contract)
        : quantity(quantity), contract(contract) {}

    static constexpr ExtendedAsset from(Asset quantity, Name contract) {
        return {quantity, contract};
    }

    constexpr bool equals(const ExtendedAsset& other) const {
        return quantity == other.quantity && contract == other.contract;
    }
    constexpr bool operator==(const ExtendedAsset&) const = default;

    json toJSON() const { return {{"quantity", quantity.toJSON()}, {"contract", contract.toJSON()}}; }
};

class ExtendedSymbol {
public:
    static constexpr std::string_view abiName = "extended_symbol";

    Asset::Symbol sym;
    Name contract;

    constexpr ExtendedSymbol() = default;
    constexpr ExtendedSymbol(Asset::Symbol sym, Name contract) : sym(sym), contract(contract) {}

    static constexpr ExtendedSymbol from(Asset::Symbol sym, Name contract) {
        return {sym, contract};
    }

    constexpr bool equals(const ExtendedSymbol& other) const {
        return sym == other.sym && contract == other.contract;
    }
    constexpr bool operator==(const ExtendedSymbol&) const = default;

    json toJSON() const { return {{"sym", sym.toJSON()}, {"contract", contract.toJSON()}}; }
};

inline namespace literals {

// "4,WAX"_symbol: precision,NAME validated at compile time
consteval Asset::Symbol operator""_symbol(const char* str, size_t len) {
    const std::string_view s(str, len);
    const size_t comma = s.find(',');
    if (comma == std::string_view::npos) {
        detail::invalidAssetLiteral();
    }
    int precision = 0;
    for (const char c : s.substr(0, comma)) {
        if (c < '0' || c > '9') detail::invalidAssetLiteral();
        precision = precision * 10 + (c - '0');
    }
    const std::string_view name = s.substr(comma + 1);
    if (precision > Asset::Symbol::maxPrecision || name.size() > 7) {
        detail::invalidAssetLiteral();
    }
    for (const char c : name) {
        if (c < 'A' || c > 'Z') detail::invalidAssetLiteral();
    }
    return Asset::Symbol(detail::toRawSymbol(name, static_cast<uint8_t>(precision)));
}

// "1.0000 WAX"_asset validated at compile time
consteval Asset operator""_asset(const char* str, size_t len) {
    const std::string_view s(str, len);
    const size_t space = s.find(' ');
    if (space == std::string_view::npos || s.find(' ', space + 1) != std::string_view::npos) {
        detail::invalidAssetLiteral();
    }
    std::string_view amount = s.substr(0, space);
    const std::string_view name = s.substr(space + 1);
    bool negative = false;
    if (!amount.empty() && (amount[0] == '-' || amount[0] == '+')) {
        negative = amount[0] == '-';
        amount = amount.substr(1);
    }
    if (amount.empty()) detail::invalidAssetLiteral();
    int64_t units = 0;
    int precision = 0;
    bool seenDot = false;
    for (const char c : amount) {
        if (c == '.') {
            if (seenDot) detail::invalidAssetLiteral();
            seenDot = true;
            continue;
        }
        if (c < '0' || c > '9') detail::invalidAssetLiteral();
        units = units * 10 + (c - '0');
        if (seenDot) precision++;
    }
    if (precision > Asset::Symbol::maxPrecision || name.size() > 7) {
        detail::invalidAssetLiteral();
    }
    for (const char c : name) {
        if (c < 'A' || c > 'Z') detail::invalidAssetLiteral();
    }
    return Asset(negative ? -units : units,
                 Asset::Symbol(detail::toRawSymbol(name, static_cast<uint8_t>(precision))));
}

}  // namespace literals

}  // namespace dwarfkit
