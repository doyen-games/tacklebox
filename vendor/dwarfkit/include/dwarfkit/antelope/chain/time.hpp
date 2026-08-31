// Port of antelope src/chain/time.ts
#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

class TimePoint;
class TimePointSec;
class BlockTimestamp;

// Timestamp with microsecond accuracy.
class TimePoint {
public:
    static constexpr std::string_view abiName = "time_point";

    int64_t value = 0;  // microseconds since epoch

    constexpr TimePoint() = default;
    constexpr explicit TimePoint(int64_t value) : value(value) {}

    static TimePoint fromMilliseconds(double ms);
    static constexpr TimePoint fromInteger(int64_t value) { return TimePoint(value); }
    static Result<TimePoint> fromString(std::string_view value);

    static constexpr TimePoint from(const TimePoint& value) { return value; }
    static constexpr TimePoint from(int64_t value) { return fromInteger(value); }
    static Result<TimePoint> from(std::string_view value) { return fromString(value); }
    static TimePoint from(const TimePointSec& value);
    static TimePoint from(const BlockTimestamp& value);

    static constexpr TimePoint abiDefault() { return {}; }

    int64_t toMilliseconds() const;
    std::string toString() const;
    json toJSON() const { return toString(); }

    constexpr bool operator==(const TimePoint&) const = default;
    constexpr auto operator<=>(const TimePoint&) const = default;
    bool equals(const TimePoint& other) const { return value == other.value; }
    bool equals(const TimePointSec& other) const;
    bool equals(const BlockTimestamp& other) const;
    bool equals(std::string_view other) const;
};

// Timestamp with second accuracy.
class TimePointSec {
public:
    static constexpr std::string_view abiName = "time_point_sec";

    uint32_t value = 0;  // seconds since epoch

    constexpr TimePointSec() = default;
    constexpr explicit TimePointSec(uint32_t value) : value(value) {}

    static TimePointSec fromMilliseconds(double ms);
    static constexpr TimePointSec fromInteger(uint32_t value) { return TimePointSec(value); }
    static Result<TimePointSec> fromString(std::string_view value);

    static constexpr TimePointSec from(const TimePointSec& value) { return value; }
    static constexpr TimePointSec from(uint32_t value) { return fromInteger(value); }
    static Result<TimePointSec> from(std::string_view value) { return fromString(value); }
    static TimePointSec from(const TimePoint& value);
    static TimePointSec from(const BlockTimestamp& value);

    static constexpr TimePointSec abiDefault() { return {}; }

    int64_t toMilliseconds() const { return static_cast<int64_t>(value) * 1000; }
    std::string toString() const;
    json toJSON() const { return toString(); }

    constexpr bool operator==(const TimePointSec&) const = default;
    constexpr auto operator<=>(const TimePointSec&) const = default;
    bool equals(const TimePointSec& other) const { return value == other.value; }
    bool equals(const TimePoint& other) const;
    bool equals(const BlockTimestamp& other) const;
    bool equals(std::string_view other) const;
};

// Timestamp in half-second slots since 2000-01-01T00:00:00.
class BlockTimestamp {
public:
    static constexpr std::string_view abiName = "block_timestamp_type";
    static constexpr int64_t epochMilliseconds = 946684800000;

    uint32_t value = 0;

    constexpr BlockTimestamp() = default;
    constexpr explicit BlockTimestamp(uint32_t value) : value(value) {}

    static BlockTimestamp fromMilliseconds(double ms);
    static constexpr BlockTimestamp fromInteger(uint32_t value) { return BlockTimestamp(value); }
    static Result<BlockTimestamp> fromString(std::string_view value);

    static constexpr BlockTimestamp from(const BlockTimestamp& value) { return value; }
    static constexpr BlockTimestamp from(uint32_t value) { return fromInteger(value); }
    static Result<BlockTimestamp> from(std::string_view value) { return fromString(value); }
    static BlockTimestamp from(const TimePoint& value);
    static BlockTimestamp from(const TimePointSec& value);

    static constexpr BlockTimestamp abiDefault() { return {}; }

    int64_t toMilliseconds() const { return static_cast<int64_t>(value) * 500 + epochMilliseconds; }
    std::string toString() const;
    json toJSON() const { return toString(); }

    constexpr bool operator==(const BlockTimestamp&) const = default;
    constexpr auto operator<=>(const BlockTimestamp&) const = default;
    bool equals(const BlockTimestamp& other) const { return value == other.value; }
    bool equals(const TimePoint& other) const;
    bool equals(const TimePointSec& other) const;
    bool equals(std::string_view other) const;
};

}  // namespace dwarfkit
