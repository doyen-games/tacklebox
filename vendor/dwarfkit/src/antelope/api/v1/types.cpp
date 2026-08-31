#include <dwarfkit/antelope/api/v1/types.hpp>

#include <dwarfkit/core/zlib.hpp>

namespace dwarfkit::api::v1 {

Result<TrxVariant> TrxVariant::from(const json& data) {
    TrxVariant rv;
    if (data.is_string()) {
        DK_TRY(id, Checksum256::from(data.get<std::string>()));
        rv.id = id;
        rv.extra = json::object();
    } else if (data.is_object()) {
        DK_TRY(id, Checksum256::from(data.value("id", "")));
        rv.id = id;
        rv.extra = data;
    } else {
        return err(ErrorKind::Invalid, "Invalid trx variant");
    }
    return rv;
}

Result<std::optional<Transaction>> TrxVariant::transaction() const {
    if (!extra.contains("packed_trx")) {
        return std::optional<Transaction>{};
    }
    DK_TRY(packed, Bytes::from(extra.value("packed_trx", "")));
    const std::string compression = extra.value("compression", "");
    if (compression == "zlib") {
        DK_TRY(inflated, zlibUncompress(packed.array));
        DK_TRY(tx, Serializer::decode<Transaction>(std::span<const uint8_t>(inflated)));
        return std::optional<Transaction>(std::move(tx));
    }
    if (compression == "none") {
        DK_TRY(tx, Serializer::decode<Transaction>(packed));
        return std::optional<Transaction>(std::move(tx));
    }
    return err(ErrorKind::Invalid, "Unsupported compression type " + compression);
}

Result<std::optional<std::vector<Signature>>> TrxVariant::signatures() const {
    if (!extra.contains("signatures")) {
        return std::optional<std::vector<Signature>>{};
    }
    DK_TRY(signatures, abi_traits<std::vector<Signature>>::fromJSON(extra.at("signatures")));
    return std::optional<std::vector<Signature>>(std::move(signatures));
}

TransactionHeader GetInfoResponse::getTransactionHeader(uint32_t secondsAhead) const {
    const auto expiration = TimePointSec::fromMilliseconds(
        static_cast<double>(head_block_time.toMilliseconds() + int64_t(secondsAhead) * 1000));
    const auto& id = last_irreversible_block_id.array;
    // lower 32 bits of the block id, little endian, bytes 8..12
    const uint32_t prefix = static_cast<uint32_t>(id[8]) | (static_cast<uint32_t>(id[9]) << 8) |
                            (static_cast<uint32_t>(id[10]) << 16) |
                            (static_cast<uint32_t>(id[11]) << 24);
    TransactionHeader header;
    header.expiration = expiration;
    header.ref_block_num = static_cast<uint16_t>(last_irreversible_block_num & 0xffff);
    header.ref_block_prefix = prefix;
    return header;
}

Result<ProducerAuthority> Producer::producerAuthority() const {
    if (!authority.is_array() || authority.size() != 2) {
        return err(ErrorKind::Invalid, "Invalid producer authority");
    }
    return abi_traits<ProducerAuthority>::fromJSON(authority[1]);
}

}  // namespace dwarfkit::api::v1
