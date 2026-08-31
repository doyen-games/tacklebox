// abi_traits specializations for every builtin and the generic
// struct/variant/alias/array/optional/extension traits. Ports the encoding in
// each chain type's toABI/fromABI/toJSON plus serializer/builtins.ts.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <dwarfkit/antelope/chain/asset.hpp>
#include <dwarfkit/antelope/chain/blob.hpp>
#include <dwarfkit/antelope/chain/block_id.hpp>
#include <dwarfkit/antelope/chain/bytes.hpp>
#include <dwarfkit/antelope/chain/checksum.hpp>
#include <dwarfkit/antelope/chain/float.hpp>
#include <dwarfkit/antelope/chain/integer.hpp>
#include <dwarfkit/antelope/chain/name.hpp>
#include <dwarfkit/antelope/chain/permission_level.hpp>
#include <dwarfkit/antelope/chain/private_key.hpp>
#include <dwarfkit/antelope/chain/public_key.hpp>
#include <dwarfkit/antelope/chain/signature.hpp>
#include <dwarfkit/antelope/chain/time.hpp>
#include <dwarfkit/antelope/serializer/serializable.hpp>

namespace dwarfkit {

// ---- helpers ---------------------------------------------------------------

namespace detail {

template <class T>
Result<T> jsonToInt(const json& j) {
    if (j.is_string()) {
        // parse decimal string (covers the >32-bit toJSON form)
        const std::string s = j.get<std::string>();
        try {
            if constexpr (std::is_signed_v<T>) {
                return static_cast<T>(std::stoll(s));
            } else {
                return static_cast<T>(std::stoull(s));
            }
        } catch (...) {
            return err(ErrorKind::Invalid, "Invalid number");
        }
    }
    if (j.is_number()) {
        return j.get<T>();
    }
    return err(ErrorKind::Invalid, "Expected number");
}

}  // namespace detail

// ---- bool / string ---------------------------------------------------------

template <>
struct abi_traits<bool> {
    static constexpr std::string_view abiName = "bool";
    static Result<void> toABI(bool v, ABIEncoder& e) {
        e.writeByte(v ? 1 : 0);
        return {};
    }
    static Result<bool> fromABI(ABIDecoder& d) {
        DK_TRY(b, d.readByte());
        return b == 1;
    }
    static json toJSON(bool v) { return v; }
    static Result<bool> fromJSON(const json& j) {
        if (j.is_boolean()) return j.get<bool>();
        // nodeos also emits 0/1 for booleans; upstream's BoolType.from passes
        // any value through
        if (j.is_number()) return j.get<double>() != 0;
        return err(ErrorKind::Invalid, "Expected bool");
    }
    static bool abiDefault() { return false; }
};

template <>
struct abi_traits<std::string> {
    static constexpr std::string_view abiName = "string";
    static Result<void> toABI(const std::string& v, ABIEncoder& e) {
        e.writeString(v);
        return {};
    }
    static Result<std::string> fromABI(ABIDecoder& d) { return d.readString(); }
    static json toJSON(const std::string& v) { return v; }
    static Result<std::string> fromJSON(const json& j) {
        if (!j.is_string()) return err(ErrorKind::Invalid, "Expected string");
        return j.get<std::string>();
    }
    static std::string abiDefault() { return ""; }
};

// ---- native integers -------------------------------------------------------

namespace detail {
template <class T>
consteval std::string_view intAbiName() {
    if constexpr (std::is_same_v<T, int8_t>) return "int8";
    else if constexpr (std::is_same_v<T, int16_t>) return "int16";
    else if constexpr (std::is_same_v<T, int32_t>) return "int32";
    else if constexpr (std::is_same_v<T, int64_t>) return "int64";
    else if constexpr (std::is_same_v<T, uint8_t>) return "uint8";
    else if constexpr (std::is_same_v<T, uint16_t>) return "uint16";
    else if constexpr (std::is_same_v<T, uint32_t>) return "uint32";
    else return "uint64";
}
}  // namespace detail

template <class T>
    requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
struct abi_traits<T> {
    static constexpr std::string_view abiName = detail::intAbiName<T>();
    static Result<void> toABI(T v, ABIEncoder& e) {
        e.writeInt(v);
        return {};
    }
    static Result<T> fromABI(ABIDecoder& d) { return d.readInt<T>(); }
    static json toJSON(T v) {
        if constexpr (sizeof(T) >= 8) {
            return intToJSON(v);
        } else {
            return v;
        }
    }
    static Result<T> fromJSON(const json& j) { return detail::jsonToInt<T>(j); }
    static T abiDefault() { return 0; }
};

// ---- floats ----------------------------------------------------------------

namespace detail {
// JavaScript Number.prototype.toString: shortest round-trip decimal
std::string jsNumberToString(double v);
// JavaScript Number.prototype.toFixed(7), the Float32 string form
std::string toFixed7(double v);
}  // namespace detail

// Wharfkit's Float32/Float64 toJSON produce strings (Float.toJSON -> toString).
template <>
struct abi_traits<float> {
    static constexpr std::string_view abiName = "float32";
    static Result<void> toABI(float v, ABIEncoder& e) {
        e.writeFloat32(v);
        return {};
    }
    static Result<float> fromABI(ABIDecoder& d) { return d.readFloat32(); }
    static json toJSON(float v) { return detail::toFixed7(static_cast<double>(v)); }
    static Result<float> fromJSON(const json& j) {
        if (j.is_string()) return std::stof(j.get<std::string>());
        return j.get<float>();
    }
    static float abiDefault() { return 0; }
};

template <>
struct abi_traits<double> {
    static constexpr std::string_view abiName = "float64";
    static Result<void> toABI(double v, ABIEncoder& e) {
        e.writeFloat64(v);
        return {};
    }
    static Result<double> fromABI(ABIDecoder& d) { return d.readFloat64(); }
    static json toJSON(double v) { return detail::jsNumberToString(v); }
    static Result<double> fromJSON(const json& j) {
        if (j.is_string()) return std::stod(j.get<std::string>());
        return j.get<double>();
    }
    static double abiDefault() { return 0; }
};

// ---- any (json passthrough; object coding only) ----------------------------

template <>
struct abi_traits<json> {
    static constexpr std::string_view abiName = "any";
    static Result<void> toABI(const json&, ABIEncoder&) {
        return err(ErrorKind::Invalid, "Unable to encode any type to binary");
    }
    static Result<json> fromABI(ABIDecoder&) {
        return err(ErrorKind::Invalid, "Unable to decode 'any' type from binary");
    }
    static json toJSON(const json& v) { return v; }
    static Result<json> fromJSON(const json& j) { return j; }
    static json abiDefault() { return nullptr; }
};

// ---- 128-bit and varints ---------------------------------------------------

template <>
struct abi_traits<UInt128> {
    static constexpr std::string_view abiName = "uint128";
    static Result<void> toABI(const UInt128& v, ABIEncoder& e) {
        e.writeArray(v.byteArray());
        return {};
    }
    static Result<UInt128> fromABI(ABIDecoder& d) {
        DK_TRY(bytes, d.readArray(16));
        return UInt128::fromByteArray(std::span<const uint8_t, 16>(bytes.data(), 16));
    }
    static json toJSON(const UInt128& v) { return v.toJSON(); }
    static Result<UInt128> fromJSON(const json& j) {
        if (j.is_number()) return UInt128::from(static_cast<uint64_t>(j.get<uint64_t>()));
        return UInt128::from(j.get<std::string>());
    }
    static UInt128 abiDefault() { return {}; }
};

template <>
struct abi_traits<Int128> {
    static constexpr std::string_view abiName = "int128";
    static Result<void> toABI(const Int128& v, ABIEncoder& e) {
        e.writeArray(v.byteArray());
        return {};
    }
    static Result<Int128> fromABI(ABIDecoder& d) {
        DK_TRY(bytes, d.readArray(16));
        return Int128::fromByteArray(std::span<const uint8_t, 16>(bytes.data(), 16));
    }
    static json toJSON(const Int128& v) { return v.toJSON(); }
    static Result<Int128> fromJSON(const json& j) {
        if (j.is_number()) return Int128::from(static_cast<int64_t>(j.get<int64_t>()));
        return Int128::from(j.get<std::string>());
    }
    static Int128 abiDefault() { return {}; }
};

template <>
struct abi_traits<VarInt> {
    static constexpr std::string_view abiName = "varint32";
    static Result<void> toABI(const VarInt& v, ABIEncoder& e) {
        e.writeVarint32(v.value);
        return {};
    }
    static Result<VarInt> fromABI(ABIDecoder& d) {
        DK_TRY(v, d.readVarint32());
        return VarInt(v);
    }
    static json toJSON(const VarInt& v) { return v.toJSON(); }
    static Result<VarInt> fromJSON(const json& j) {
        DK_TRY(v, detail::jsonToInt<int32_t>(j));
        return VarInt(v);
    }
    static VarInt abiDefault() { return {}; }
};

template <>
struct abi_traits<VarUInt> {
    static constexpr std::string_view abiName = "varuint32";
    static Result<void> toABI(const VarUInt& v, ABIEncoder& e) {
        e.writeVaruint32(v.value);
        return {};
    }
    static Result<VarUInt> fromABI(ABIDecoder& d) {
        DK_TRY(v, d.readVaruint32());
        return VarUInt(v);
    }
    static json toJSON(const VarUInt& v) { return v.toJSON(); }
    static Result<VarUInt> fromJSON(const json& j) {
        DK_TRY(v, detail::jsonToInt<uint32_t>(j));
        return VarUInt(v);
    }
    static VarUInt abiDefault() { return {}; }
};

// ---- chain value types -----------------------------------------------------

template <>
struct abi_traits<Name> {
    static constexpr std::string_view abiName = "name";
    static Result<void> toABI(const Name& v, ABIEncoder& e) {
        e.writeInt<uint64_t>(v.value);
        return {};
    }
    static Result<Name> fromABI(ABIDecoder& d) {
        DK_TRY(v, d.readInt<uint64_t>());
        return Name(v);
    }
    static json toJSON(const Name& v) { return v.toJSON(); }
    static Result<Name> fromJSON(const json& j) { return Name::from(j.get<std::string>()); }
    static Name abiDefault() { return {}; }
};

template <>
struct abi_traits<Asset::Symbol> {
    static constexpr std::string_view abiName = "symbol";
    static Result<void> toABI(const Asset::Symbol& v, ABIEncoder& e) {
        e.writeInt<uint64_t>(v.value);
        return {};
    }
    static Result<Asset::Symbol> fromABI(ABIDecoder& d) {
        DK_TRY(v, d.readInt<uint64_t>());
        return Asset::Symbol::from(v);
    }
    static json toJSON(const Asset::Symbol& v) { return v.toJSON(); }
    static Result<Asset::Symbol> fromJSON(const json& j) {
        return Asset::Symbol::from(j.get<std::string>());
    }
    static Asset::Symbol abiDefault() { return Asset::Symbol::abiDefault(); }
};

template <>
struct abi_traits<Asset::SymbolCode> {
    static constexpr std::string_view abiName = "symbol_code";
    static Result<void> toABI(const Asset::SymbolCode& v, ABIEncoder& e) {
        e.writeInt<uint64_t>(v.value);
        return {};
    }
    static Result<Asset::SymbolCode> fromABI(ABIDecoder& d) {
        DK_TRY(v, d.readInt<uint64_t>());
        return Asset::SymbolCode::from(v);
    }
    static json toJSON(const Asset::SymbolCode& v) { return v.toJSON(); }
    static Result<Asset::SymbolCode> fromJSON(const json& j) {
        return Asset::SymbolCode::from(j.get<std::string>());
    }
    static Asset::SymbolCode abiDefault() { return Asset::SymbolCode::abiDefault(); }
};

template <>
struct abi_traits<Asset> {
    static constexpr std::string_view abiName = "asset";
    static Result<void> toABI(const Asset& v, ABIEncoder& e) {
        e.writeInt<int64_t>(v.units);
        e.writeInt<uint64_t>(v.symbol.value);
        return {};
    }
    static Result<Asset> fromABI(ABIDecoder& d) {
        DK_TRY(units, d.readInt<int64_t>());
        DK_TRY(rawSym, d.readInt<uint64_t>());
        DK_TRY(symbol, Asset::Symbol::from(rawSym));
        return Asset(units, symbol);
    }
    static json toJSON(const Asset& v) { return v.toJSON(); }
    static Result<Asset> fromJSON(const json& j) { return Asset::from(j.get<std::string>()); }
    static Asset abiDefault() { return Asset::abiDefault(); }
};

template <>
struct abi_traits<ExtendedAsset> {
    static constexpr std::string_view abiName = "extended_asset";
    static Result<void> toABI(const ExtendedAsset& v, ABIEncoder& e) {
        DK_CHECK(abi_traits<Asset>::toABI(v.quantity, e));
        DK_CHECK(abi_traits<Name>::toABI(v.contract, e));
        return {};
    }
    static Result<ExtendedAsset> fromABI(ABIDecoder& d) {
        DK_TRY(quantity, abi_traits<Asset>::fromABI(d));
        DK_TRY(contract, abi_traits<Name>::fromABI(d));
        return ExtendedAsset(quantity, contract);
    }
    static json toJSON(const ExtendedAsset& v) { return v.toJSON(); }
    static Result<ExtendedAsset> fromJSON(const json& j) {
        DK_TRY(quantity, Asset::from(j.at("quantity").get<std::string>()));
        DK_TRY(contract, abi_traits<Name>::fromJSON(j.at("contract")));
        return ExtendedAsset(quantity, contract);
    }
    static ExtendedAsset abiDefault() { return {}; }
};

template <>
struct abi_traits<Bytes> {
    static constexpr std::string_view abiName = "bytes";
    static Result<void> toABI(const Bytes& v, ABIEncoder& e) {
        e.writeVaruint32(static_cast<uint32_t>(v.array.size()));
        e.writeArray(v.array);
        return {};
    }
    static Result<Bytes> fromABI(ABIDecoder& d) {
        DK_TRY(len, d.readVaruint32());
        DK_TRY(data, d.readArray(len));
        return Bytes(data);
    }
    static json toJSON(const Bytes& v) { return v.toJSON(); }
    static Result<Bytes> fromJSON(const json& j) { return Bytes::from(j.get<std::string>()); }
    static Bytes abiDefault() { return {}; }
};

template <class C, std::string_view (*Name_)()>
struct checksum_traits;  // unused placeholder to keep the pattern uniform

template <size_t N, class Derived>
struct checksum_traits_impl {
    static Result<void> toABI(const Derived& v, ABIEncoder& e) {
        e.writeArray(v.array);
        return {};
    }
    static Result<Derived> fromABI(ABIDecoder& d) {
        DK_TRY(data, d.readArray(N));
        return Derived::from(data);
    }
    static json toJSON(const Derived& v) { return v.toJSON(); }
    static Result<Derived> fromJSON(const json& j) { return Derived::from(j.get<std::string>()); }
    static Derived abiDefault() { return Derived::abiDefault(); }
};

template <>
struct abi_traits<Checksum160> : checksum_traits_impl<20, Checksum160> {
    static constexpr std::string_view abiName = "checksum160";
};
template <>
struct abi_traits<Checksum256> : checksum_traits_impl<32, Checksum256> {
    static constexpr std::string_view abiName = "checksum256";
};
template <>
struct abi_traits<Checksum512> : checksum_traits_impl<64, Checksum512> {
    static constexpr std::string_view abiName = "checksum512";
};

// Blob is base64 in JSON and raw bytes on the wire (no length prefix); binary
// decode has no length information so it reads to the end of the buffer.
template <>
struct abi_traits<Blob> {
    static constexpr std::string_view abiName = "blob";
    static Result<void> toABI(const Blob& v, ABIEncoder& e) {
        e.writeArray(v.array);
        return {};
    }
    static Result<Blob> fromABI(ABIDecoder&) {
        return err(ErrorKind::Unsupported, "blob has no binary decoding");
    }
    static json toJSON(const Blob& v) { return v.toJSON(); }
    static Result<Blob> fromJSON(const json& j) { return Blob::from(j.get<std::string>()); }
    static Blob abiDefault() { return {}; }
};

template <>
struct abi_traits<BlockId> {
    static constexpr std::string_view abiName = "block_id_type";
    static Result<void> toABI(const BlockId& v, ABIEncoder& e) {
        e.writeArray(v.array);
        return {};
    }
    static Result<BlockId> fromABI(ABIDecoder& d) {
        DK_TRY(data, d.readArray(32));
        return BlockId::from(data);
    }
    static json toJSON(const BlockId& v) { return v.toJSON(); }
    static Result<BlockId> fromJSON(const json& j) { return BlockId::from(j.get<std::string>()); }
    static BlockId abiDefault() { return {}; }
};

template <>
struct abi_traits<Float128> {
    static constexpr std::string_view abiName = "float128";
    static Result<void> toABI(const Float128& v, ABIEncoder& e) {
        e.writeArray(v.data.array);
        return {};
    }
    static Result<Float128> fromABI(ABIDecoder& d) {
        DK_TRY(data, d.readArray(16));
        return Float128::from(Bytes(data));
    }
    static json toJSON(const Float128& v) { return v.toJSON(); }
    static Result<Float128> fromJSON(const json& j) { return Float128::from(j.get<std::string>()); }
    static Float128 abiDefault() { return {}; }
};

template <>
struct abi_traits<TimePoint> {
    static constexpr std::string_view abiName = "time_point";
    static Result<void> toABI(const TimePoint& v, ABIEncoder& e) {
        e.writeInt<int64_t>(v.value);
        return {};
    }
    static Result<TimePoint> fromABI(ABIDecoder& d) {
        DK_TRY(v, d.readInt<int64_t>());
        return TimePoint(v);
    }
    static json toJSON(const TimePoint& v) { return v.toJSON(); }
    static Result<TimePoint> fromJSON(const json& j) {
        if (j.is_number()) return TimePoint(j.get<int64_t>());
        return TimePoint::from(j.get<std::string>());
    }
    static TimePoint abiDefault() { return {}; }
};

template <>
struct abi_traits<TimePointSec> {
    static constexpr std::string_view abiName = "time_point_sec";
    static Result<void> toABI(const TimePointSec& v, ABIEncoder& e) {
        e.writeInt<uint32_t>(v.value);
        return {};
    }
    static Result<TimePointSec> fromABI(ABIDecoder& d) {
        DK_TRY(v, d.readInt<uint32_t>());
        return TimePointSec(v);
    }
    static json toJSON(const TimePointSec& v) { return v.toJSON(); }
    static Result<TimePointSec> fromJSON(const json& j) {
        if (j.is_number()) return TimePointSec(j.get<uint32_t>());
        return TimePointSec::from(j.get<std::string>());
    }
    static TimePointSec abiDefault() { return {}; }
};

template <>
struct abi_traits<BlockTimestamp> {
    static constexpr std::string_view abiName = "block_timestamp_type";
    static Result<void> toABI(const BlockTimestamp& v, ABIEncoder& e) {
        e.writeInt<uint32_t>(v.value);
        return {};
    }
    static Result<BlockTimestamp> fromABI(ABIDecoder& d) {
        DK_TRY(v, d.readInt<uint32_t>());
        return BlockTimestamp(v);
    }
    static json toJSON(const BlockTimestamp& v) { return v.toJSON(); }
    static Result<BlockTimestamp> fromJSON(const json& j) {
        if (j.is_number()) return BlockTimestamp(j.get<uint32_t>());
        return BlockTimestamp::from(j.get<std::string>());
    }
    static BlockTimestamp abiDefault() { return {}; }
};

template <>
struct abi_traits<PublicKey> {
    static constexpr std::string_view abiName = "public_key";
    static Result<void> toABI(const PublicKey& v, ABIEncoder& e) {
        DK_TRY(idx, keytype::indexFor(v.type));
        e.writeByte(static_cast<uint8_t>(idx));
        e.writeArray(v.data.array);
        return {};
    }
    static Result<PublicKey> fromABI(ABIDecoder& d) {
        DK_TRY(typeByte, d.readByte());
        DK_TRY(type, keytype::from(typeByte));
        if (type == KeyType::WA) {
            const size_t start = d.getPosition();
            DK_CHECK(d.advance(33));  // key_data
            DK_CHECK(d.advance(1));   // user presence
            DK_TRY(rpidLen, d.readVaruint32());
            DK_CHECK(d.advance(rpidLen));  // rpid
            const size_t len = d.getPosition() - start;
            DK_CHECK(d.setPosition(start));
            DK_TRY(data, d.readArray(len));
            return PublicKey(KeyType::WA, Bytes(data));
        }
        DK_TRY(data, d.readArray(33));
        return PublicKey(type, Bytes(data));
    }
    static json toJSON(const PublicKey& v) { return v.toJSON(); }
    static Result<PublicKey> fromJSON(const json& j) { return PublicKey::from(j.get<std::string>()); }
    static PublicKey abiDefault() {
        return PublicKey(KeyType::K1, Bytes(std::vector<uint8_t>(33, 0)));
    }
};

template <>
struct abi_traits<Signature> {
    static constexpr std::string_view abiName = "signature";
    static Result<void> toABI(const Signature& v, ABIEncoder& e) {
        DK_TRY(idx, keytype::indexFor(v.type));
        e.writeByte(static_cast<uint8_t>(idx));
        e.writeArray(v.data.array);
        return {};
    }
    static Result<Signature> fromABI(ABIDecoder& d) {
        DK_TRY(typeByte, d.readByte());
        DK_TRY(type, keytype::from(typeByte));
        if (type == KeyType::WA) {
            const size_t start = d.getPosition();
            DK_CHECK(d.advance(65));  // compact_signature
            DK_TRY(authLen, d.readVaruint32());
            DK_CHECK(d.advance(authLen));  // auth_data
            DK_TRY(jsonLen, d.readVaruint32());
            DK_CHECK(d.advance(jsonLen));  // client_json
            const size_t len = d.getPosition() - start;
            DK_CHECK(d.setPosition(start));
            DK_TRY(data, d.readArray(len));
            return Signature(KeyType::WA, Bytes(data));
        }
        DK_TRY(data, d.readArray(65));
        return Signature(type, Bytes(data));
    }
    static json toJSON(const Signature& v) { return v.toJSON(); }
    static Result<Signature> fromJSON(const json& j) { return Signature::from(j.get<std::string>()); }
    static Signature abiDefault() {
        return Signature(KeyType::K1, Bytes(std::vector<uint8_t>(65, 0)));
    }
};

// ---- generic array / optional / binary extension ---------------------------

template <class T>
struct abi_traits<std::vector<T>> {
    static Result<void> toABI(const std::vector<T>& v, ABIEncoder& e) {
        e.writeVaruint32(static_cast<uint32_t>(v.size()));
        for (const auto& item : v) {
            DK_CHECK(abi_traits<T>::toABI(item, e));
        }
        return {};
    }
    static Result<std::vector<T>> fromABI(ABIDecoder& d) {
        DK_TRY(len, d.readVaruint32());
        std::vector<T> rv;
        rv.reserve(len);
        for (uint32_t i = 0; i < len; i++) {
            DK_TRY(item, abi_traits<T>::fromABI(d));
            rv.push_back(std::move(item));
        }
        return rv;
    }
    static json toJSON(const std::vector<T>& v) {
        json arr = json::array();
        for (const auto& item : v) arr.push_back(abi_traits<T>::toJSON(item));
        return arr;
    }
    static Result<std::vector<T>> fromJSON(const json& j) {
        if (!j.is_array()) return err(ErrorKind::Invalid, "Expected array");
        std::vector<T> rv;
        rv.reserve(j.size());
        for (const auto& item : j) {
            DK_TRY(decoded, abi_traits<T>::fromJSON(item));
            rv.push_back(std::move(decoded));
        }
        return rv;
    }
    static std::vector<T> abiDefault() { return {}; }
};

template <class T>
struct abi_traits<std::optional<T>> {
    static Result<void> toABI(const std::optional<T>& v, ABIEncoder& e) {
        e.writeByte(v ? 1 : 0);
        if (v) {
            DK_CHECK(abi_traits<T>::toABI(*v, e));
        }
        return {};
    }
    static Result<std::optional<T>> fromABI(ABIDecoder& d) {
        DK_TRY(present, d.readByte());
        if (present == 0) return std::optional<T>{};
        DK_TRY(value, abi_traits<T>::fromABI(d));
        return std::optional<T>(std::move(value));
    }
    static json toJSON(const std::optional<T>& v) {
        return v ? abi_traits<T>::toJSON(*v) : json(nullptr);
    }
    static Result<std::optional<T>> fromJSON(const json& j) {
        if (j.is_null()) return std::optional<T>{};
        DK_TRY(value, abi_traits<T>::fromJSON(j));
        return std::optional<T>(std::move(value));
    }
    static std::optional<T> abiDefault() { return std::nullopt; }
};

// Boxed optional: for self-referential optional struct fields where
// std::optional<T> would need a complete type (TS `self?: Complex`). Encodes
// exactly like optional<T>; abi type name is "T?".
template <class T>
struct abi_traits<std::shared_ptr<T>> {
    static Result<void> toABI(const std::shared_ptr<T>& v, ABIEncoder& e) {
        e.writeByte(v ? 1 : 0);
        if (v) {
            DK_CHECK(abi_traits<T>::toABI(*v, e));
        }
        return {};
    }
    static Result<std::shared_ptr<T>> fromABI(ABIDecoder& d) {
        DK_TRY(present, d.readByte());
        if (present == 0) return std::shared_ptr<T>{};
        DK_TRY(value, abi_traits<T>::fromABI(d));
        return std::make_shared<T>(std::move(value));
    }
    static json toJSON(const std::shared_ptr<T>& v) {
        return v ? abi_traits<T>::toJSON(*v) : json(nullptr);
    }
    static Result<std::shared_ptr<T>> fromJSON(const json& j) {
        if (j.is_null()) return std::shared_ptr<T>{};
        DK_TRY(value, abi_traits<T>::fromJSON(j));
        return std::make_shared<T>(std::move(value));
    }
    static std::shared_ptr<T> abiDefault() { return {}; }
};

template <class T>
struct abi_traits<BinaryExtension<T>> {
    static Result<void> toABI(const BinaryExtension<T>& v, ABIEncoder& e) {
        if (v.hasValue()) {
            DK_CHECK(abi_traits<T>::toABI(*v, e));
        }
        return {};
    }
    static Result<BinaryExtension<T>> fromABI(ABIDecoder& d) {
        if (!d.canRead()) {
            if (d.strictExtensions) {
                return BinaryExtension<T>(abi_traits<T>::abiDefault());
            }
            return BinaryExtension<T>{};
        }
        DK_TRY(value, abi_traits<T>::fromABI(d));
        return BinaryExtension<T>(std::move(value));
    }
    static json toJSON(const BinaryExtension<T>& v) {
        return v.hasValue() ? abi_traits<T>::toJSON(*v) : json(nullptr);
    }
    static Result<BinaryExtension<T>> fromJSON(const json& j) {
        if (j.is_null()) return BinaryExtension<T>{};
        DK_TRY(value, abi_traits<T>::fromJSON(j));
        return BinaryExtension<T>(std::move(value));
    }
    static BinaryExtension<T> abiDefault() { return {}; }
};

// ---- generic struct / variant / alias --------------------------------------

// whether a struct field type is a binary extension or optional (may be absent)
template <class T>
inline constexpr bool isAbsentable = false;
template <class T>
inline constexpr bool isAbsentable<std::optional<T>> = true;
template <class T>
inline constexpr bool isAbsentable<BinaryExtension<T>> = true;
template <class T>
inline constexpr bool isAbsentable<std::shared_ptr<T>> = true;

// Wharfkit Struct.toJSON skips optional fields with no value, and JSON.stringify
// drops empty binary extensions; both amount to omitting an empty absentable.
template <class T>
bool jsonFieldPresent(const T&) {
    return true;
}
template <class T>
bool jsonFieldPresent(const std::optional<T>& v) {
    return v.has_value();
}
template <class T>
bool jsonFieldPresent(const BinaryExtension<T>& v) {
    return v.hasValue();
}
template <class T>
bool jsonFieldPresent(const std::shared_ptr<T>& v) {
    return v != nullptr;
}

template <DkStruct S>
Result<void> dkStructToABI(const S& v, ABIEncoder& e) {
    Result<void> status{};
    v.dkForEach([&](std::string_view, const auto& field) {
        if (!status) return;
        status = abi_traits<std::decay_t<decltype(field)>>::toABI(field, e);
    });
    return status;
}

template <DkStruct S>
Result<S> dkStructFromABI(ABIDecoder& d) {
    S rv{};
    Result<void> status{};
    rv.dkForEach([&](std::string_view, auto& field) {
        if (!status) return;
        auto value = abi_traits<std::decay_t<decltype(field)>>::fromABI(d);
        if (!value) {
            status = err(std::move(value.error()));
            return;
        }
        field = std::move(*value);
    });
    if (!status) return err(std::move(status.error()));
    return rv;
}

template <DkStruct S>
json dkStructToJSON(const S& v) {
    json obj = json::object();
    v.dkForEach([&](std::string_view name, const auto& field) {
        using F = std::decay_t<decltype(field)>;
        if (!jsonFieldPresent(field)) return;
        obj[std::string(name)] = abi_traits<F>::toJSON(field);
    });
    return obj;
}

template <DkStruct S>
Result<S> dkStructFromJSON(const json& j) {
    if (!j.is_object()) {
        return err(ErrorKind::Invalid, "Expected object");
    }
    S rv{};
    Result<void> status{};
    rv.dkForEach([&](std::string_view name, auto& field) {
        using F = std::decay_t<decltype(field)>;
        if (!status) return;
        const std::string key(name);
        if (!j.contains(key) || j.at(key).is_null()) {
            if constexpr (!isAbsentable<F>) {
                status = err(ErrorKind::Invalid,
                             "Unexpectedly encountered undefined for non-optional (" + key + ")");
            }
            return;  // absentable fields keep their default
        }
        auto value = abi_traits<F>::fromJSON(j.at(key));
        if (!value) {
            status = err(std::move(value.error()));
            return;
        }
        field = std::move(*value);
    });
    if (!status) return err(std::move(status.error()));
    return rv;
}

template <class S>
    requires DkStruct<S>
struct abi_traits<S> {
    static constexpr std::string_view abiName = S::abiName;
    static Result<void> toABI(const S& v, ABIEncoder& e) { return dkStructToABI(v, e); }
    static Result<S> fromABI(ABIDecoder& d) { return dkStructFromABI<S>(d); }
    static json toJSON(const S& v) { return dkStructToJSON(v); }
    static Result<S> fromJSON(const json& j) { return dkStructFromJSON<S>(j); }
    static S abiDefault() { return S{}; }
};

template <DkStruct S>
Result<S> structFrom(const json& value) {
    return abi_traits<S>::fromJSON(value);
}

template <DkStruct S>
bool structEquals(const S& a, const S& b) {
    ABIEncoder ea, eb;
    const auto ra = abi_traits<S>::toABI(a, ea);
    const auto rb = abi_traits<S>::toABI(b, eb);
    if (!ra || !rb) return false;
    return ea.getBytes() == eb.getBytes();
}

// ---- variant ---------------------------------------------------------------

namespace detail {

template <class Variant, class... Ts, size_t... Is>
Result<Variant> variantFromABIImpl(size_t idx, ABIDecoder& d, std::index_sequence<Is...>) {
    Result<Variant> rv = err(ErrorKind::Invalid, "Unknown variant idx: " + std::to_string(idx));
    (void)((idx == Is ? (rv =
                             [&]() -> Result<Variant> {
                                 DK_TRY(value, abi_traits<Ts>::fromABI(d));
                                 return Variant(std::move(value));
                             }(),
                         true)
                      : false) ||
           ...);
    return rv;
}

template <class Variant, class... Ts, size_t... Is>
Result<Variant> variantFromJSONImpl(std::string_view name, const json& value,
                                     std::index_sequence<Is...>) {
    static const std::array<std::string, sizeof...(Ts)> names = {abiTypeName<Ts>()...};
    Result<Variant> rv = err(ErrorKind::Invalid, "Unknown variant type: " + std::string(name));
    (void)((names[Is] == name ? (rv =
                                     [&]() -> Result<Variant> {
                                         DK_TRY(v, abi_traits<Ts>::fromJSON(value));
                                         return Variant(std::move(v));
                                     }(),
                                 true)
                              : false) ||
           ...);
    return rv;
}

}  // namespace detail

template <detail::FixedString AbiName, class... Ts>
struct abi_traits<Variant<AbiName, Ts...>> {
    using V = Variant<AbiName, Ts...>;
    static constexpr std::string_view abiName = AbiName;

    static Result<void> toABI(const V& v, ABIEncoder& e) {
        e.writeVaruint32(static_cast<uint32_t>(v.value.index()));
        return std::visit(
            [&](const auto& inner) -> Result<void> {
                return abi_traits<std::decay_t<decltype(inner)>>::toABI(inner, e);
            },
            v.value);
    }
    static Result<V> fromABI(ABIDecoder& d) {
        DK_TRY(idx, d.readByte());
        return detail::variantFromABIImpl<V, Ts...>(idx, d, std::index_sequence_for<Ts...>{});
    }
    static json toJSON(const V& v) { return v.toJSON(); }
    static Result<V> fromJSON(const json& j) { return V::from(j); }
    static V abiDefault() { return V{}; }
};

template <detail::FixedString AbiName, class... Ts>
Result<Variant<AbiName, Ts...>> Variant<AbiName, Ts...>::from(const json& object) {
    // JSON form is ["type_name", value]
    if (object.is_array() && object.size() == 2 && object[0].is_string()) {
        return detail::variantFromJSONImpl<Variant, Ts...>(
            object[0].get<std::string>(), object[1], std::index_sequence_for<Ts...>{});
    }
    return err(ErrorKind::Invalid, "Expected [type, value] for variant " + std::string(abiName));
}

// PermissionLevel also accepts its "actor@permission" string form in JSON.
template <>
struct abi_traits<PermissionLevel, void> {
    static constexpr std::string_view abiName = "permission_level";
    static Result<void> toABI(const PermissionLevel& v, ABIEncoder& e) {
        return dkStructToABI(v, e);
    }
    static Result<PermissionLevel> fromABI(ABIDecoder& d) {
        return dkStructFromABI<PermissionLevel>(d);
    }
    static json toJSON(const PermissionLevel& v) { return dkStructToJSON(v); }
    static Result<PermissionLevel> fromJSON(const json& j) {
        if (j.is_string()) {
            return PermissionLevel::from(std::string_view(j.get_ref<const std::string&>()));
        }
        return dkStructFromJSON<PermissionLevel>(j);
    }
    static PermissionLevel abiDefault() { return {}; }
};

// ---- type alias ------------------------------------------------------------

template <class A>
    requires DkTypeAlias<A>
struct abi_traits<A> {
    static constexpr std::string_view abiName = A::abiName;
    using U = typename A::DkAliased;
    static Result<void> toABI(const A& v, ABIEncoder& e) { return abi_traits<U>::toABI(v.value, e); }
    static Result<A> fromABI(ABIDecoder& d) {
        DK_TRY(value, abi_traits<U>::fromABI(d));
        return A(std::move(value));
    }
    static json toJSON(const A& v) { return abi_traits<U>::toJSON(v.value); }
    static Result<A> fromJSON(const json& j) {
        DK_TRY(value, abi_traits<U>::fromJSON(j));
        return A(std::move(value));
    }
    static A abiDefault() { return A{}; }
};

}  // namespace dwarfkit
