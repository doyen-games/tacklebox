#include <dwarfkit/antelope/chain/signature.hpp>

#include <dwarfkit/antelope/base58.hpp>

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

}  // namespace

Signature Signature::from(const crypto::SignatureParts& parts) {
    std::vector<uint8_t> data(1 + 32 + 32);
    int recid = parts.recid;
    if (parts.type == KeyType::K1 || parts.type == KeyType::R1) {
        recid += 31;
    }
    data[0] = static_cast<uint8_t>(recid);
    std::copy(parts.r.begin(), parts.r.end(), data.begin() + 1);
    std::copy(parts.s.begin(), parts.s.end(), data.begin() + 33);
    return Signature(parts.type, Bytes(std::move(data)));
}

Result<Signature> Signature::from(std::string_view value) {
    if (value.starts_with("SIG_")) {
        const auto parts = split(value, '_');
        if (parts.size() != 3) {
            return err(ErrorKind::Invalid, "Invalid signature string");
        }
        DK_TRY(type, keytype::from(parts[1]));
        const std::optional<size_t> size =
            (type == KeyType::K1 || type == KeyType::R1) ? std::optional<size_t>(65) : std::nullopt;
        DK_TRY(data, Base58::decodeRipemd160Check(parts[2], size, keytype::toString(type)));
        return Signature(type, std::move(data));
    }
    return err(ErrorKind::Invalid, "Invalid signature string");
}

Result<PublicKey> Signature::recoverDigest(const Checksum256& digest) const {
    DK_TRY(compressed, crypto::recover(data.array, digest.array, type));
    return PublicKey(type, Bytes(std::vector<uint8_t>(compressed.begin(), compressed.end())));
}

bool Signature::verifyDigest(const Checksum256& digest, const PublicKey& publicKey) const {
    return crypto::verify(data.array, digest.array, publicKey.getCompressedKeyBytes().array, type);
}

std::string Signature::toString() const {
    return "SIG_" + std::string(keytype::toString(type)) + "_" +
           Base58::encodeRipemd160Check(data, keytype::toString(type));
}

}  // namespace dwarfkit
