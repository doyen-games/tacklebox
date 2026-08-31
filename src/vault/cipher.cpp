#include "vault/cipher.hpp"

#include <cstring>

#include <aes/aes.h>
#include <dwarfkit/core/hash.hpp>

#include "core/rng.hpp"

namespace tb {

static std::array<uint8_t, 32> computeMac(const SecureBytes& keys,
                                          std::span<const uint8_t> aad,
                                          std::span<const uint8_t> iv,
                                          std::span<const uint8_t> ct) {
    std::vector<uint8_t> msg;
    msg.reserve(aad.size() + iv.size() + ct.size());
    msg.insert(msg.end(), aad.begin(), aad.end());
    msg.insert(msg.end(), iv.begin(), iv.end());
    msg.insert(msg.end(), ct.begin(), ct.end());
    std::span<const uint8_t> macKey(keys.data() + 32, 32);
    return dwarfkit::hmacSha256(macKey, msg);
}

SealedBox seal(const SecureBytes& keys, std::span<const uint8_t> plaintext,
               std::span<const uint8_t> aad) {
    SealedBox box;
    randomBytes(box.iv.data(), box.iv.size());

    // PKCS7 pad to the AES block size.
    size_t padded = (plaintext.size() / 16 + 1) * 16;
    uint8_t pad = static_cast<uint8_t>(padded - plaintext.size());
    SecureBytes buf(padded);
    if (!plaintext.empty()) std::memcpy(buf.data(), plaintext.data(), plaintext.size());
    std::memset(buf.data() + plaintext.size(), pad, pad);

    aes_encrypt_ctx ctx;
    aes_encrypt_key256(keys.data(), &ctx);
    box.ciphertext.resize(padded);
    uint8_t iv[16];
    std::memcpy(iv, box.iv.data(), 16);
    aes_cbc_encrypt(buf.data(), box.ciphertext.data(), static_cast<int>(padded), iv, &ctx);
    secureWipe(&ctx, sizeof ctx);

    box.mac = computeMac(keys, aad, box.iv, box.ciphertext);
    return box;
}

std::optional<SecureBytes> open(const SecureBytes& keys, const SealedBox& box,
                                std::span<const uint8_t> aad) {
    if (keys.size() != 64) return std::nullopt;
    if (box.ciphertext.empty() || box.ciphertext.size() % 16 != 0) return std::nullopt;

    auto expect = computeMac(keys, aad, box.iv, box.ciphertext);
    if (!constTimeEq(expect, box.mac)) return std::nullopt;

    aes_decrypt_ctx ctx;
    aes_decrypt_key256(keys.data(), &ctx);
    SecureBytes buf(box.ciphertext.size());
    uint8_t iv[16];
    std::memcpy(iv, box.iv.data(), 16);
    aes_cbc_decrypt(box.ciphertext.data(), buf.data(), static_cast<int>(box.ciphertext.size()),
                    iv, &ctx);
    secureWipe(&ctx, sizeof ctx);

    uint8_t pad = buf.data()[buf.size() - 1];
    if (pad == 0 || pad > 16 || pad > buf.size()) return std::nullopt;
    for (size_t i = buf.size() - pad; i < buf.size(); ++i)
        if (buf.data()[i] != pad) return std::nullopt;

    SecureBytes out(std::span<const uint8_t>(buf.data(), buf.size() - pad));
    return out;
}

}  // namespace tb
