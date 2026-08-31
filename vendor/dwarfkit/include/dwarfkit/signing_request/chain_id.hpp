// Port of signing-request src/chain-id.ts
#pragma once

#include <dwarfkit/antelope/serializer.hpp>

namespace dwarfkit {

// Chain ID aliases.
enum class ChainName : uint8_t {
    UNKNOWN = 0,
    EOS = 1,
    TELOS = 2,
    JUNGLE = 3,
    KYLIN = 4,
    WORBLI = 5,
    BOS = 6,
    MEETONE = 7,
    INSIGHTS = 8,
    BEOS = 9,
    WAX = 10,
    PROTON = 11,
    FIO = 12,
};

DK_TYPE_ALIAS(ChainAlias, "chain_alias", uint8_t)

class ChainId;
DK_VARIANT(ChainIdVariant, "variant_id", ChainAlias, ChainId)

class ChainId : public Checksum256 {
public:
    using Checksum256::Checksum256;
    ChainId() = default;
    ChainId(const Checksum256& sum) : Checksum256(sum) {}  // NOLINT(runtime/explicit)

    static Result<ChainId> from(ChainName alias);
    static Result<ChainId> from(uint8_t alias) { return from(static_cast<ChainName>(alias)); }
    static Result<ChainId> from(std::string_view hex);
    static ChainId from(const Checksum256& sum) { return ChainId(sum); }

    ChainIdVariant chainVariant() const;
    ChainName chainName() const;
};

// The chain id of a ChainIdVariant; errors on the UNKNOWN alias.
Result<ChainId> variantChainId(const ChainIdVariant& variant);

// ChainId serializes as a checksum256 under the typedef name chain_id.
template <>
struct abi_traits<ChainId, void> {
    static constexpr std::string_view abiName = "chain_id";
    static Result<void> toABI(const ChainId& v, ABIEncoder& e) {
        e.writeArray(v.array);
        return {};
    }
    static Result<ChainId> fromABI(ABIDecoder& d) {
        DK_TRY(sum, abi_traits<Checksum256>::fromABI(d));
        return ChainId(sum);
    }
    static json toJSON(const ChainId& v) { return v.toJSON(); }
    static Result<ChainId> fromJSON(const json& j) {
        DK_TRY(sum, Checksum256::from(j.get<std::string>()));
        return ChainId(sum);
    }
    static ChainId abiDefault() { return {}; }
};

template <>
struct abi_collect<ChainId, void> {
    static void collect(ABI& abi, std::set<std::string>& seen) {
        if (seen.contains("chain_id")) return;
        seen.insert("chain_id");
        abi.types.push_back({"chain_id", "checksum256"});
    }
};

}  // namespace dwarfkit
