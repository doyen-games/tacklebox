#include <dwarfkit/antelope/serializer/builtins.hpp>

namespace dwarfkit {

namespace {

template <class T>
BuiltinType makeBuiltin() {
    return BuiltinType{
        [](const json& value, ABIEncoder& encoder) -> Result<void> {
            DK_TRY(typed, abi_traits<T>::fromJSON(value));
            return abi_traits<T>::toABI(typed, encoder);
        },
        [](ABIDecoder& decoder) -> Result<json> {
            DK_TRY(typed, abi_traits<T>::fromABI(decoder));
            return abi_traits<T>::toJSON(typed);
        },
        [](const json& value) -> Result<json> {
            DK_TRY(typed, abi_traits<T>::fromJSON(value));
            return abi_traits<T>::toJSON(typed);
        },
        []() -> json { return abi_traits<T>::toJSON(abi_traits<T>::abiDefault()); },
    };
}

std::map<std::string, BuiltinType, std::less<>> buildRegistry() {
    std::map<std::string, BuiltinType, std::less<>> rv;
    const auto add = [&rv]<class T>(std::in_place_type_t<T>) {
        rv.emplace(std::string(abi_traits<T>::abiName), makeBuiltin<T>());
    };
    add(std::in_place_type<bool>);
    add(std::in_place_type<std::string>);
    add(std::in_place_type<int8_t>);
    add(std::in_place_type<int16_t>);
    add(std::in_place_type<int32_t>);
    add(std::in_place_type<int64_t>);
    add(std::in_place_type<Int128>);
    add(std::in_place_type<uint8_t>);
    add(std::in_place_type<uint16_t>);
    add(std::in_place_type<uint32_t>);
    add(std::in_place_type<uint64_t>);
    add(std::in_place_type<UInt128>);
    add(std::in_place_type<VarInt>);
    add(std::in_place_type<VarUInt>);
    add(std::in_place_type<float>);
    add(std::in_place_type<double>);
    add(std::in_place_type<Float128>);
    add(std::in_place_type<Name>);
    add(std::in_place_type<Bytes>);
    add(std::in_place_type<Checksum160>);
    add(std::in_place_type<Checksum256>);
    add(std::in_place_type<Checksum512>);
    add(std::in_place_type<PublicKey>);
    add(std::in_place_type<Signature>);
    add(std::in_place_type<Asset>);
    add(std::in_place_type<Asset::Symbol>);
    add(std::in_place_type<Asset::SymbolCode>);
    add(std::in_place_type<ExtendedAsset>);
    add(std::in_place_type<TimePoint>);
    add(std::in_place_type<TimePointSec>);
    add(std::in_place_type<BlockTimestamp>);
    return rv;
}

}  // namespace

const std::map<std::string, BuiltinType, std::less<>>& builtinTypes() {
    static const auto registry = buildRegistry();
    return registry;
}

}  // namespace dwarfkit
