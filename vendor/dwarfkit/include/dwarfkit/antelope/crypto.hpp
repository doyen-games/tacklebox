// Ports antelope src/crypto/{sign,generate,get-public,recover,shared-secret,
// verify,curves}.ts. K1 runs on libsecp256k1 with a nonce function that
// reproduces elliptic's HMAC-DRBG (pers = [attempt]) so signatures match
// Wharfkit byte for byte; R1 runs on trezor-crypto's nist256p1.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <dwarfkit/antelope/chain/key_type.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit::crypto {

struct SignatureParts {
    KeyType type;
    std::array<uint8_t, 32> r{};
    std::array<uint8_t, 32> s{};
    int recid = 0;
};

// Sign a 32-byte digest with a 32-byte private key.
Result<SignatureParts> sign(std::span<const uint8_t> secret, std::span<const uint8_t> message,
                            KeyType type);

// Public key (33-byte compressed) for a private key.
Result<std::array<uint8_t, 33>> getPublic(std::span<const uint8_t> secret, KeyType type);

// Recover the 33-byte compressed public key from a 65-byte signature
// (recid+31 || r || s) and a 32-byte digest.
Result<std::array<uint8_t, 33>> recover(std::span<const uint8_t> signature,
                                        std::span<const uint8_t> message, KeyType type);

// ECDH shared secret X coordinate as minimal big-endian bytes (matches
// elliptic's derive().toArrayLike('be') with no fixed length).
Result<std::vector<uint8_t>> sharedSecret(std::span<const uint8_t> secret,
                                          std::span<const uint8_t> pubkey, KeyType type);

// Verify a 65-byte signature against a digest and 33-byte compressed key.
bool verify(std::span<const uint8_t> signature, std::span<const uint8_t> message,
            std::span<const uint8_t> pubkey, KeyType type);

// Generate a new 32-byte private key.
Result<std::array<uint8_t, 32>> generate(KeyType type);

}  // namespace dwarfkit::crypto
