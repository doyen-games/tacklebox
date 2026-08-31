// Port of antelope src/chain/public-key.ts
#pragma once

#include <string>
#include <string_view>

#include <dwarfkit/antelope/chain/bytes.hpp>
#include <dwarfkit/antelope/chain/key_type.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

class PublicKey {
public:
    static constexpr std::string_view abiName = "public_key";

    // Type, e.g. K1
    KeyType type = KeyType::K1;
    // Compressed public key point.
    Bytes data;

    PublicKey() = default;
    PublicKey(KeyType type, Bytes data) : type(type), data(std::move(data)) {}

    // Create PublicKey object from a modern (PUB_<type>_...) or legacy (EOS...)
    // string.
    static Result<PublicKey> from(std::string_view value);
    static PublicKey from(KeyType type, const Bytes& compressed) { return PublicKey(type, compressed); }

    // The core 33-byte compressed public key data, suitable for verification.
    Bytes getCompressedKeyBytes() const {
        return type == KeyType::WA ? Bytes(std::vector<uint8_t>(data.array.begin(),
                                                                data.array.begin() + 33))
                                   : data;
    }

    bool equals(const PublicKey& other) const {
        return type == other.type && data.equals(other.data);
    }
    bool operator==(const PublicKey&) const = default;

    // Ordering nodeos enforces on authority keys.
    int compare(const PublicKey& other) const;

    // Antelope/EOSIO legacy (EOS<base58data>) formatted key. Errors on non-K1.
    Result<std::string> toLegacyString(std::string_view prefix = "EOS") const;

    // Modern Antelope/EOSIO format (PUB_<type>_<base58data>).
    std::string toString() const;

    json toJSON() const { return toString(); }
};

}  // namespace dwarfkit
