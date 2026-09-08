// Small shared helpers: hex, time, strings, uuid. Everything here is pure and
// thread-safe.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tb {

std::string toHex(std::span<const uint8_t> data);
std::optional<std::vector<uint8_t>> fromHex(std::string_view hex);

// Unix time in seconds / milliseconds.
int64_t nowSec();
int64_t nowMs();

// "2026-08-27 14:03:11" local time for display; ISO 8601 UTC for storage.
std::string formatLocal(int64_t unixSec);
std::string formatIsoUtc(int64_t unixSec);
// Compact relative form for activity feeds: "12s", "4m", "3h", "6d".
std::string formatAgo(int64_t unixSec);

std::string toLower(std::string s);
std::string trim(const std::string& s);
bool startsWith(std::string_view s, std::string_view prefix);

// Strict decimal parse of the whole string ([-+]digits[.digits][e[-+]digits])
// into a double, independent of the C locale. Neither std::from_chars (no
// floating-point overloads in libc++ before macOS 26) nor strtod (follows
// LC_NUMERIC) fits a wallet that must read "1.5" the same everywhere.
bool parseDouble(std::string_view text, double& out);

// RFC 4122 v4 UUID from the OS CSPRNG.
std::string uuid4();

// Shorten "PUB_K1_6MRy..." style strings for display: first n, ellipsis, last m.
std::string middleEllipsis(const std::string& s, size_t head = 12, size_t tail = 6);

}  // namespace tb
