// Password key derivation for the vault: scrypt (RFC 7914) implemented on
// PBKDF2-HMAC-SHA256 from dwarfkit's hash primitives. Memory-hard so GPU/ASIC
// bruteforce of a stolen vault file stays expensive.
//
// Interactive default: N=2^15, r=8, p=1 (~32 MB, tens of ms on desktop).
// Parameters are stored in the vault header so they can be raised over time
// without breaking old files.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/secure.hpp"

namespace tb {

struct ScryptParams {
    uint32_t logN = 15;  // N = 1 << logN
    uint32_t r = 8;
    uint32_t p = 1;

    bool valid() const {
        return logN >= 10 && logN <= 22 && r >= 1 && r <= 32 && p >= 1 && p <= 16;
    }
};

// PBKDF2-HMAC-SHA256 (RFC 2898), exposed for tests.
void pbkdf2HmacSha256(std::span<const uint8_t> password, std::span<const uint8_t> salt,
                      uint32_t iterations, std::span<uint8_t> out);

// Derive `out.size()` bytes. Returns false only on invalid parameters.
bool scrypt(std::span<const uint8_t> password, std::span<const uint8_t> salt,
            const ScryptParams& params, std::span<uint8_t> out);

// Convenience: derive the vault's 64-byte key block (32 cipher + 32 mac).
SecureBytes deriveVaultKeys(const SecureBytes& password, std::span<const uint8_t> salt,
                            const ScryptParams& params);

}  // namespace tb
