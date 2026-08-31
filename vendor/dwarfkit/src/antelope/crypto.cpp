#include <dwarfkit/antelope/crypto.hpp>

#include <algorithm>
#include <cstring>

#include <dwarfkit/antelope/utils.hpp>
#include <dwarfkit/core/hash.hpp>

#include <secp256k1.h>
#include <secp256k1_ecdh.h>
#include <secp256k1_recovery.h>

extern "C" {
#include <ecdsa.h>
#include <nist256p1.h>
}

namespace dwarfkit::crypto {

namespace {

// secp256k1 group order, big-endian
constexpr std::array<uint8_t, 32> secp256k1Order = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
    0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0, 0x3b, 0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41, 0x41};

int compareBE(const std::array<uint8_t, 32>& a, const std::array<uint8_t, 32>& b) {
    for (size_t i = 0; i < 32; i++) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

// value -= subtrahend (both big-endian, value >= subtrahend)
void subBE(std::array<uint8_t, 32>& value, const std::array<uint8_t, 32>& subtrahend) {
    int borrow = 0;
    for (int i = 31; i >= 0; --i) {
        const int diff = value[static_cast<size_t>(i)] - subtrahend[static_cast<size_t>(i)] - borrow;
        value[static_cast<size_t>(i)] = static_cast<uint8_t>(diff & 0xff);
        borrow = diff < 0 ? 1 : 0;
    }
}

secp256k1_context* secpContext() {
    static secp256k1_context* ctx =
        secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    return ctx;
}

// elliptic's HmacDRBG over sha256 with a personalization string. Reproduces the
// K1 nonce so signatures match Wharfkit byte for byte.
class EllipticDRBG {
public:
    EllipticDRBG(std::span<const uint8_t> entropy, std::span<const uint8_t> nonce,
                 std::span<const uint8_t> pers) {
        k_.fill(0x00);
        v_.fill(0x01);
        std::vector<uint8_t> seed;
        seed.insert(seed.end(), entropy.begin(), entropy.end());
        seed.insert(seed.end(), nonce.begin(), nonce.end());
        seed.insert(seed.end(), pers.begin(), pers.end());
        update(&seed);
    }

    std::array<uint8_t, 32> generate() {
        v_ = hmacSha256(k_, v_);
        const std::array<uint8_t, 32> result = v_;
        update(nullptr);
        return result;
    }

private:
    void update(const std::vector<uint8_t>* seed) {
        std::vector<uint8_t> data(v_.begin(), v_.end());
        data.push_back(0x00);
        if (seed) data.insert(data.end(), seed->begin(), seed->end());
        k_ = hmacSha256(k_, data);
        v_ = hmacSha256(k_, v_);
        if (!seed) return;
        data.assign(v_.begin(), v_.end());
        data.push_back(0x01);
        data.insert(data.end(), seed->begin(), seed->end());
        k_ = hmacSha256(k_, data);
        v_ = hmacSha256(k_, v_);
    }

    std::array<uint8_t, 32> k_{};
    std::array<uint8_t, 32> v_{};
};

// nonce function passed to libsecp256k1. data points to the current Wharfkit
// attempt (the pers byte). libsecp256k1's own attempt counter maps to
// elliptic's inner DRBG regeneration loop.
int k1NonceFn(unsigned char* nonce32, const unsigned char* msg32, const unsigned char* key32,
              const unsigned char* /*algo16*/, void* data, unsigned int attempt) {
    const uint8_t pers = *static_cast<const uint8_t*>(data);

    // reduce the message mod n exactly like elliptic's _truncateToN
    std::array<uint8_t, 32> nonce{};
    std::memcpy(nonce.data(), msg32, 32);
    if (compareBE(nonce, secp256k1Order) >= 0) {
        subBE(nonce, secp256k1Order);
    }
    std::array<uint8_t, 32> entropy{};
    std::memcpy(entropy.data(), key32, 32);
    const std::array<uint8_t, 1> persBytes{pers};

    EllipticDRBG drbg(entropy, nonce, persBytes);

    // return the (attempt+1)-th k that lands in [2, n-2]
    unsigned int produced = 0;
    for (int guard = 0; guard < 1000; ++guard) {
        std::array<uint8_t, 32> candidate = drbg.generate();
        // reject k <= 1 or k >= n-1 (elliptic's inner truncate/degenerate check)
        std::array<uint8_t, 32> nMinusOne = secp256k1Order;
        nMinusOne[31] -= 1;
        const bool tooSmall = [&] {
            for (size_t i = 0; i < 31; i++)
                if (candidate[i] != 0) return false;
            return candidate[31] <= 1;
        }();
        if (tooSmall || compareBE(candidate, nMinusOne) >= 0) {
            continue;
        }
        if (produced++ == attempt) {
            std::memcpy(nonce32, candidate.data(), 32);
            return 1;
        }
    }
    return 0;
}

}  // namespace

Result<SignatureParts> sign(std::span<const uint8_t> secret, std::span<const uint8_t> message,
                            KeyType type) {
    if (secret.size() != 32 || message.size() != 32) {
        return err(ErrorKind::Invalid, "Invalid key or message length");
    }
    SignatureParts parts;
    parts.type = type;

    if (type == KeyType::K1) {
        secp256k1_context* ctx = secpContext();
        // Wharfkit loops attempt=1,2,... until the r/s high-bit canonical check
        // passes, re-seeding the DRBG pers with each attempt.
        for (uint8_t attempt = 1; attempt != 0; ++attempt) {
            secp256k1_ecdsa_recoverable_signature sig;
            if (!secp256k1_ecdsa_sign_recoverable(ctx, &sig, message.data(), secret.data(),
                                                  k1NonceFn, &attempt)) {
                return err(ErrorKind::Invalid, "Failed to sign");
            }
            std::array<uint8_t, 64> compact{};
            int recid = 0;
            secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, compact.data(), &recid,
                                                                    &sig);
            const uint8_t r0 = compact[0], r1 = compact[1], s0 = compact[32], s1 = compact[33];
            const bool canonical = !(r0 & 0x80) && !(r0 == 0 && !(r1 & 0x80)) && !(s0 & 0x80) &&
                                   !(s0 == 0 && !(s1 & 0x80));
            if (canonical) {
                std::copy(compact.begin(), compact.begin() + 32, parts.r.begin());
                std::copy(compact.begin() + 32, compact.end(), parts.s.begin());
                parts.recid = recid;
                return parts;
            }
        }
        return err(ErrorKind::Internal, "Failed to find canonical signature");
    }

    if (type == KeyType::R1) {
        std::array<uint8_t, 64> compact{};
        uint8_t pby = 0;
        if (ecdsa_sign_digest(&nist256p1, secret.data(), message.data(), compact.data(), &pby,
                              nullptr) != 0) {
            return err(ErrorKind::Invalid, "Failed to sign");
        }
        std::copy(compact.begin(), compact.begin() + 32, parts.r.begin());
        std::copy(compact.begin() + 32, compact.end(), parts.s.begin());
        parts.recid = pby;
        return parts;
    }

    return err(ErrorKind::Unsupported, "Cannot sign with WA keys");
}

Result<std::array<uint8_t, 33>> getPublic(std::span<const uint8_t> secret, KeyType type) {
    if (secret.size() != 32) {
        return err(ErrorKind::Invalid, "Invalid private key length");
    }
    std::array<uint8_t, 33> compressed{};
    if (type == KeyType::K1) {
        secp256k1_context* ctx = secpContext();
        secp256k1_pubkey pubkey;
        if (!secp256k1_ec_pubkey_create(ctx, &pubkey, secret.data())) {
            return err(ErrorKind::Invalid, "Invalid private key");
        }
        size_t len = 33;
        secp256k1_ec_pubkey_serialize(ctx, compressed.data(), &len, &pubkey,
                                      SECP256K1_EC_COMPRESSED);
        return compressed;
    }
    // R1 and WA both derive on the p256 curve
    if (ecdsa_get_public_key33(&nist256p1, secret.data(), compressed.data()) != 0) {
        return err(ErrorKind::Invalid, "Invalid private key");
    }
    return compressed;
}

Result<std::array<uint8_t, 33>> recover(std::span<const uint8_t> signature,
                                        std::span<const uint8_t> message, KeyType type) {
    if (type == KeyType::WA) {
        return err(ErrorKind::Unsupported,
                   "can't recover webauthn public keys, please use @wharfkit/webauthn.");
    }
    if (signature.size() < 65 || message.size() != 32) {
        return err(ErrorKind::Invalid, "Invalid signature or message length");
    }
    const int recid = signature[0] - 31;
    std::array<uint8_t, 33> compressed{};

    if (type == KeyType::K1) {
        secp256k1_context* ctx = secpContext();
        secp256k1_ecdsa_recoverable_signature sig;
        if (!secp256k1_ecdsa_recoverable_signature_parse_compact(ctx, &sig, signature.data() + 1,
                                                                 recid)) {
            return err(ErrorKind::Invalid, "Invalid signature");
        }
        secp256k1_pubkey pubkey;
        if (!secp256k1_ecdsa_recover(ctx, &pubkey, &sig, message.data())) {
            return err(ErrorKind::Invalid, "Unable to recover public key");
        }
        size_t len = 33;
        secp256k1_ec_pubkey_serialize(ctx, compressed.data(), &len, &pubkey,
                                      SECP256K1_EC_COMPRESSED);
        return compressed;
    }

    // R1
    std::array<uint8_t, 64> compact{};
    std::copy(signature.begin() + 1, signature.begin() + 65, compact.begin());
    std::array<uint8_t, 65> uncompressed{};
    if (ecdsa_recover_pub_from_sig(&nist256p1, uncompressed.data(), compact.data(), message.data(),
                                   recid) != 0) {
        return err(ErrorKind::Invalid, "Unable to recover public key");
    }
    compressed[0] = static_cast<uint8_t>(0x02 | (uncompressed[64] & 1));
    std::copy(uncompressed.begin() + 1, uncompressed.begin() + 33, compressed.begin() + 1);
    return compressed;
}

Result<std::vector<uint8_t>> sharedSecret(std::span<const uint8_t> secret,
                                          std::span<const uint8_t> pubkey, KeyType type) {
    if (secret.size() != 32) {
        return err(ErrorKind::Invalid, "Invalid private key length");
    }
    std::array<uint8_t, 32> x{};

    if (type == KeyType::K1) {
        secp256k1_context* ctx = secpContext();
        secp256k1_pubkey parsed;
        if (!secp256k1_ec_pubkey_parse(ctx, &parsed, pubkey.data(), pubkey.size())) {
            return err(ErrorKind::Invalid, "Invalid public key");
        }
        // custom hash copies the X coordinate (elliptic returns the raw X)
        const auto copyX = [](unsigned char* output, const unsigned char* x32,
                              const unsigned char* /*y32*/, void* /*data*/) {
            std::memcpy(output, x32, 32);
            return 1;
        };
        if (!secp256k1_ecdh(ctx, x.data(), &parsed, secret.data(), copyX, nullptr)) {
            return err(ErrorKind::Invalid, "Failed to derive shared secret");
        }
    } else if (type == KeyType::R1) {
        std::array<uint8_t, 65> session{};
        if (ecdh_multiply(&nist256p1, secret.data(), pubkey.data(), session.data()) != 0) {
            return err(ErrorKind::Invalid, "Failed to derive shared secret");
        }
        std::copy(session.begin() + 1, session.begin() + 33, x.begin());
    } else {
        return err(ErrorKind::Unsupported, "Cannot derive WA shared secrets");
    }

    // elliptic encodes the X coordinate as minimal big-endian (leading zeros dropped)
    size_t start = 0;
    while (start < 31 && x[start] == 0) {
        ++start;
    }
    return std::vector<uint8_t>(x.begin() + static_cast<long>(start), x.end());
}

bool verify(std::span<const uint8_t> signature, std::span<const uint8_t> message,
            std::span<const uint8_t> pubkey, KeyType type) {
    if (signature.size() < 65 || message.size() != 32 || pubkey.size() < 33) {
        return false;
    }
    if (type == KeyType::K1) {
        secp256k1_context* ctx = secpContext();
        secp256k1_pubkey parsed;
        if (!secp256k1_ec_pubkey_parse(ctx, &parsed, pubkey.data(), 33)) {
            return false;
        }
        secp256k1_ecdsa_signature sig;
        if (!secp256k1_ecdsa_signature_parse_compact(ctx, &sig, signature.data() + 1)) {
            return false;
        }
        secp256k1_ecdsa_signature_normalize(ctx, &sig, &sig);
        return secp256k1_ecdsa_verify(ctx, &sig, message.data(), &parsed) == 1;
    }
    // R1 and WA both verify on the p256 curve
    std::array<uint8_t, 64> compact{};
    std::copy(signature.begin() + 1, signature.begin() + 65, compact.begin());
    return ecdsa_verify_digest(&nist256p1, pubkey.data(), compact.data(), message.data()) == 0;
}

Result<std::array<uint8_t, 32>> generate(KeyType type) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        DK_TRY(bytes, secureRandom(32));
        std::array<uint8_t, 32> key{};
        std::copy(bytes.begin(), bytes.end(), key.begin());
        bool valid = false;
        if (type == KeyType::K1) {
            valid = secp256k1_ec_seckey_verify(secpContext(), key.data()) == 1;
        } else {
            // 0 < key < nist256p1 order; a random 256-bit value is below the
            // order with overwhelming probability, so only reject all-zero
            valid = std::any_of(key.begin(), key.end(), [](uint8_t b) { return b != 0; });
        }
        if (valid) return key;
    }
    return err(ErrorKind::Internal, "Failed to generate valid private key");
}

}  // namespace dwarfkit::crypto
