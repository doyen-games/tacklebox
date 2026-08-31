// Port of antelope src/chain/integer.ts.
//
// Wharfkit's Int wrapper classes (Int8..UInt64) map to native C++ integers;
// this header keeps only what natives cannot express: the 128-bit types, the
// varint wrappers, and the exact toJSON number-versus-string rule.
#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <span>
#include <string>

#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

// How to handle integer overflow.
// - Throw: error if the value overflows (or underflows).
// - Truncate: truncates or extends bit-pattern with sign extension (C++11 behavior).
// - Clamp: clamps the value within the supported range.
enum class OverflowBehavior { Throw, Truncate, Clamp };

// Division rounding, named like the upstream DivisionBehavior union: floor is
// BN.div (truncation toward zero), round is half away from zero, ceil rounds
// away from zero.
enum class DivisionBehavior { floor, round, ceil };

// Matches FCs behavior: values that need more than 32 bits render as strings.
json intToJSON(int64_t value);
json intToJSON(uint64_t value);

class UInt128 {
public:
    static constexpr std::string_view abiName = "uint128";

    uint64_t lo = 0;
    uint64_t hi = 0;

    constexpr UInt128() = default;
    constexpr UInt128(uint64_t lo, uint64_t hi) : lo(lo), hi(hi) {}
    constexpr UInt128(uint64_t value) : lo(value) {}  // NOLINT(runtime/explicit)

    static constexpr UInt128 zero() { return {}; }
    static constexpr UInt128 max() { return {~0ull, ~0ull}; }
    static constexpr UInt128 min() { return {}; }

    static constexpr UInt128 from(uint64_t value) { return UInt128(value); }
    static Result<UInt128> from(std::string_view value,
                                OverflowBehavior overflow = OverflowBehavior::Throw);
    static constexpr UInt128 abiDefault() { return {}; }
    static Result<UInt128> random();

    // Number as bytes in little endian (matches memory layout in C++ contract).
    std::array<uint8_t, 16> byteArray() const;
    static UInt128 fromByteArray(std::span<const uint8_t, 16> le);

    constexpr bool equals(const UInt128& other) const { return lo == other.lo && hi == other.hi; }
    constexpr bool operator==(const UInt128&) const = default;
    friend constexpr std::strong_ordering operator<=>(const UInt128& a, const UInt128& b) {
        if (auto c = a.hi <=> b.hi; c != 0) return c;
        return a.lo <=> b.lo;
    }

    // Wrapping arithmetic (Wharfkit's default 'truncate' operator behavior).
    constexpr UInt128 adding(const UInt128& other) const {
        UInt128 rv{lo + other.lo, hi + other.hi};
        if (rv.lo < lo) rv.hi++;
        return rv;
    }
    constexpr UInt128 subtracting(const UInt128& other) const {
        UInt128 rv{lo - other.lo, hi - other.hi};
        if (other.lo > lo) rv.hi--;
        return rv;
    }

    // Wrapping multiply and division with the upstream DivisionBehavior
    // rounding names ('floor' is BN.div, truncation toward zero).
    UInt128 multiplying(const UInt128& other) const;
    Result<UInt128> dividing(const UInt128& other,
                             DivisionBehavior behavior = DivisionBehavior::floor) const;
    Result<UInt128> remainder(const UInt128& other) const;

    // Truncating cast (Wharfkit .cast(UInt64)).
    constexpr uint64_t toUInt64() const { return lo; }
    double toDouble() const {
        return static_cast<double>(hi) * 18446744073709551616.0 + static_cast<double>(lo);
    }

    std::string toString() const;
    json toJSON() const;
};

class Int128 {
public:
    static constexpr std::string_view abiName = "int128";

    // Two's complement representation; hi carries the sign bit.
    uint64_t lo = 0;
    uint64_t hi = 0;

    constexpr Int128() = default;
    constexpr Int128(uint64_t lo, uint64_t hi) : lo(lo), hi(hi) {}
    constexpr Int128(int64_t value)  // NOLINT(runtime/explicit)
        : lo(static_cast<uint64_t>(value)), hi(value < 0 ? ~0ull : 0) {}

    static constexpr Int128 zero() { return {}; }
    static constexpr Int128 max() { return {~0ull, 0x7fffffffffffffffull}; }
    static constexpr Int128 min() { return {0, 0x8000000000000000ull}; }

    static constexpr Int128 from(int64_t value) { return Int128(value); }
    static Result<Int128> from(std::string_view value,
                               OverflowBehavior overflow = OverflowBehavior::Throw);
    static constexpr Int128 abiDefault() { return {}; }
    static Result<Int128> random();

    std::array<uint8_t, 16> byteArray() const;
    static Int128 fromByteArray(std::span<const uint8_t, 16> le);

    constexpr bool isNegative() const { return hi >> 63; }

    constexpr bool equals(const Int128& other) const { return lo == other.lo && hi == other.hi; }
    constexpr bool operator==(const Int128&) const = default;
    friend constexpr std::strong_ordering operator<=>(const Int128& a, const Int128& b) {
        // flip the sign bit to compare as unsigned
        if (auto c = (a.hi ^ (1ull << 63)) <=> (b.hi ^ (1ull << 63)); c != 0) return c;
        return a.lo <=> b.lo;
    }

    constexpr Int128 adding(const Int128& other) const {
        Int128 rv{lo + other.lo, hi + other.hi};
        if (rv.lo < lo) rv.hi++;
        return rv;
    }
    constexpr Int128 subtracting(const Int128& other) const {
        Int128 rv{lo - other.lo, hi - other.hi};
        if (other.lo > lo) rv.hi--;
        return rv;
    }
    constexpr Int128 negated() const { return Int128{~lo, ~hi}.adding(Int128(int64_t(1))); }

    Int128 multiplying(const Int128& other) const;
    Result<Int128> dividing(const Int128& other,
                            DivisionBehavior behavior = DivisionBehavior::floor) const;

    constexpr int64_t toInt64() const { return static_cast<int64_t>(lo); }
    constexpr uint64_t toUInt64() const { return lo; }
    double toDouble() const;

    std::string toString() const;
    json toJSON() const;
};

// varint32: zig-zag is not used, plain signed LEB128 range int32.
class VarInt {
public:
    static constexpr std::string_view abiName = "varint32";

    int32_t value = 0;

    constexpr VarInt() = default;
    constexpr VarInt(int32_t value) : value(value) {}  // NOLINT(runtime/explicit)

    static constexpr VarInt from(int32_t value) { return VarInt(value); }
    static constexpr VarInt abiDefault() { return {}; }

    constexpr bool equals(const VarInt& other) const { return value == other.value; }
    constexpr bool operator==(const VarInt&) const = default;
    constexpr auto operator<=>(const VarInt&) const = default;

    std::string toString() const { return std::to_string(value); }
    json toJSON() const { return value; }
};

class VarUInt {
public:
    static constexpr std::string_view abiName = "varuint32";

    uint32_t value = 0;

    constexpr VarUInt() = default;
    constexpr VarUInt(uint32_t value) : value(value) {}  // NOLINT(runtime/explicit)

    static constexpr VarUInt from(uint32_t value) { return VarUInt(value); }
    static constexpr VarUInt abiDefault() { return {}; }

    constexpr bool equals(const VarUInt& other) const { return value == other.value; }
    constexpr bool operator==(const VarUInt&) const = default;
    constexpr auto operator<=>(const VarUInt&) const = default;

    std::string toString() const { return std::to_string(value); }
    json toJSON() const { return value; }
};

// BLUEPRINT.md 5.6 aliases
using VarInt32 = VarInt;
using VarUInt32 = VarUInt;

}  // namespace dwarfkit
