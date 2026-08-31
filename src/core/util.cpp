#include "core/util.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>

#include "core/rng.hpp"

namespace tb {

std::string toHex(std::span<const uint8_t> data) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t b : data) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0f]);
    }
    return out;
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<std::vector<uint8_t>> fromHex(std::string_view hex) {
    if (hex.size() % 2 != 0) return std::nullopt;
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hexVal(hex[i]), lo = hexVal(hex[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

int64_t nowSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string formatLocal(int64_t unixSec) {
    std::time_t t = static_cast<std::time_t>(unixSec);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d", tmv.tm_year + 1900,
                  tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

std::string formatIsoUtc(int64_t unixSec) {
    std::time_t t = static_cast<std::time_t>(unixSec);
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", tmv.tm_year + 1900,
                  tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

std::string formatAgo(int64_t unixSec) {
    int64_t d = nowSec() - unixSec;
    if (d < 0) d = 0;
    char buf[24];
    if (d < 60)
        std::snprintf(buf, sizeof buf, "%llds", static_cast<long long>(d));
    else if (d < 3600)
        std::snprintf(buf, sizeof buf, "%lldm", static_cast<long long>(d / 60));
    else if (d < 86400)
        std::snprintf(buf, sizeof buf, "%lldh", static_cast<long long>(d / 3600));
    else
        std::snprintf(buf, sizeof buf, "%lldd", static_cast<long long>(d / 86400));
    return buf;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

std::string uuid4() {
    uint8_t b[16];
    randomBytes(b, sizeof b);
    b[6] = static_cast<uint8_t>((b[6] & 0x0f) | 0x40);
    b[8] = static_cast<uint8_t>((b[8] & 0x3f) | 0x80);
    char buf[40];
    std::snprintf(buf, sizeof buf,
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0],
                  b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13],
                  b[14], b[15]);
    return buf;
}

std::string middleEllipsis(const std::string& s, size_t head, size_t tail) {
    if (s.size() <= head + tail + 3) return s;
    return s.substr(0, head) + "..." + s.substr(s.size() - tail);
}

}  // namespace tb
