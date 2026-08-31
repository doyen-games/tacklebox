#include "vault/kdf.hpp"

#include <cstring>

#include <dwarfkit/core/hash.hpp>

namespace tb {

void pbkdf2HmacSha256(std::span<const uint8_t> password, std::span<const uint8_t> salt,
                      uint32_t iterations, std::span<uint8_t> out) {
    const size_t hLen = 32;
    uint32_t blockCount = static_cast<uint32_t>((out.size() + hLen - 1) / hLen);
    std::vector<uint8_t> saltBlock(salt.begin(), salt.end());
    saltBlock.resize(salt.size() + 4);

    size_t written = 0;
    for (uint32_t i = 1; i <= blockCount; ++i) {
        saltBlock[salt.size() + 0] = static_cast<uint8_t>(i >> 24);
        saltBlock[salt.size() + 1] = static_cast<uint8_t>(i >> 16);
        saltBlock[salt.size() + 2] = static_cast<uint8_t>(i >> 8);
        saltBlock[salt.size() + 3] = static_cast<uint8_t>(i);

        auto u = dwarfkit::hmacSha256(password, saltBlock);
        auto t = u;
        for (uint32_t iter = 1; iter < iterations; ++iter) {
            u = dwarfkit::hmacSha256(password, u);
            for (size_t k = 0; k < hLen; ++k) t[k] ^= u[k];
        }
        size_t take = out.size() - written < hLen ? out.size() - written : hLen;
        std::memcpy(out.data() + written, t.data(), take);
        written += take;
        secureWipe(t.data(), t.size());
        secureWipe(u.data(), u.size());
    }
}

// --- scrypt core (RFC 7914) -------------------------------------------------

static inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

// Salsa20/8 core over 16 little-endian words, in place.
static void salsa208(uint32_t b[16]) {
    uint32_t x[16];
    std::memcpy(x, b, sizeof x);
    for (int round = 0; round < 8; round += 2) {
        x[4] ^= rotl32(x[0] + x[12], 7);
        x[8] ^= rotl32(x[4] + x[0], 9);
        x[12] ^= rotl32(x[8] + x[4], 13);
        x[0] ^= rotl32(x[12] + x[8], 18);
        x[9] ^= rotl32(x[5] + x[1], 7);
        x[13] ^= rotl32(x[9] + x[5], 9);
        x[1] ^= rotl32(x[13] + x[9], 13);
        x[5] ^= rotl32(x[1] + x[13], 18);
        x[14] ^= rotl32(x[10] + x[6], 7);
        x[2] ^= rotl32(x[14] + x[10], 9);
        x[6] ^= rotl32(x[2] + x[14], 13);
        x[10] ^= rotl32(x[6] + x[2], 18);
        x[3] ^= rotl32(x[15] + x[11], 7);
        x[7] ^= rotl32(x[3] + x[15], 9);
        x[11] ^= rotl32(x[7] + x[3], 13);
        x[15] ^= rotl32(x[11] + x[7], 18);
        x[1] ^= rotl32(x[0] + x[3], 7);
        x[2] ^= rotl32(x[1] + x[0], 9);
        x[3] ^= rotl32(x[2] + x[1], 13);
        x[0] ^= rotl32(x[3] + x[2], 18);
        x[6] ^= rotl32(x[5] + x[4], 7);
        x[7] ^= rotl32(x[6] + x[5], 9);
        x[4] ^= rotl32(x[7] + x[6], 13);
        x[5] ^= rotl32(x[4] + x[7], 18);
        x[11] ^= rotl32(x[10] + x[9], 7);
        x[8] ^= rotl32(x[11] + x[10], 9);
        x[9] ^= rotl32(x[8] + x[11], 13);
        x[10] ^= rotl32(x[9] + x[8], 18);
        x[12] ^= rotl32(x[15] + x[14], 7);
        x[13] ^= rotl32(x[12] + x[15], 9);
        x[14] ^= rotl32(x[13] + x[12], 13);
        x[15] ^= rotl32(x[14] + x[13], 18);
    }
    for (int i = 0; i < 16; ++i) b[i] += x[i];
}

// BlockMix: B is 2r 64-byte blocks as uint32 words; Y is scratch of equal size.
static void blockMix(uint32_t* b, uint32_t* y, uint32_t r) {
    uint32_t x[16];
    std::memcpy(x, b + (2 * r - 1) * 16, sizeof x);
    for (uint32_t i = 0; i < 2 * r; ++i) {
        for (int k = 0; k < 16; ++k) x[k] ^= b[i * 16 + k];
        salsa208(x);
        std::memcpy(y + i * 16, x, sizeof x);
    }
    // Even blocks first, then odd (the RFC's interleave).
    for (uint32_t i = 0; i < r; ++i) std::memcpy(b + i * 16, y + (i * 2) * 16, 64);
    for (uint32_t i = 0; i < r; ++i)
        std::memcpy(b + (r + i) * 16, y + (i * 2 + 1) * 16, 64);
}

static void romix(uint32_t* block, uint32_t n, uint32_t r, uint32_t* v, uint32_t* scratch) {
    const uint32_t words = 32 * r;
    for (uint32_t i = 0; i < n; ++i) {
        std::memcpy(v + static_cast<size_t>(i) * words, block, words * 4);
        blockMix(block, scratch, r);
    }
    for (uint32_t i = 0; i < n; ++i) {
        // Integerify: first word of the last 64-byte block, mod N.
        uint32_t j = block[(2 * r - 1) * 16] & (n - 1);
        const uint32_t* vj = v + static_cast<size_t>(j) * words;
        for (uint32_t k = 0; k < words; ++k) block[k] ^= vj[k];
        blockMix(block, scratch, r);
    }
}

bool scrypt(std::span<const uint8_t> password, std::span<const uint8_t> salt,
            const ScryptParams& params, std::span<uint8_t> out) {
    if (!params.valid() || out.empty()) return false;
    const uint32_t n = 1u << params.logN;
    const uint32_t r = params.r;
    const uint32_t p = params.p;
    const size_t blockBytes = static_cast<size_t>(128) * r;

    std::vector<uint8_t> b(blockBytes * p);
    pbkdf2HmacSha256(password, salt, 1, b);

    // Word views (scrypt is little-endian; convert explicitly for portability).
    const uint32_t words = 32 * r;
    std::vector<uint32_t> block(words);
    std::vector<uint32_t> v(static_cast<size_t>(n) * words);
    std::vector<uint32_t> scratch(words);

    for (uint32_t i = 0; i < p; ++i) {
        uint8_t* bi = b.data() + static_cast<size_t>(i) * blockBytes;
        for (uint32_t w = 0; w < words; ++w)
            block[w] = static_cast<uint32_t>(bi[w * 4]) |
                       (static_cast<uint32_t>(bi[w * 4 + 1]) << 8) |
                       (static_cast<uint32_t>(bi[w * 4 + 2]) << 16) |
                       (static_cast<uint32_t>(bi[w * 4 + 3]) << 24);
        romix(block.data(), n, r, v.data(), scratch.data());
        for (uint32_t w = 0; w < words; ++w) {
            bi[w * 4] = static_cast<uint8_t>(block[w]);
            bi[w * 4 + 1] = static_cast<uint8_t>(block[w] >> 8);
            bi[w * 4 + 2] = static_cast<uint8_t>(block[w] >> 16);
            bi[w * 4 + 3] = static_cast<uint8_t>(block[w] >> 24);
        }
    }

    pbkdf2HmacSha256(password, b, 1, out);
    secureWipe(b.data(), b.size());
    secureWipe(block.data(), block.size() * 4);
    secureWipe(v.data(), v.size() * 4);
    secureWipe(scratch.data(), scratch.size() * 4);
    return true;
}

SecureBytes deriveVaultKeys(const SecureBytes& password, std::span<const uint8_t> salt,
                            const ScryptParams& params) {
    SecureBytes keys(64);
    if (!scrypt(password.span(), salt, params, keys.span())) return SecureBytes();
    return keys;
}

}  // namespace tb
