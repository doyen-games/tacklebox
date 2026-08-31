#include <dwarfkit/antelope/chain/private_key.hpp>

#include <algorithm>

#include <dwarfkit/antelope/base58.hpp>
#include <dwarfkit/antelope/crypto.hpp>

namespace dwarfkit {

namespace {

std::vector<std::string_view> split(std::string_view value, char delim) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        const size_t pos = value.find(delim, start);
        if (pos == std::string_view::npos) {
            parts.push_back(value.substr(start));
            break;
        }
        parts.push_back(value.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

struct DecodedKey {
    KeyType type;
    Bytes data;
};

Result<DecodedKey> decodeKey(std::string_view value) {
    if (value.starts_with("PVT_")) {
        // Antelope/EOSIO format
        const auto parts = split(value, '_');
        if (parts.size() != 3) {
            return err(ErrorKind::Invalid, "Invalid PVT format");
        }
        DK_TRY(type, keytype::from(parts[1]));
        const std::optional<size_t> size =
            (type == KeyType::K1 || type == KeyType::R1) ? std::optional<size_t>(32) : std::nullopt;
        DK_TRY(data, Base58::decodeRipemd160Check(parts[2], size, keytype::toString(type)));
        return DecodedKey{type, std::move(data)};
    }
    // WIF format
    DK_TRY(data, Base58::decodeCheck(value));
    if (data.array.empty() || data.array[0] != 0x80) {
        return err(ErrorKind::Invalid, "Invalid WIF");
    }
    return DecodedKey{KeyType::K1, data.droppingFirst()};
}

}  // namespace

Result<PrivateKey> PrivateKey::make(KeyType type, Bytes data) {
    if ((type == KeyType::K1 || type == KeyType::R1) && data.length() != 32) {
        return err(ErrorKind::Invalid, "Invalid private key (Invalid private key length)");
    }
    return PrivateKey(type, std::move(data));
}

Result<PrivateKey> PrivateKey::fromString(std::string_view value, bool ignoreChecksumError) {
    auto decoded = decodeKey(value);
    if (decoded) {
        if (isAllZero(decoded->data)) {
            return err(ErrorKind::Invalid,
                       "Invalid private key (All-zero private key is not allowed)");
        }
        return make(decoded->type, std::move(decoded->data));
    }

    const Error& error = decoded.error();
    const bool isChecksum = error.details.is_object() && error.details.contains("code") &&
                            error.details["code"] == "E_CHECKSUM";
    if (ignoreChecksumError && isChecksum && error.details.contains("data")) {
        const KeyType type = value.starts_with("PVT_R1") ? KeyType::R1 : KeyType::K1;
        DK_TRY(data, Bytes::from(error.details["data"].get<std::string>()));
        if (data.length() == 33) {
            data.dropFirst();
        }
        data.zeropad(32, true);
        if (isAllZero(data)) {
            return err(ErrorKind::Invalid,
                       "Invalid private key (All-zero private key is not allowed)");
        }
        return make(type, std::move(data));
    }
    // wrap the message like upstream but preserve the decoder's details/code
    return err(Error{error.kind, "Invalid private key (" + error.message + ")", error.code,
                     error.details});
}

Result<PrivateKey> PrivateKey::generate(KeyType type) {
    DK_TRY(key, crypto::generate(type));
    Bytes data(std::vector<uint8_t>(key.begin(), key.end()));
    if (isAllZero(data)) {
        return err(ErrorKind::Internal,
                   "Failed to generate valid private key: Maximum retries exceeded");
    }
    return PrivateKey(type, std::move(data));
}

Result<PrivateKey> PrivateKey::generate(std::string_view type) {
    DK_TRY(keyType, keytype::from(type));
    return generate(keyType);
}

Result<Signature> PrivateKey::signDigest(const Checksum256& digest) const {
    DK_TRY(parts, crypto::sign(data.array, digest.array, type));
    return Signature::from(parts);
}

Result<Checksum512> PrivateKey::sharedSecret(const PublicKey& publicKey) const {
    DK_TRY(shared, crypto::sharedSecret(data.array, publicKey.data.array, type));
    return Checksum512::hash(shared);
}

Result<PublicKey> PrivateKey::toPublic() const {
    DK_TRY(compressed, crypto::getPublic(data.array, type));
    return PublicKey(type, Bytes(std::vector<uint8_t>(compressed.begin(), compressed.end())));
}

Result<std::string> PrivateKey::toWif() const {
    if (type != KeyType::K1) {
        return err(ErrorKind::Invalid, "Unable to generate WIF for non-k1 key");
    }
    return Base58::encodeCheck(Bytes(std::vector<uint8_t>{0x80}).appending(data));
}

std::string PrivateKey::toString() const {
    return "PVT_" + std::string(keytype::toString(type)) + "_" +
           Base58::encodeRipemd160Check(data, keytype::toString(type));
}

}  // namespace dwarfkit
