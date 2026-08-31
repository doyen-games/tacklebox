#include <dwarfkit/antelope/chain/asset.hpp>

#include <cmath>
#include <cstdio>

namespace dwarfkit {

namespace {

std::string symbolNameFromCode(uint64_t code) {
    std::string name;
    while (code) {
        name += static_cast<char>(code & 0xff);
        code >>= 8;
    }
    return name;
}

// Int64.from(string) equivalent for the digits of an asset amount
Result<int64_t> parseInt64(std::string_view value) {
    bool negative = false;
    size_t i = 0;
    if (i < value.size() && (value[i] == '-' || value[i] == '+')) {
        negative = value[i] == '-';
        i++;
    }
    if (i >= value.size()) {
        return err(ErrorKind::Invalid, "Invalid number");
    }
    uint64_t magnitude = 0;
    constexpr uint64_t limitNegative = 0x8000000000000000ull;
    constexpr uint64_t limitPositive = 0x7fffffffffffffffull;
    for (; i < value.size(); ++i) {
        const char c = value[i];
        if (c < '0' || c > '9') {
            return err(ErrorKind::Invalid, "Invalid number");
        }
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        const uint64_t limit = negative ? limitNegative : limitPositive;
        if (magnitude > (limit - digit) / 10) {
            return err(ErrorKind::Invalid, negative
                                               ? "Number " + std::string(value) + " underflows int64"
                                               : "Number " + std::string(value) + " overflows int64");
        }
        magnitude = magnitude * 10 + digit;
    }
    return negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
}

}  // namespace

Result<Asset::SymbolCode> Asset::SymbolCode::from(uint64_t value) {
    if (value != 0 && !detail::isValidSymbolName(value)) {
        return err(ErrorKind::Invalid, "Invalid asset symbol, name must be uppercase A-Z");
    }
    return SymbolCode(value);
}

Result<Asset::SymbolCode> Asset::SymbolCode::from(std::string_view name) {
    return from(detail::toRawSymbolCode(name));
}

std::string Asset::SymbolCode::toString() const { return symbolNameFromCode(value); }

Result<Asset::Symbol> Asset::Symbol::from(uint64_t value) {
    if (static_cast<int>(value & 0xff) > maxPrecision) {
        return err(ErrorKind::Invalid, "Invalid asset symbol, precision too large");
    }
    if (value != 0 && !detail::isValidSymbolName(value >> 8)) {
        return err(ErrorKind::Invalid, "Invalid asset symbol, name must be uppercase A-Z");
    }
    return Symbol(value);
}

Result<Asset::Symbol> Asset::Symbol::from(std::string_view value) {
    // "0," is the only string with a trailing comma upstream accepts
    const size_t comma = value.find(',');
    if (comma == std::string_view::npos ||
        (value.find(',', comma + 1) != std::string_view::npos && value != "0,")) {
        return err(ErrorKind::Invalid, "Invalid symbol string");
    }
    int precision = 0;
    bool anyDigit = false;
    for (const char c : value.substr(0, comma)) {
        if (c < '0' || c > '9') break;
        precision = precision * 10 + (c - '0');
        anyDigit = true;
    }
    if (!anyDigit) {
        return err(ErrorKind::Invalid, "Invalid symbol string");
    }
    return fromParts(value.substr(comma + 1), precision);
}

Result<Asset::Symbol> Asset::Symbol::fromParts(std::string_view name, int precision) {
    return from(detail::toRawSymbol(name, static_cast<uint8_t>(precision)));
}

std::string Asset::Symbol::name() const { return symbolNameFromCode(value >> 8); }

Result<double> Asset::Symbol::convertUnits(int64_t units) const {
    const uint64_t magnitude =
        units < 0 ? ~static_cast<uint64_t>(units) + 1 : static_cast<uint64_t>(units);
    if (magnitude >= (1ull << 53)) {
        return err(ErrorKind::Invalid, "Number can only safely store up to 53 bits");
    }
    return static_cast<double>(units) / std::pow(10.0, precision());
}

Result<int64_t> Asset::Symbol::convertFloat(double number) const {
    // float.toFixed(precision).replace('.', '') then Int64.from
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision(), number);
    std::string digits(buffer);
    if (const size_t dot = digits.find('.'); dot != std::string::npos) {
        digits.erase(dot, 1);
    }
    return parseInt64(digits);
}

std::string Asset::Symbol::toString() const {
    return std::to_string(precision()) + "," + name();
}

Result<Asset> Asset::from(std::string_view value) { return fromString(value); }

Result<Asset> Asset::from(double value, const Symbol& symbol) { return fromFloat(value, symbol); }

Result<Asset> Asset::from(double value, std::string_view symbol) {
    DK_TRY(sym, Symbol::from(symbol));
    return fromFloat(value, sym);
}

Result<Asset> Asset::fromString(std::string_view value) {
    const size_t space = value.find(' ');
    if (space == std::string_view::npos || value.find(' ', space + 1) != std::string_view::npos) {
        return err(ErrorKind::Invalid, "Invalid asset string");
    }
    std::string amount(value.substr(0, space));
    const std::string_view symbolName = value.substr(space + 1);
    int precision = 0;
    if (const size_t dot = amount.find('.'); dot != std::string::npos) {
        precision = static_cast<int>(amount.size() - dot - 1);
        amount.erase(dot, 1);
    }
    DK_TRY(symbol, Symbol::fromParts(symbolName, precision));
    DK_TRY(units, parseInt64(amount));
    return Asset(units, symbol);
}

Result<Asset> Asset::fromFloat(double value, const Symbol& symbol) {
    DK_TRY(units, symbol.convertFloat(value));
    return Asset(units, symbol);
}

Result<Asset> Asset::fromUnits(int64_t value, std::string_view symbol) {
    DK_TRY(sym, Symbol::from(symbol));
    return Asset(value, sym);
}

std::string Asset::formatUnits(int64_t units, int precision) {
    std::string digits = std::to_string(units);
    bool negative = false;
    if (digits[0] == '-') {
        negative = true;
        digits.erase(0, 1);
    }
    while (static_cast<int>(digits.size()) <= precision) {
        digits.insert(digits.begin(), '0');
    }
    if (precision > 0) {
        digits.insert(digits.size() - static_cast<size_t>(precision), 1, '.');
    }
    return negative ? "-" + digits : digits;
}

double Asset::value() const {
    return static_cast<double>(units) / std::pow(10.0, symbol.precision());
}

Result<void> Asset::setValue(double newValue) {
    DK_TRY(newUnits, symbol.convertFloat(newValue));
    units = newUnits;
    return {};
}

}  // namespace dwarfkit
