#include <dwarfkit/antelope/chain/public_key.hpp>

#include <algorithm>

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

int compareBytes(std::span<const uint8_t> a, std::span<const uint8_t> b) {
    const size_t length = std::min(a.size(), b.size());
    for (size_t i = 0; i < length; i++) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

std::span<const uint8_t> readRpid(std::span<const uint8_t> data) {
    // skip the varuint32 that length-prefixes the rpid on the wire
    size_t offset = 34;
    while (offset < data.size() && (data[offset] & 0x80)) {
        offset++;
    }
    offset++;
    return offset <= data.size() ? data.subspan(offset) : std::span<const uint8_t>{};
}

int compareWebAuthn(std::span<const uint8_t> a, std::span<const uint8_t> b) {
    const int prefix = compareBytes(a.subspan(0, std::min<size_t>(34, a.size())),
                                    b.subspan(0, std::min<size_t>(34, b.size())));
    if (prefix != 0) return prefix;
    return compareBytes(readRpid(a), readRpid(b));
}

}  // namespace

Result<PublicKey> PublicKey::from(std::string_view value) {
    if (value.starts_with("PUB_")) {
        const auto parts = split(value, '_');
        if (parts.size() != 3) {
            return err(ErrorKind::Invalid, "Invalid public key string");
        }
        DK_TRY(type, keytype::from(parts[1]));
        const std::optional<size_t> size =
            (type == KeyType::K1 || type == KeyType::R1) ? std::optional<size_t>(33) : std::nullopt;
        DK_TRY(data, Base58::decodeRipemd160Check(parts[2], size, keytype::toString(type)));
        return PublicKey(type, std::move(data));
    }
    if (value.size() >= 50) {
        // Legacy EOS key
        DK_TRY(data, Base58::decodeRipemd160Check(value.substr(value.size() - 50)));
        return PublicKey(KeyType::K1, std::move(data));
    }
    return err(ErrorKind::Invalid, "Invalid public key string");
}

int PublicKey::compare(const PublicKey& other) const {
    const int type_ = keytype::indexFor(type).value();
    const int otherType = keytype::indexFor(other.type).value();
    if (type_ != otherType) {
        return type_ < otherType ? -1 : 1;
    }
    if (type == KeyType::WA) {
        return compareWebAuthn(data.array, other.data.array);
    }
    return compareBytes(data.array, other.data.array);
}

Result<std::string> PublicKey::toLegacyString(std::string_view prefix) const {
    if (type != KeyType::K1) {
        return err(ErrorKind::Invalid, "Unable to create legacy formatted string for non-K1 key");
    }
    return std::string(prefix) + Base58::encodeRipemd160Check(data);
}

std::string PublicKey::toString() const {
    return "PUB_" + std::string(keytype::toString(type)) + "_" +
           Base58::encodeRipemd160Check(data, keytype::toString(type));
}

}  // namespace dwarfkit
