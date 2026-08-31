#include <dwarfkit/antelope/base58.hpp>

#include <algorithm>
#include <array>

#include <dwarfkit/core/hash.hpp>

namespace dwarfkit {
namespace Base58 {

namespace {

constexpr std::string_view chars = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

constexpr std::array<int16_t, 256> buildCharMap() {
    std::array<int16_t, 256> map{};
    map.fill(-1);
    for (int16_t i = 0; i < 58; ++i) {
        map[static_cast<uint8_t>(chars[static_cast<size_t>(i)])] = i;
    }
    return map;
}
constexpr auto charMap = buildCharMap();

const char* codeName(ErrorCode code) {
    return code == ErrorCode::E_CHECKSUM ? "E_CHECKSUM" : "E_INVALID";
}

tl::unexpected<Error> decodingError(std::string message, ErrorCode code, json info = json::object()) {
    info["code"] = codeName(code);
    return err(ErrorKind::Invalid, std::move(message), 0, std::move(info));
}

Result<Bytes> decodeVar(std::string_view s) {
    std::vector<uint8_t> result;
    for (const char c : s) {
        int32_t carry = charMap[static_cast<uint8_t>(c)];
        if (carry < 0) {
            return decodingError("Invalid Base58 character encountered", ErrorCode::E_INVALID,
                                 {{"char", std::string(1, c)}});
        }
        for (auto& byte : result) {
            const int32_t x = byte * 58 + carry;
            byte = static_cast<uint8_t>(x & 0xff);
            carry = x >> 8;
        }
        if (carry) {
            result.push_back(static_cast<uint8_t>(carry));
        }
    }
    for (const char c : s) {
        if (c == '1') {
            result.push_back(0);
        } else {
            break;
        }
    }
    std::reverse(result.begin(), result.end());
    return Bytes(std::move(result));
}

std::array<uint8_t, 4> ripemd160Checksum(std::span<const uint8_t> data,
                                         std::optional<std::string_view> suffix) {
    std::vector<uint8_t> input(data.begin(), data.end());
    if (suffix) {
        input.insert(input.end(), suffix->begin(), suffix->end());
    }
    const auto digest = ripemd160(input);
    return {digest[0], digest[1], digest[2], digest[3]};
}

std::array<uint8_t, 4> dsha256Checksum(std::span<const uint8_t> data) {
    const auto round1 = sha256(data);
    const auto round2 = sha256(round1);
    return {round2[0], round2[1], round2[2], round2[3]};
}

tl::unexpected<Error> checksumMismatch(std::span<const uint8_t> actual,
                                       std::span<const uint8_t> expected,
                                       std::span<const uint8_t> data, const char* hash) {
    return decodingError("Checksum mismatch", ErrorCode::E_CHECKSUM,
                         {{"actual", arrayToHex(actual)},
                          {"expected", arrayToHex(expected)},
                          {"data", arrayToHex(data)},
                          {"hash", hash}});
}

}  // namespace

Result<Bytes> decode(std::string_view s, std::optional<size_t> size) {
    if (!size) {
        return decodeVar(s);
    }
    std::vector<uint8_t> result(*size, 0);
    for (const char c : s) {
        int32_t carry = charMap[static_cast<uint8_t>(c)];
        if (carry < 0) {
            return decodingError("Invalid Base58 character encountered", ErrorCode::E_INVALID,
                                 {{"char", std::string(1, c)}});
        }
        for (auto& byte : result) {
            const int32_t x = byte * 58 + carry;
            byte = static_cast<uint8_t>(x & 0xff);
            carry = x >> 8;
        }
        if (carry) {
            return decodingError("Base58 value is out of range", ErrorCode::E_INVALID);
        }
    }
    std::reverse(result.begin(), result.end());
    return Bytes(std::move(result));
}

Result<Bytes> decodeCheck(std::string_view encoded, std::optional<size_t> size) {
    DK_TRY(decoded, decode(encoded, size ? std::optional(*size + 4) : std::nullopt));
    if (decoded.array.size() < 4) {
        return checksumMismatch({}, decoded.array, {}, "double_sha256");
    }
    const std::span data(decoded.array.data(), decoded.array.size() - 4);
    const std::span expected(decoded.array.data() + decoded.array.size() - 4, size_t(4));
    const auto actual = dsha256Checksum(data);
    if (!std::ranges::equal(expected, actual)) {
        return checksumMismatch(actual, expected, data, "double_sha256");
    }
    return Bytes(data);
}

Result<Bytes> decodeRipemd160Check(std::string_view encoded, std::optional<size_t> size,
                                   std::optional<std::string_view> suffix) {
    DK_TRY(decoded, decode(encoded, size ? std::optional(*size + 4) : std::nullopt));
    if (decoded.array.size() < 4) {
        return checksumMismatch({}, decoded.array, {}, "ripemd160");
    }
    const std::span data(decoded.array.data(), decoded.array.size() - 4);
    const std::span expected(decoded.array.data() + decoded.array.size() - 4, size_t(4));
    const auto actual = ripemd160Checksum(data, suffix);
    if (!std::ranges::equal(expected, actual)) {
        return checksumMismatch(actual, expected, data, "ripemd160");
    }
    return Bytes(data);
}

std::string encode(const Bytes& data) {
    std::vector<char> result;
    for (const uint8_t byte : data.array) {
        int32_t carry = byte;
        for (auto& digit : result) {
            const int32_t x = (charMap[static_cast<uint8_t>(digit)] << 8) + carry;
            digit = chars[static_cast<size_t>(x % 58)];
            carry = x / 58;
        }
        while (carry) {
            result.push_back(chars[static_cast<size_t>(carry % 58)]);
            carry = carry / 58;
        }
    }
    for (const uint8_t byte : data.array) {
        if (byte) {
            break;
        }
        result.push_back('1');
    }
    std::reverse(result.begin(), result.end());
    return {result.begin(), result.end()};
}

Result<std::string> encode(std::string_view hexData) {
    DK_TRY(data, Bytes::from(hexData));
    return encode(data);
}

std::string encodeCheck(const Bytes& data) {
    return encode(data.appending(dsha256Checksum(data.array)));
}

Result<std::string> encodeCheck(std::string_view hexData) {
    DK_TRY(data, Bytes::from(hexData));
    return encodeCheck(data);
}

std::string encodeRipemd160Check(const Bytes& data, std::optional<std::string_view> suffix) {
    return encode(data.appending(ripemd160Checksum(data.array, suffix)));
}

Result<std::string> encodeRipemd160Check(std::string_view hexData,
                                         std::optional<std::string_view> suffix) {
    DK_TRY(data, Bytes::from(hexData));
    return encodeRipemd160Check(data, suffix);
}

}  // namespace Base58
}  // namespace dwarfkit
