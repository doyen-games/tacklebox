#include <dwarfkit/antelope/chain/time.hpp>

#include <cmath>
#include <cstdio>

namespace dwarfkit {

namespace {

// Math.round: floor(x + 0.5), ties toward positive infinity
int64_t jsRound(double x) { return static_cast<int64_t>(std::floor(x + 0.5)); }

// Howard Hinnant's days_from_civil / civil_from_days
constexpr int64_t daysFromCivil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

constexpr void civilFromDays(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y = yoe + era * 400 + (m <= 2);
}

constexpr int64_t floorDiv(int64_t a, int64_t b) {
    return a / b - ((a % b != 0 && (a < 0) != (b < 0)) ? 1 : 0);
}
constexpr int64_t floorMod(int64_t a, int64_t b) { return a - floorDiv(a, b) * b; }

// Date.parse(value + 'Z') for the ISO formats nodeos emits. Rejects a trailing
// Z in the input exactly like upstream (which would produce an invalid "..ZZ").
Result<int64_t> parseIsoMilliseconds(std::string_view s) {
    const auto invalid = [] { return err(ErrorKind::Invalid, "Invalid date string"); };
    const auto digits = [&s](size_t pos, size_t count, int64_t& out) {
        out = 0;
        if (pos + count > s.size()) return false;
        for (size_t i = pos; i < pos + count; i++) {
            if (s[i] < '0' || s[i] > '9') return false;
            out = out * 10 + (s[i] - '0');
        }
        return true;
    };
    int64_t year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, millis = 0;
    if (!digits(0, 4, year) || s.size() < 10 || s[4] != '-' || !digits(5, 2, month) ||
        s[7] != '-' || !digits(8, 2, day)) {
        return invalid();
    }
    size_t pos = 10;
    if (pos < s.size()) {
        if (s[pos] != 'T' && s[pos] != ' ') return invalid();
        pos++;
        if (!digits(pos, 2, hour) || pos + 5 >= s.size() || s[pos + 2] != ':' ||
            !digits(pos + 3, 2, minute) || s[pos + 5] != ':' || !digits(pos + 6, 2, second)) {
            return invalid();
        }
        pos += 8;
        if (pos < s.size() && s[pos] == '.') {
            pos++;
            int64_t scale = 100;
            const size_t start = pos;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
                if (scale > 0) {
                    millis += (s[pos] - '0') * scale;
                    scale /= 10;
                }
                pos++;
            }
            if (pos == start) return invalid();
        }
        if (pos != s.size()) return invalid();
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) {
        return invalid();
    }
    return ((daysFromCivil(year, month, day) * 24 + hour) * 60 + minute) * 60000 + second * 1000 +
           millis;
}

// Date.toISOString without the trailing Z: YYYY-MM-DDTHH:MM:SS.mmm
std::string isoFromMilliseconds(int64_t ms) {
    const int64_t days = floorDiv(ms, 86400000);
    const int64_t msOfDay = floorMod(ms, 86400000);
    int64_t y = 0, m = 0, d = 0;
    civilFromDays(days, y, m, d);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%03lld",
                  static_cast<long long>(y), static_cast<long long>(m), static_cast<long long>(d),
                  static_cast<long long>(msOfDay / 3600000),
                  static_cast<long long>((msOfDay / 60000) % 60),
                  static_cast<long long>((msOfDay / 1000) % 60),
                  static_cast<long long>(msOfDay % 1000));
    return buffer;
}

}  // namespace

TimePoint TimePoint::fromMilliseconds(double ms) { return TimePoint(jsRound(ms * 1000)); }

Result<TimePoint> TimePoint::fromString(std::string_view value) {
    DK_TRY(ms, parseIsoMilliseconds(value));
    return fromMilliseconds(static_cast<double>(ms));
}

TimePoint TimePoint::from(const TimePointSec& value) {
    return fromMilliseconds(static_cast<double>(value.toMilliseconds()));
}
TimePoint TimePoint::from(const BlockTimestamp& value) {
    return fromMilliseconds(static_cast<double>(value.toMilliseconds()));
}

int64_t TimePoint::toMilliseconds() const {
    // value.dividing(1000, 'round'): BN divRound, half away from zero
    return value >= 0 ? (value + 500) / 1000 : -((-value + 500) / 1000);
}

std::string TimePoint::toString() const { return isoFromMilliseconds(toMilliseconds()); }

bool TimePoint::equals(const TimePointSec& other) const {
    return toMilliseconds() == other.toMilliseconds();
}
bool TimePoint::equals(const BlockTimestamp& other) const {
    return toMilliseconds() == other.toMilliseconds();
}
bool TimePoint::equals(std::string_view other) const {
    const auto parsed = fromString(other);
    return parsed && toMilliseconds() == parsed->toMilliseconds();
}

TimePointSec TimePointSec::fromMilliseconds(double ms) {
    return TimePointSec(static_cast<uint32_t>(jsRound(ms / 1000)));
}

Result<TimePointSec> TimePointSec::fromString(std::string_view value) {
    DK_TRY(ms, parseIsoMilliseconds(value));
    return fromMilliseconds(static_cast<double>(ms));
}

TimePointSec TimePointSec::from(const TimePoint& value) {
    return fromMilliseconds(static_cast<double>(value.toMilliseconds()));
}
TimePointSec TimePointSec::from(const BlockTimestamp& value) {
    return fromMilliseconds(static_cast<double>(value.toMilliseconds()));
}

std::string TimePointSec::toString() const {
    // ISO string without the .mmmZ suffix
    std::string iso = isoFromMilliseconds(toMilliseconds());
    return iso.substr(0, iso.size() - 4);
}

bool TimePointSec::equals(const TimePoint& other) const {
    return toMilliseconds() == other.toMilliseconds();
}
bool TimePointSec::equals(const BlockTimestamp& other) const {
    return toMilliseconds() == other.toMilliseconds();
}
bool TimePointSec::equals(std::string_view other) const {
    const auto parsed = fromString(other);
    return parsed && toMilliseconds() == parsed->toMilliseconds();
}

BlockTimestamp BlockTimestamp::fromMilliseconds(double ms) {
    return BlockTimestamp(
        static_cast<uint32_t>(jsRound((ms - static_cast<double>(epochMilliseconds)) / 500)));
}

Result<BlockTimestamp> BlockTimestamp::fromString(std::string_view value) {
    DK_TRY(ms, parseIsoMilliseconds(value));
    return fromMilliseconds(static_cast<double>(ms));
}

BlockTimestamp BlockTimestamp::from(const TimePoint& value) {
    return fromMilliseconds(static_cast<double>(value.toMilliseconds()));
}
BlockTimestamp BlockTimestamp::from(const TimePointSec& value) {
    return fromMilliseconds(static_cast<double>(value.toMilliseconds()));
}

std::string BlockTimestamp::toString() const { return isoFromMilliseconds(toMilliseconds()); }

bool BlockTimestamp::equals(const TimePoint& other) const {
    return toMilliseconds() == other.toMilliseconds();
}
bool BlockTimestamp::equals(const TimePointSec& other) const {
    return toMilliseconds() == other.toMilliseconds();
}
bool BlockTimestamp::equals(std::string_view other) const {
    const auto parsed = fromString(other);
    return parsed && toMilliseconds() == parsed->toMilliseconds();
}

}  // namespace dwarfkit
