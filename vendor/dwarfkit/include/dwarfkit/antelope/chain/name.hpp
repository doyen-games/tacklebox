// Port of antelope src/chain/name.ts
#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

#include <dwarfkit/core/json.hpp>

namespace dwarfkit {

namespace detail {

constexpr uint64_t charToSymbol(char c) {
    if (c >= 'a' && c <= 'z') return static_cast<uint64_t>(c - 'a') + 6;
    if (c >= '1' && c <= '5') return static_cast<uint64_t>(c - '1') + 1;
    return 0;
}

constexpr uint64_t stringToName(std::string_view s) {
    uint64_t value = 0;
    int bit = 63;
    for (const char ch : s) {
        uint64_t c = charToSymbol(ch);
        if (bit < 5) {
            c = c << 1;
        }
        for (int j = 4; j >= 0; --j) {
            if (bit >= 0) {
                value |= ((c >> j) & 1ull) << bit;
                --bit;
            }
        }
    }
    return value;
}

// used by the _n literal to fail compilation on names Name::from would mangle
void invalidNameLiteral();

}  // namespace detail

// Antelope/EOSIO Name
class Name {
public:
    static constexpr std::string_view abiName = "name";

    // Regex pattern matching a Antelope/EOSIO name, case-sensitive.
    static constexpr std::string_view pattern = "^[a-z1-5.]{0,13}$";

    // The numeric representation of the name.
    uint64_t value = 0;

    constexpr Name() = default;
    constexpr explicit Name(uint64_t value) : value(value) {}

    // Create a new Name instance from any of its representing types.
    // Characters outside [a-z1-5.] map to '.' and names longer than 13
    // characters truncate, exactly like upstream.
    static constexpr Name from(std::string_view value) {
        return Name(detail::stringToName(value));
    }
    static constexpr Name from(uint64_t value) { return Name(value); }

    static constexpr Name abiDefault() { return {}; }

    // The raw representation of the name. Deprecated upstream, use value instead.
    constexpr uint64_t rawValue() const { return value; }

    // Return true if this name is equal to passed name.
    constexpr bool equals(const Name& other) const { return value == other.value; }
    constexpr bool equals(std::string_view other) const { return value == from(other).value; }
    constexpr bool operator==(const Name&) const = default;
    constexpr auto operator<=>(const Name&) const = default;

    // Compare with another name by underlying uint64 value.
    constexpr int compare(const Name& other) const {
        return value < other.value ? -1 : value > other.value ? 1 : 0;
    }

    // Return string representation of this name.
    std::string toString() const;

    json toJSON() const { return toString(); }
};

inline namespace literals {

consteval Name operator""_n(const char* str, size_t len) {
    const std::string_view s(str, len);
    if (len > 13) {
        detail::invalidNameLiteral();
    }
    for (size_t i = 0; i < len; ++i) {
        const char c = s[i];
        const bool valid = (c >= 'a' && c <= 'z') || (c >= '1' && c <= '5') || c == '.';
        // the 13th character only has 4 bits of space: [.1-5a-j]
        if (!valid || (i == 12 && detail::charToSymbol(c) > 15)) {
            detail::invalidNameLiteral();
        }
    }
    return Name::from(s);
}

}  // namespace literals

}  // namespace dwarfkit
