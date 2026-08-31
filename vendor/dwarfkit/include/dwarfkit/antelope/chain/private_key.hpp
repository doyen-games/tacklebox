// Port of antelope src/chain/private-key.ts
#pragma once

#include <string>
#include <string_view>

#include <dwarfkit/antelope/chain/bytes.hpp>
#include <dwarfkit/antelope/chain/checksum.hpp>
#include <dwarfkit/antelope/chain/key_type.hpp>
#include <dwarfkit/antelope/chain/public_key.hpp>
#include <dwarfkit/antelope/chain/signature.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

class PrivateKey {
public:
    KeyType type = KeyType::K1;
    Bytes data;

    PrivateKey() = default;
    PrivateKey(KeyType type, Bytes data) : type(type), data(std::move(data)) {}

    // Create from a WIF (5...) or Antelope/EOSIO (PVT_...) string.
    static Result<PrivateKey> from(std::string_view value) { return fromString(value); }
    static Result<PrivateKey> fromString(std::string_view value, bool ignoreChecksumError = false);

    // Generate a new PrivateKey. Errors if a secure random source isn't available.
    static Result<PrivateKey> generate(KeyType type);
    static Result<PrivateKey> generate(std::string_view type);

    static bool isAllZero(const Bytes& data) {
        return std::all_of(data.array.begin(), data.array.end(), [](uint8_t b) { return b == 0; });
    }

    // Validate the K1/R1 length invariant the constructor enforces upstream.
    static Result<PrivateKey> make(KeyType type, Bytes data);

    // Sign a message digest. Errors if the key type isn't R1 or K1.
    Result<Signature> signDigest(const Checksum256& digest) const;
    // Sign a message. Errors if the key type isn't R1 or K1.
    Result<Signature> signMessage(std::span<const uint8_t> message) const {
        return signDigest(Checksum256::hash(message));
    }

    // Derive the shared secret with a public key. Errors if the key type isn't R1 or K1.
    Result<Checksum512> sharedSecret(const PublicKey& publicKey) const;

    // Corresponding public key. Errors if the key type isn't R1 or K1.
    Result<PublicKey> toPublic() const;

    // WIF representation. Errors if the key type isn't K1.
    Result<std::string> toWif() const;

    // Antelope/EOSIO PVT_<type>_<base58check> format.
    std::string toString() const;

    json toJSON() const { return toString(); }
};

}  // namespace dwarfkit
