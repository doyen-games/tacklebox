#include <dwarfkit/core/hash.hpp>

extern "C" {
#include <hmac.h>
#include <ripemd160.h>
#include <sha2.h>
}

namespace dwarfkit {

std::array<uint8_t, 32> sha256(std::span<const uint8_t> data) {
    std::array<uint8_t, 32> digest{};
    sha256_Raw(data.data(), data.size(), digest.data());
    return digest;
}

std::array<uint8_t, 64> sha512(std::span<const uint8_t> data) {
    std::array<uint8_t, 64> digest{};
    sha512_Raw(data.data(), data.size(), digest.data());
    return digest;
}

std::array<uint8_t, 20> ripemd160(std::span<const uint8_t> data) {
    std::array<uint8_t, 20> digest{};
    ::ripemd160(data.data(), static_cast<uint32_t>(data.size()), digest.data());
    return digest;
}

std::array<uint8_t, 32> hmacSha256(std::span<const uint8_t> key, std::span<const uint8_t> data) {
    std::array<uint8_t, 32> digest{};
    hmac_sha256(key.data(), static_cast<uint32_t>(key.size()), data.data(),
                static_cast<uint32_t>(data.size()), digest.data());
    return digest;
}

std::array<uint8_t, 64> hmacSha512(std::span<const uint8_t> key, std::span<const uint8_t> data) {
    std::array<uint8_t, 64> digest{};
    hmac_sha512(key.data(), static_cast<uint32_t>(key.size()), data.data(),
                static_cast<uint32_t>(data.size()), digest.data());
    return digest;
}

}  // namespace dwarfkit
