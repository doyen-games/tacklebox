// Port of antelope src/base58.ts
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <dwarfkit/antelope/chain/bytes.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {
namespace Base58 {

// Wharfkit's Base58.DecodingError codes; carried in Error::details["code"].
enum class ErrorCode { E_CHECKSUM, E_INVALID };

// Decode a Base58 encoded string.
Result<Bytes> decode(std::string_view s, std::optional<size_t> size = std::nullopt);

// Decode a Base58Check encoded string.
Result<Bytes> decodeCheck(std::string_view encoded, std::optional<size_t> size = std::nullopt);

// Decode a Base58Check encoded string that uses ripemd160 instead of double
// sha256 for the digest.
Result<Bytes> decodeRipemd160Check(std::string_view encoded,
                                   std::optional<size_t> size = std::nullopt,
                                   std::optional<std::string_view> suffix = std::nullopt);

// Encode bytes to a Base58 string.
std::string encode(const Bytes& data);
Result<std::string> encode(std::string_view hexData);

std::string encodeCheck(const Bytes& data);
Result<std::string> encodeCheck(std::string_view hexData);

std::string encodeRipemd160Check(const Bytes& data,
                                 std::optional<std::string_view> suffix = std::nullopt);
Result<std::string> encodeRipemd160Check(std::string_view hexData,
                                         std::optional<std::string_view> suffix = std::nullopt);

}  // namespace Base58
}  // namespace dwarfkit
