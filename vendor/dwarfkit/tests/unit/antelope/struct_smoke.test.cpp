// Smoke test for the static serializer path (DK_STRUCT/DK_FIELDS reflection)
// before the dynamic ABI machinery lands.
#include <doctest/doctest.h>

#include <dwarfkit/antelope/serializer.hpp>

using namespace dwarfkit;

namespace {

struct Transfer {
    DK_STRUCT("transfer")
    Name from;
    Name to;
    Asset quantity;
    std::string memo;
    DK_FIELDS(from, to, quantity, memo)
};

struct Nested {
    DK_STRUCT("nested")
    Transfer inner;
    std::vector<uint32_t> nums;
    std::optional<std::string> note;
    DK_FIELDS(inner, nums, note)
};

struct Headerish {
    DK_STRUCT("headerish")
    uint16_t ref_num = 0;
    uint32_t prefix = 0;
    DK_FIELDS(ref_num, prefix)
};

struct Derived : Headerish {
    DK_STRUCT_BASE("derived", Headerish)
    std::string tail;
    DK_FIELDS(tail)
};

DK_VARIANT(MyVariant, "my_variant", std::string, int32_t)

struct ExtStruct {
    DK_STRUCT("ext_struct")
    uint8_t a = 0;
    BinaryExtension<uint16_t> b;
    DK_FIELDS(a, b)
};

}  // namespace

TEST_SUITE("struct-smoke") {
    TEST_CASE("encode and decode a struct") {
        const Transfer t{.from = "foo"_n,
                         .to = "bar"_n,
                         .quantity = "1.0000 EOS"_asset,
                         .memo = "hello"};
        const auto encoded = Serializer::encode(t).value();
        CHECK(encoded.hexString() ==
              "000000000000285d000000000000ae39102700000000000004454f5300000000"
              "0568656c6c6f");
        const auto decoded = Serializer::decode<Transfer>(encoded).value();
        CHECK(decoded.from.toString() == "foo");
        CHECK(decoded.to.toString() == "bar");
        CHECK(decoded.quantity.toString() == "1.0000 EOS");
        CHECK(decoded.memo == "hello");
        CHECK(structEquals(t, decoded));
    }

    TEST_CASE("toJSON and stringify") {
        const Transfer t{.from = "foo"_n,
                         .to = "bar"_n,
                         .quantity = "1.0000 EOS"_asset,
                         .memo = "hello"};
        CHECK(Serializer::stringify(t) ==
              R"({"from":"foo","to":"bar","quantity":"1.0000 EOS","memo":"hello"})");
    }

    TEST_CASE("from json") {
        const json j = {{"from", "foo"},
                        {"to", "bar"},
                        {"quantity", "1.0000 EOS"},
                        {"memo", "hello"}};
        const auto t = structFrom<Transfer>(j).value();
        CHECK(t.from.toString() == "foo");
        CHECK(t.quantity.toString() == "1.0000 EOS");
        CHECK_FALSE(structFrom<Transfer>(json{{"from", "foo"}}).has_value());
    }

    TEST_CASE("nested, array, optional") {
        const Nested n{
            .inner = {.from = "a"_n, .to = "b"_n, .quantity = "2.0000 EOS"_asset, .memo = "x"},
            .nums = {1, 2, 3},
            .note = std::nullopt};
        const auto encoded = Serializer::encode(n).value();
        const auto decoded = Serializer::decode<Nested>(encoded).value();
        CHECK(decoded.nums == std::vector<uint32_t>{1, 2, 3});
        CHECK(decoded.inner.memo == "x");
        CHECK_FALSE(decoded.note.has_value());
        // optional field with no value is omitted from JSON
        CHECK(Serializer::objectify(n).contains("note") == false);
    }

    TEST_CASE("struct inheritance") {
        Derived d;
        d.ref_num = 7;
        d.prefix = 9;
        d.tail = "z";
        const auto encoded = Serializer::encode(d).value();
        CHECK(encoded.hexString() == "070009000000017a");
        const auto decoded = Serializer::decode<Derived>(encoded).value();
        CHECK(decoded.ref_num == 7);
        CHECK(decoded.tail == "z");
        CHECK(Serializer::stringify(d) == R"({"ref_num":7,"prefix":9,"tail":"z"})");
    }

    TEST_CASE("variant") {
        const MyVariant v{std::string("hello")};
        CHECK(v.variantName() == "string");
        CHECK(Serializer::stringify(v) == R"(["string","hello"])");
        const auto encoded = Serializer::encode(v).value();
        CHECK(encoded.hexString() == "000568656c6c6f");
        const auto decoded = Serializer::decode<MyVariant>(encoded).value();
        CHECK(decoded == v);
        const MyVariant i{int32_t(42)};
        CHECK(Serializer::encode(i).value().hexString() == "012a000000");
        CHECK(MyVariant::from(json::array({"int32", 42})).value() == i);
        CHECK_FALSE(MyVariant::from(json::array({"nope", 1})).has_value());
    }

    TEST_CASE("abi type name derivation") {
        CHECK(abiTypeName<Name>() == "name");
        CHECK(abiTypeName<std::vector<Name>>() == "name[]");
        CHECK(abiTypeName<std::optional<Asset>>() == "asset?");
        CHECK(abiTypeName<BinaryExtension<uint32_t>>() == "uint32$");
        CHECK(abiTypeName<Transfer>() == "transfer");
        CHECK(abiTypeName<MyVariant>() == "my_variant");
    }

    TEST_CASE("binary extension") {
        ExtStruct e;
        e.a = 1;
        CHECK(Serializer::encode(e).value().hexString() == "01");
        e.b = uint16_t(7);
        CHECK(Serializer::encode(e).value().hexString() == "010700");
        const auto decodedShort =
            Serializer::decode<ExtStruct>(Bytes::from("01").value()).value();
        CHECK_FALSE(decodedShort.b.hasValue());
        const auto decodedFull =
            Serializer::decode<ExtStruct>(Bytes::from("010700").value()).value();
        CHECK(*decodedFull.b == 7);
        // strictExtensions synthesizes the default into the typed field
        const auto decodedStrict =
            Serializer::decode<ExtStruct>(std::span<const uint8_t>(
                                              Bytes::from("01").value().array),
                                          {.strictExtensions = true})
                .value();
        REQUIRE(decodedStrict.b.hasValue());
        CHECK(*decodedStrict.b == 0);
    }
}
