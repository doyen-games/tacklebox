// Authenticated encryption for the vault payload: AES-256-CBC with PKCS7
// padding, then HMAC-SHA256 over (aad || iv || ciphertext) - encrypt-then-MAC.
// Primitives come from the trezor-crypto library dwarfkit already vendors, so
// no additional crypto dependency enters the build.
//
// The MAC is verified in constant time before any decryption is attempted;
// a tampered or wrongly-keyed box never reaches the AES layer.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/secure.hpp"

namespace tb {

struct SealedBox {
    std::array<uint8_t, 16> iv{};
    std::vector<uint8_t> ciphertext;
    std::array<uint8_t, 32> mac{};
};

// keys = 64 bytes from the KDF: [0..32) AES key, [32..64) HMAC key.
// aad binds header fields (version, kdf params) so they cannot be swapped.
SealedBox seal(const SecureBytes& keys, std::span<const uint8_t> plaintext,
               std::span<const uint8_t> aad);

// nullopt on MAC failure, bad padding, or malformed input.
std::optional<SecureBytes> open(const SecureBytes& keys, const SealedBox& box,
                                std::span<const uint8_t> aad);

}  // namespace tb
