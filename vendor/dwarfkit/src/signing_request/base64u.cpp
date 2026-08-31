#include <dwarfkit/signing_request/base64u.hpp>

#include <array>

namespace dwarfkit::base64u {

namespace {

constexpr std::string_view baseCharset =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

constexpr std::array<uint8_t, 256> buildLookup() {
    std::array<uint8_t, 256> lookup{};
    for (uint8_t i = 0; i < 62; i++) {
        lookup[static_cast<uint8_t>(baseCharset[i])] = i;
    }
    // support both urlsafe and standard base64
    lookup[static_cast<uint8_t>('+')] = lookup[static_cast<uint8_t>('-')] = 62;
    lookup[static_cast<uint8_t>('/')] = lookup[static_cast<uint8_t>('_')] = 63;
    return lookup;
}
constexpr auto lookup = buildLookup();

}  // namespace

std::string encode(std::span<const uint8_t> data, bool urlSafe) {
    const size_t byteLength = data.size();
    const size_t byteRemainder = byteLength % 3;
    const size_t mainLength = byteLength - byteRemainder;
    std::string charset = std::string(baseCharset) + (urlSafe ? "-_" : "+/");
    std::string out;
    out.reserve((byteLength * 4) / 3 + 4);

    for (size_t i = 0; i < mainLength; i += 3) {
        const uint32_t chunk = (static_cast<uint32_t>(data[i]) << 16) |
                               (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out += charset[(chunk & 16515072) >> 18];
        out += charset[(chunk & 258048) >> 12];
        out += charset[(chunk & 4032) >> 6];
        out += charset[chunk & 63];
    }
    if (byteRemainder == 1) {
        const uint32_t chunk = data[mainLength];
        out += charset[(chunk & 252) >> 2];
        out += charset[(chunk & 3) << 4];
    } else if (byteRemainder == 2) {
        const uint32_t chunk = (static_cast<uint32_t>(data[mainLength]) << 8) | data[mainLength + 1];
        out += charset[(chunk & 64512) >> 10];
        out += charset[(chunk & 1008) >> 4];
        out += charset[(chunk & 15) << 2];
    }
    return out;
}

std::vector<uint8_t> decode(std::string_view input) {
    const size_t byteLength = (input.size() * 3) / 4;
    std::vector<uint8_t> data(byteLength);
    size_t p = 0;
    for (size_t i = 0; i < input.size(); i += 4) {
        const auto at = [&](size_t idx) -> uint8_t {
            return idx < input.size() ? lookup[static_cast<uint8_t>(input[idx])] : 0;
        };
        const uint8_t a = at(i), b = at(i + 1), c = at(i + 2), d = at(i + 3);
        if (p < byteLength) data[p++] = static_cast<uint8_t>((a << 2) | (b >> 4));
        if (p < byteLength) data[p++] = static_cast<uint8_t>(((b & 15) << 4) | (c >> 2));
        if (p < byteLength) data[p++] = static_cast<uint8_t>(((c & 3) << 6) | (d & 63));
    }
    return data;
}

}  // namespace dwarfkit::base64u
