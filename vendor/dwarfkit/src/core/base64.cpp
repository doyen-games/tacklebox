#include <dwarfkit/core/base64.hpp>

#include <array>

namespace dwarfkit {

namespace {

constexpr std::string_view alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr std::array<int8_t, 256> buildDecodeMap() {
    std::array<int8_t, 256> map{};
    map.fill(-1);
    for (int8_t i = 0; i < 64; ++i) {
        map[static_cast<uint8_t>(alphabet[static_cast<size_t>(i)])] = i;
    }
    return map;
}
constexpr auto decodeMap = buildDecodeMap();

}  // namespace

std::string base64Encode(std::span<const uint8_t> data) {
    std::string result;
    result.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                           (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
        result += alphabet[(n >> 18) & 63];
        result += alphabet[(n >> 12) & 63];
        result += alphabet[(n >> 6) & 63];
        result += alphabet[n & 63];
    }
    if (data.size() - i == 1) {
        const uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        result += alphabet[(n >> 18) & 63];
        result += alphabet[(n >> 12) & 63];
        result += "==";
    } else if (data.size() - i == 2) {
        const uint32_t n =
            (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
        result += alphabet[(n >> 18) & 63];
        result += alphabet[(n >> 12) & 63];
        result += alphabet[(n >> 6) & 63];
        result += '=';
    }
    return result;
}

Result<std::vector<uint8_t>> base64Decode(std::string_view text) {
    // trailing padding only
    size_t end = text.size();
    while (end > 0 && text[end - 1] == '=') {
        end--;
    }
    if (text.size() - end > 2 || (end % 4) == 1) {
        return err(ErrorKind::Invalid, "Invalid base64 string");
    }
    std::vector<uint8_t> result;
    result.reserve(end / 4 * 3 + 2);
    uint32_t buffer = 0;
    int bits = 0;
    for (size_t i = 0; i < end; i++) {
        const int8_t value = decodeMap[static_cast<uint8_t>(text[i])];
        if (value < 0) {
            return err(ErrorKind::Invalid, "Invalid base64 string");
        }
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<uint8_t>(buffer >> bits));
        }
    }
    return result;
}

}  // namespace dwarfkit
