#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace dwarfkit {

// Raw digest primitives backing the Checksum types, base58 check variants,
// key derivation and signing. Implemented on the vendored trezor-crypto.
std::array<uint8_t, 32> sha256(std::span<const uint8_t> data);
std::array<uint8_t, 64> sha512(std::span<const uint8_t> data);
std::array<uint8_t, 20> ripemd160(std::span<const uint8_t> data);
std::array<uint8_t, 32> hmacSha256(std::span<const uint8_t> key, std::span<const uint8_t> data);
std::array<uint8_t, 64> hmacSha512(std::span<const uint8_t> key, std::span<const uint8_t> data);

}  // namespace dwarfkit
