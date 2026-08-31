// Port of antelope src/chain/signature.ts
#pragma once

#include <string>
#include <string_view>

#include <dwarfkit/antelope/chain/bytes.hpp>
#include <dwarfkit/antelope/chain/checksum.hpp>
#include <dwarfkit/antelope/chain/key_type.hpp>
#include <dwarfkit/antelope/chain/public_key.hpp>
#include <dwarfkit/antelope/crypto.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

class Signature {
public:
    static constexpr std::string_view abiName = "signature";

    // Type, e.g. K1
    KeyType type = KeyType::K1;
    // Signature data (recid || r || s for K1/R1).
    Bytes data;

    Signature() = default;
    Signature(KeyType type, Bytes data) : type(type), data(std::move(data)) {}

    static Signature from(const crypto::SignatureParts& parts);
    static Result<Signature> from(std::string_view value);

    bool equals(const Signature& other) const {
        return type == other.type && data.equals(other.data);
    }
    bool operator==(const Signature&) const = default;

    // Recover public key from a message digest.
    Result<PublicKey> recoverDigest(const Checksum256& digest) const;
    // Recover public key from a message.
    Result<PublicKey> recoverMessage(std::span<const uint8_t> message) const {
        return recoverDigest(Checksum256::hash(message));
    }

    // Verify this signature with a message digest and public key.
    bool verifyDigest(const Checksum256& digest, const PublicKey& publicKey) const;
    // Verify this signature with a message and public key.
    bool verifyMessage(std::span<const uint8_t> message, const PublicKey& publicKey) const {
        return verifyDigest(Checksum256::hash(message), publicKey);
    }

    // Base58check encoded string (SIG_<type>_<data>).
    std::string toString() const;

    json toJSON() const { return toString(); }
};

}  // namespace dwarfkit
