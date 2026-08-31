// Port of antelope src/serializer/serializable.ts plus src/chain/struct.ts,
// variant.ts and type-alias.ts. The TS decorators become DK_ macros over
// compile-time reflection (BLUEPRINT.md 5.5).
//
// DK_STRUCT/DK_FIELDS inject reflection into plain aggregates: designated
// initializers work (`Transfer{.from = ..., .to = ...}`) and a field named
// `from` never collides with a factory. JSON construction goes through
// dwarfkit::structFrom<T>(json) / abi_traits<T>::fromJSON.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <dwarfkit/antelope/serializer/decoder.hpp>
#include <dwarfkit/antelope/serializer/encoder.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

// Primary template; specialized for every ABI-serializable type. Provides:
//   static std::string_view abiName;
//   static Result<void> toABI(const T&, ABIEncoder&);
//   static Result<T> fromABI(ABIDecoder&);
//   static json toJSON(const T&);
//   static Result<T> fromJSON(const json&);
//   static T abiDefault();
template <class T, class Enable = void>
struct abi_traits;

namespace detail {

template <size_t N>
struct FixedString {
    char data[N]{};
    constexpr FixedString(const char (&str)[N]) {
        for (size_t i = 0; i < N; i++) data[i] = str[i];
    }
    constexpr operator std::string_view() const { return {data, N - 1}; }
};

// Visit the ABI base struct's fields first, if the type declares one.
template <class S, class F>
void visitBase(S& obj, F& f) {
    if constexpr (requires { typename std::remove_cvref_t<S>::DkBase; }) {
        using Base = typename std::remove_cvref_t<S>::DkBase;
        if constexpr (std::is_const_v<std::remove_reference_t<S>>) {
            static_cast<const Base&>(obj).dkForEach(f);
        } else {
            static_cast<Base&>(obj).dkForEach(f);
        }
    }
}

}  // namespace detail

// A field that only exists past a certain point of a binary stream ($ suffix).
template <class T>
class BinaryExtension {
public:
    std::optional<T> value;

    BinaryExtension() = default;
    BinaryExtension(T v) : value(std::move(v)) {}  // NOLINT(runtime/explicit)

    bool hasValue() const { return value.has_value(); }
    const T& operator*() const { return *value; }
    T& operator*() { return *value; }
    bool operator==(const BinaryExtension&) const = default;
};

// ---- ABI type-name derivation ----------------------------------------------
// The full ABI type string for a C++ type, container suffixes included
// (BLUEPRINT.md 5.5: Name -> "name", vector<T> -> "T[]", optional<T> -> "T?",
// BinaryExtension<T> -> "T$").

template <class T>
struct abi_type {
    static std::string name() { return std::string(abi_traits<T>::abiName); }
};
template <class T>
struct abi_type<std::vector<T>> {
    static std::string name() { return abi_type<T>::name() + "[]"; }
};
template <class T>
struct abi_type<std::optional<T>> {
    static std::string name() { return abi_type<T>::name() + "?"; }
};
template <class T>
struct abi_type<BinaryExtension<T>> {
    static std::string name() { return abi_type<T>::name() + "$"; }
};
template <class T>
struct abi_type<std::shared_ptr<T>> {
    static std::string name() { return abi_type<T>::name() + "?"; }
};

template <class T>
std::string abiTypeName() {
    return abi_type<T>::name();
}

// A struct type carries reflection injected by DK_STRUCT + DK_FIELDS.
template <class T>
concept DkStruct = requires(T& t, const T& ct) {
    { T::abiName } -> std::convertible_to<std::string_view>;
    t.dkForEach([](std::string_view, auto&) {});
    ct.dkForEach([](std::string_view, const auto&) {});
};

template <class T>
concept DkVariant = requires {
    { T::abiName } -> std::convertible_to<std::string_view>;
    typename T::DkVariantTag;
};

template <class T>
concept DkTypeAlias = requires {
    { T::abiName } -> std::convertible_to<std::string_view>;
    typename T::DkAliasTag;
};

// Struct.from(value) equivalent; declared here, defined in traits.hpp.
template <DkStruct S>
Result<S> structFrom(const json& value);

// Struct equality; like upstream this compares the ABI encoded bytes.
template <DkStruct S>
bool structEquals(const S& a, const S& b);

template <DkStruct S>
bool operator==(const S& a, const S& b) {
    return structEquals(a, b);
}

// Variant over a fixed set of alternatives; JSON form is ["type_name", value].
template <detail::FixedString AbiName, class... Ts>
class Variant {
public:
    using DkVariantTag = void;
    static constexpr std::string_view abiName = AbiName;

    std::variant<Ts...> value;

    Variant() = default;
    template <class T>
        requires(std::is_constructible_v<std::variant<Ts...>, T &&> &&
                 !std::is_same_v<std::decay_t<T>, Variant>)
    Variant(T&& v) : value(std::forward<T>(v)) {}  // NOLINT(runtime/explicit)

    static Result<Variant> from(const json& object);
    static Variant from(const Variant& object) { return object; }

    size_t variantIdx() const { return value.index(); }
    std::string variantName() const {
        static const std::array<std::string, sizeof...(Ts)> names = {abiTypeName<Ts>()...};
        return names[value.index()];
    }

    template <class T>
    const T* get_if() const {
        return std::get_if<T>(&value);
    }

    template <class T>
    T* get_if() {
        return std::get_if<T>(&value);
    }

    bool equals(const Variant& other) const { return value == other.value; }
    bool operator==(const Variant&) const = default;

    json toJSON() const {
        return std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                return json::array({std::string(variantName()), abi_traits<T>::toJSON(v)});
            },
            value);
    }
};

// Alias type: a named typedef in synthesized ABIs (TS @TypeAlias).
#define DK_TYPE_ALIAS(TypeName, abi_name, Underlying)                 \
    struct TypeName {                                                 \
        using DkAliasTag = void;                                      \
        using DkAliased = Underlying;                                 \
        static constexpr std::string_view abiName = abi_name;         \
        Underlying value{};                                           \
        TypeName() = default;                                         \
        TypeName(Underlying v) : value(std::move(v)) {} /* NOLINT */  \
        operator const Underlying&() const { return value; }          \
        bool operator==(const TypeName&) const = default;             \
    };

#define DK_VARIANT(TypeName, abi_name, ...) \
    using TypeName = ::dwarfkit::Variant<abi_name, __VA_ARGS__>;

}  // namespace dwarfkit

// ---------------------------------------------------------------------------
// DK_STRUCT / DK_FIELDS machinery (32 fields max)
// ---------------------------------------------------------------------------

// statement-map: DK_SFE(m, a, b) -> m(a) m(b)
#define DK_SFE_1(m, a) m(a)
#define DK_SFE_2(m, a, ...) m(a) DK_SFE_1(m, __VA_ARGS__)
#define DK_SFE_3(m, a, ...) m(a) DK_SFE_2(m, __VA_ARGS__)
#define DK_SFE_4(m, a, ...) m(a) DK_SFE_3(m, __VA_ARGS__)
#define DK_SFE_5(m, a, ...) m(a) DK_SFE_4(m, __VA_ARGS__)
#define DK_SFE_6(m, a, ...) m(a) DK_SFE_5(m, __VA_ARGS__)
#define DK_SFE_7(m, a, ...) m(a) DK_SFE_6(m, __VA_ARGS__)
#define DK_SFE_8(m, a, ...) m(a) DK_SFE_7(m, __VA_ARGS__)
#define DK_SFE_9(m, a, ...) m(a) DK_SFE_8(m, __VA_ARGS__)
#define DK_SFE_10(m, a, ...) m(a) DK_SFE_9(m, __VA_ARGS__)
#define DK_SFE_11(m, a, ...) m(a) DK_SFE_10(m, __VA_ARGS__)
#define DK_SFE_12(m, a, ...) m(a) DK_SFE_11(m, __VA_ARGS__)
#define DK_SFE_13(m, a, ...) m(a) DK_SFE_12(m, __VA_ARGS__)
#define DK_SFE_14(m, a, ...) m(a) DK_SFE_13(m, __VA_ARGS__)
#define DK_SFE_15(m, a, ...) m(a) DK_SFE_14(m, __VA_ARGS__)
#define DK_SFE_16(m, a, ...) m(a) DK_SFE_15(m, __VA_ARGS__)
#define DK_SFE_17(m, a, ...) m(a) DK_SFE_16(m, __VA_ARGS__)
#define DK_SFE_18(m, a, ...) m(a) DK_SFE_17(m, __VA_ARGS__)
#define DK_SFE_19(m, a, ...) m(a) DK_SFE_18(m, __VA_ARGS__)
#define DK_SFE_20(m, a, ...) m(a) DK_SFE_19(m, __VA_ARGS__)
#define DK_SFE_21(m, a, ...) m(a) DK_SFE_20(m, __VA_ARGS__)
#define DK_SFE_22(m, a, ...) m(a) DK_SFE_21(m, __VA_ARGS__)
#define DK_SFE_23(m, a, ...) m(a) DK_SFE_22(m, __VA_ARGS__)
#define DK_SFE_24(m, a, ...) m(a) DK_SFE_23(m, __VA_ARGS__)
#define DK_SFE_25(m, a, ...) m(a) DK_SFE_24(m, __VA_ARGS__)
#define DK_SFE_26(m, a, ...) m(a) DK_SFE_25(m, __VA_ARGS__)
#define DK_SFE_27(m, a, ...) m(a) DK_SFE_26(m, __VA_ARGS__)
#define DK_SFE_28(m, a, ...) m(a) DK_SFE_27(m, __VA_ARGS__)
#define DK_SFE_29(m, a, ...) m(a) DK_SFE_28(m, __VA_ARGS__)
#define DK_SFE_30(m, a, ...) m(a) DK_SFE_29(m, __VA_ARGS__)
#define DK_SFE_31(m, a, ...) m(a) DK_SFE_30(m, __VA_ARGS__)
#define DK_SFE_32(m, a, ...) m(a) DK_SFE_31(m, __VA_ARGS__)

#define DK_NARG(...)                                                                              \
    DK_NARG_IMPL(__VA_ARGS__, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, \
                 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define DK_NARG_IMPL(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17,   \
                     a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, N, \
                     ...)                                                                          \
    N

#define DK_SFE_DISPATCH(N) DK_CAT(DK_SFE_, N)
#define DK_SFE(m, ...) DK_SFE_DISPATCH(DK_NARG(__VA_ARGS__))(m, __VA_ARGS__)

#define DK_VISIT_FIELD(x) dkFn(std::string_view(#x), this->x);

// Declare the ABI type name of a struct.
#define DK_STRUCT(abi_name_str) static constexpr std::string_view abiName = abi_name_str;

// Declare the ABI type name of a struct extending another ABI struct. Base must
// be an actual C++ base class of this struct; its fields are visited first.
#define DK_STRUCT_BASE(abi_name_str, Base)                    \
    static constexpr std::string_view abiName = abi_name_str; \
    using DkBase = Base;

// A struct with no serializable fields of its own (some ABIs declare empty
// action argument structs).
#define DK_NO_FIELDS                                   \
    template <class DkFn>                              \
    void dkForEach(DkFn&& dkFn) {                      \
        ::dwarfkit::detail::visitBase(*this, dkFn);    \
    }                                                  \
    template <class DkFn>                              \
    void dkForEach(DkFn&& dkFn) const {                \
        ::dwarfkit::detail::visitBase(*this, dkFn);    \
    }

// Declare the (own, non-base) serializable fields, in ABI order.
#define DK_FIELDS(...)                                 \
    template <class DkFn>                              \
    void dkForEach(DkFn&& dkFn) {                      \
        ::dwarfkit::detail::visitBase(*this, dkFn);    \
        DK_SFE(DK_VISIT_FIELD, __VA_ARGS__)            \
    }                                                  \
    template <class DkFn>                              \
    void dkForEach(DkFn&& dkFn) const {                \
        ::dwarfkit::detail::visitBase(*this, dkFn);    \
        DK_SFE(DK_VISIT_FIELD, __VA_ARGS__)            \
    }
