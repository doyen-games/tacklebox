#include <dwarfkit/antelope/utils.hpp>

namespace dwarfkit {

std::string arrayToHex(std::span<const uint8_t> array) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string rv(array.size() * 2, '\0');
    for (size_t i = 0; i < array.size(); ++i) {
        rv[i * 2] = digits[array[i] >> 4];
        rv[i * 2 + 1] = digits[array[i] & 0xf];
    }
    return rv;
}

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Result<std::vector<uint8_t>> hexToArray(std::string_view hex) {
    if (hex.size() % 2) {
        return err(ErrorKind::Invalid, "Odd number of hex digits");
    }
    std::vector<uint8_t> result(hex.size() / 2);
    for (size_t i = 0; i < result.size(); i++) {
        const int hi = hexDigit(hex[i * 2]);
        const int lo = hexDigit(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return err(ErrorKind::Invalid, "Expected hex string");
        }
        result[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return result;
}

}  // namespace dwarfkit
