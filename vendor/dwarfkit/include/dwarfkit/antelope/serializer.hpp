// Port of antelope src/serializer/index.ts (the Serializer namespace) plus the
// abiEncode/abiDecode entry points. Static (typed) overloads live here; the
// dynamic (json + type name + ABI) overloads are declared here and defined in
// serializer.cpp once the ABI model is available.
#pragma once

#include <set>
#include <span>
#include <string>

#include <dwarfkit/antelope/chain/abi.hpp>
#include <dwarfkit/antelope/serializer/traits.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

// ---- ABI synthesis (port of serializable.ts synthesizeABI) -----------------

template <class T, class Enable = void>
struct abi_collect {
    static void collect(ABI&, std::set<std::string>&) {}  // builtins: nothing to add
};

template <class T>
struct abi_collect<std::vector<T>> {
    static void collect(ABI& abi, std::set<std::string>& seen) {
        abi_collect<T>::collect(abi, seen);
    }
};
template <class T>
struct abi_collect<std::optional<T>> {
    static void collect(ABI& abi, std::set<std::string>& seen) {
        abi_collect<T>::collect(abi, seen);
    }
};
template <class T>
struct abi_collect<BinaryExtension<T>> {
    static void collect(ABI& abi, std::set<std::string>& seen) {
        abi_collect<T>::collect(abi, seen);
    }
};
template <class T>
struct abi_collect<std::shared_ptr<T>> {
    static void collect(ABI& abi, std::set<std::string>& seen) {
        abi_collect<T>::collect(abi, seen);
    }
};

template <class S>
    requires DkStruct<S>
struct abi_collect<S> {
    static void collect(ABI& abi, std::set<std::string>& seen) {
        const std::string name(S::abiName);
        if (seen.contains(name)) return;
        seen.insert(name);
        // gather all (name, type) pairs; base fields come first
        std::vector<ABI::Field> fields;
        S instance{};
        instance.dkForEach([&](std::string_view fieldName, const auto& field) {
            using F = std::decay_t<decltype(field)>;
            abi_collect<F>::collect(abi, seen);
            fields.push_back({std::string(fieldName), abiTypeName<F>()});
        });
        std::string base;
        if constexpr (requires { typename S::DkBase; }) {
            using B = typename S::DkBase;
            abi_collect<B>::collect(abi, seen);
            base = std::string(B::abiName);
            // drop the base's fields from the own-field list
            size_t baseCount = 0;
            B{}.dkForEach([&](std::string_view, const auto&) { baseCount++; });
            fields.erase(fields.begin(),
                         fields.begin() + static_cast<ptrdiff_t>(baseCount));
        }
        abi.structs.push_back({name, std::move(base), std::move(fields)});
    }
};

template <detail::FixedString AbiName, class... Ts>
struct abi_collect<Variant<AbiName, Ts...>> {
    static void collect(ABI& abi, std::set<std::string>& seen) {
        const std::string name{std::string_view(AbiName)};
        if (seen.contains(name)) return;
        seen.insert(name);
        (abi_collect<Ts>::collect(abi, seen), ...);
        abi.variants.push_back({name, {abiTypeName<Ts>()...}});
    }
};

template <class A>
    requires DkTypeAlias<A>
struct abi_collect<A> {
    static void collect(ABI& abi, std::set<std::string>& seen) {
        const std::string name(A::abiName);
        if (seen.contains(name)) return;
        seen.insert(name);
        abi_collect<typename A::DkAliased>::collect(abi, seen);
        abi.types.push_back({name, abiTypeName<typename A::DkAliased>()});
    }
};

namespace Serializer {

// Static encode: type known at compile time.
template <class T>
Result<Bytes> encode(const T& object) {
    ABIEncoder encoder;
    DK_CHECK(abi_traits<T>::toABI(object, encoder));
    return encoder.getBytes();
}

// Decode behavior flags (upstream args.strictExtensions).
struct DecodeOptions {
    // Absent binary-extension fields synthesize their type's default value
    // instead of decoding as null/absent.
    bool strictExtensions = false;
};

// Static decode: type known at compile time.
// The options overload enables strictExtensions for typed BinaryExtension
// fields.
template <class T>
Result<T> decode(std::span<const uint8_t> data) {
    ABIDecoder decoder(data);
    return abi_traits<T>::fromABI(decoder);
}

template <class T>
Result<T> decode(std::span<const uint8_t> data, const DecodeOptions& options) {
    ABIDecoder decoder(data);
    decoder.strictExtensions = options.strictExtensions;
    return abi_traits<T>::fromABI(decoder);
}

template <class T>
Result<T> decode(const Bytes& data) {
    return decode<T>(std::span<const uint8_t>(data.array));
}

// Dynamic encode/decode against an ABI (defined in serializer.cpp).
Result<Bytes> encode(const json& object, std::string_view type, const ABI& abi);
Result<json> decode(std::span<const uint8_t> data, std::string_view type, const ABI& abi);
Result<json> decode(std::span<const uint8_t> data, std::string_view type, const ABI& abi,
                    const DecodeOptions& options);
Result<json> decode(const Bytes& data, std::string_view type, const ABI& abi);
// Decode (validate and normalize) an untyped json object against an ABI.
Result<json> decodeObject(const json& object, std::string_view type, const ABI& abi);
Result<json> decodeObject(const json& object, std::string_view type, const ABI& abi,
                          const DecodeOptions& options);

// Create an Antelope/EOSIO ABI definition for given core type.
template <class T>
ABI synthesize() {
    ABI abi;
    std::set<std::string> seen;
    abi_collect<T>::collect(abi, seen);
    return abi;
}

// JSON string of a core object (JSON.stringify parity: compact, insertion order).
template <class T>
std::string stringify(const T& object) {
    return abi_traits<T>::toJSON(object).dump();
}

// Vanilla json representation of a core object (Serializer.objectify).
template <class T>
json objectify(const T& object) {
    return abi_traits<T>::toJSON(object);
}

}  // namespace Serializer

}  // namespace dwarfkit
