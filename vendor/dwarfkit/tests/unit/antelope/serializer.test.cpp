// Port of antelope test/serializer.ts.
//
// Not ported (see DIVERGENCES.md): 'struct object'/'untyped struct'/'custom
// alias'/'coder metadata' (TS runtime class machinery), 'argument mutation'
// (json is passed by value). The 'binary extensions' strictExtensions cases
// port through the dynamic path (custom-class coupling cases are TS-runtime).
#include <doctest/doctest.h>

#include <fstream>
#include <sstream>

#include <dwarfkit/antelope/chain/transaction.hpp>
#include <dwarfkit/antelope/serializer.hpp>

using namespace dwarfkit;

namespace {

json parse(std::string_view text) { return json::parse(text); }

json loadFixture(const std::string& name) {
    std::ifstream file(std::string(DK_FIXTURE_DIR "/antelope/") + name);
    REQUIRE(file.good());
    std::stringstream buffer;
    buffer << file.rdbuf();
    return json::parse(buffer.str());
}

// nodejs deepEqual ignores object key order; ordered_json does not
bool deepEqual(const json& a, const json& b) {
    return nlohmann::json(a) == nlohmann::json(b);
}

struct SerTransfer {
    DK_STRUCT("transfer")
    Name from;
    Name to;
    Asset quantity;
    std::string memo;
    DK_FIELDS(from, to, quantity, memo)
};

DK_TYPE_ALIAS(MyTransaction, "my_transaction", Transaction)
DK_VARIANT(MyTransactionVariant, "my_variant", std::string, MyTransaction)

DK_TYPE_ALIAS(DoYouEven, "do_you_even", Int128)
DK_VARIANT(SeveralThings, "several_things", std::vector<Transaction>, std::optional<bool>, DoYouEven)

struct Complex {
    DK_STRUCT("complex")
    SeveralThings things;
    std::shared_ptr<Complex> self;
    DK_FIELDS(things, self)
};

struct AnyStruct {
    DK_STRUCT("my_struct")
    json foo;
    std::vector<json> bar;
    std::optional<json> baz;
    Name account;
    DK_FIELDS(foo, bar, baz, account)
};

struct OptStruct {
    DK_STRUCT("test")
    std::string a;
    std::optional<std::string> b;
    std::optional<std::string> c;
    std::optional<std::vector<std::string>> d;
    DK_FIELDS(a, b, c, d)
};

struct U16Struct {
    DK_STRUCT("my_struct")
    uint16_t foo = 0;
    DK_FIELDS(foo)
};

}  // namespace

TEST_SUITE("serializer") {
    TEST_CASE("array") {
        const std::string data = "0303666f6f036261720362617a";
        const json array = json::array({"foo", "bar", "baz"});
        const ABI noAbi;
        CHECK(Serializer::encode(array, "string[]", noAbi).value().hexString() == data);
        CHECK(Serializer::decode(Bytes::from(data).value(), "string[]", noAbi).value() == array);
        CHECK_FALSE(Serializer::encode(json("banana"), "string[]", noAbi).has_value());
        CHECK_FALSE(Serializer::decodeObject(json("banana"), "string[]", noAbi).has_value());
        // typed equivalent of the CustomType cases
        const auto typedArray = std::vector<std::string>{"foo", "bar", "baz"};
        CHECK(Serializer::encode(typedArray).value().hexString() == data);
    }

    TEST_CASE("name") {
        const std::string data = "000000005c73285d";
        const ABI noAbi;
        CHECK(Serializer::encode(Name::from("foobar")).value().hexString() == data);
        CHECK(Serializer::decode<Name>(Bytes::from(data).value()).value() == Name::from("foobar"));
        CHECK(Serializer::decode(Bytes::from(data).value(), "name", noAbi).value() == "foobar");
        CHECK(Name::from(6712742083569909760ull) == Name::from("foobar"));
        CHECK(Serializer::stringify(Name::from("foobar")) == "\"foobar\"");
        CHECK(Name::from("foobar").value == 6712742083569909760ull);
        CHECK(Serializer::stringify(Name::from(uint64_t(0))) == "\"\"");
    }

    TEST_CASE("asset") {
        const std::string data = "393000000000000004464f4f00000000";
        const auto object = Asset::from("1.2345 FOO").value();
        const ABI noAbi;
        CHECK(Serializer::encode(object).value().hexString() == data);
        CHECK(Serializer::decode<Asset>(Bytes::from(data).value()).value().toString() ==
              "1.2345 FOO");
        CHECK(Serializer::decode(Bytes::from(data).value(), "asset", noAbi).value() ==
              "1.2345 FOO");
        CHECK(Serializer::stringify(object) == "\"1.2345 FOO\"");

        const std::string data2 = "00000000000000000000000000000000";
        const auto object2 = Asset::from("0 ").value();
        CHECK(Serializer::encode(object2).value().hexString() == data2);
        CHECK(Serializer::decode(Bytes::from(data2).value(), "asset", noAbi).value() == "0 ");
        CHECK(Serializer::stringify(object2) == "\"0 \"");
    }

    TEST_CASE("asset symbol") {
        const std::string data = "04464f4f00000000";
        const auto object = Asset::Symbol::from("4,FOO").value();
        const ABI noAbi;
        CHECK(Serializer::encode(object).value().hexString() == data);
        CHECK(Serializer::decode(Bytes::from(data).value(), "symbol", noAbi).value() == "4,FOO");
        CHECK(Serializer::stringify(object) == "\"4,FOO\"");
    }

    TEST_CASE("struct") {
        const auto abi = ABI::from(parse(R"({
            "structs": [
                {"base": "", "name": "foo", "fields": [
                    {"name": "one", "type": "string"},
                    {"name": "two", "type": "int8"}
                ]},
                {"base": "foo", "name": "bar", "fields": [
                    {"name": "three", "type": "name?"},
                    {"name": "four", "type": "string[]?"}
                ]}
            ]})")).value();
        const json object = parse(R"({"one":"one","two":2,"three":"two","four":["f","o","u","r"]})");
        const auto enc = Serializer::encode(object, "bar", abi).value();
        CHECK(enc.hexString() == "036f6e65020100000000000028cf01040166016f01750172");
        const auto dec = Serializer::decode(enc, "bar", abi).value();
        CHECK(dec.dump() == R"({"one":"one","two":2,"three":"two","four":["f","o","u","r"]})");
    }

    TEST_CASE("struct decorators") {
        auto transfer = structFrom<SerTransfer>(parse(
                            R"({"from":"alice","to":"bob","quantity":"3.5 GMZ","memo":"for you"})"))
                            .value();
        REQUIRE(transfer.quantity.setValue(transfer.quantity.value() + 38.5).has_value());
        CHECK(Serializer::encode(transfer).value().hexString() ==
              "0000000000855c340000000000000e3da40100000000000001474d5a0000000007666f7220796f75");
        CHECK(Serializer::stringify(transfer) ==
              R"({"from":"alice","to":"bob","quantity":"42.0 GMZ","memo":"for you"})");
    }

    TEST_CASE("string") {
        const std::string data = "0b68656c6c6f20776f726c64";
        CHECK(Serializer::encode(std::string("hello world")).value().hexString() == data);
        CHECK(Serializer::decode<std::string>(Bytes::from(data).value()).value() == "hello world");
    }

    TEST_CASE("bool") {
        CHECK(Serializer::encode(true).value().hexString() == "01");
        CHECK(Serializer::decode<bool>(Bytes::from("01").value()).value() == true);
    }

    TEST_CASE("public key") {
        const std::string data =
            "000223e0ae8aacb41b06dc74af1a56b2eb69133f07f7f75bd1d5e53316bff195edf4";
        const std::string keyString = "PUB_K1_5AHoNnWetuDhKWSDx3WUf8W7Dg5xjHCMc4yHmmSiaJCFvvAgnB";
        const auto object = PublicKey::from(keyString).value();
        const ABI noAbi;
        CHECK(Serializer::encode(object).value().hexString() == data);
        CHECK(Serializer::decode<PublicKey>(Bytes::from(data).value()).value().toString() ==
              keyString);
        CHECK(Serializer::decode(Bytes::from(data).value(), "public_key", noAbi).value() ==
              keyString);
    }

    TEST_CASE("public key (wa)") {
        const std::string data =
            "020220b9dab512e892392a44a9f41f9433c9fbd80db864e9df5889c2407db3acbb9f010d6b656f73642e"
            "696e76616c6964";
        const std::string keyString =
            "PUB_WA_WdCPfafVNxVMiW5ybdNs83oWjenQXvSt1F49fg9mv7qrCiRwHj5b38U3ponCFWxQTkDsMC";
        const auto object = PublicKey::from(keyString).value();
        CHECK(Serializer::encode(object).value().hexString() == data);
        CHECK(Serializer::decode<PublicKey>(Bytes::from(data).value()).value().toString() ==
              keyString);
    }

    TEST_CASE("signature") {
        const std::string data =
            "00205150a67288c3b393fdba9061b05019c54b12bdac295fc83bebad7cd63c7bb67d5cb8cc220564da00"
            "6240a58419f64d06a5c6e1fc62889816a6c3dfdd231ed389";
        const std::string sigString =
            "SIG_K1_KfPLgpw35iX8nfDzhbcmSBCr7nEGNEYXgmmempQspDJYBCKuAEs5rm3s4ZuLJY428Ca8ZhvR2Dkwu"
            "118y3NAoMDxhicRj9";
        const auto object = Signature::from(sigString).value();
        CHECK(Serializer::encode(object).value().hexString() == data);
        CHECK(Serializer::decode<Signature>(Bytes::from(data).value()).value().toString() ==
              sigString);
    }

    TEST_CASE("signature (wa)") {
        const std::string sig =
            "SIG_WA_2AAAuLJS3pLPgkQQPqLsehL6VeRBaAZS7NYM91UYRUrSAEfUvzKN7DCSwhjsDqe74cZNWKUU"
            "GAHGG8ddSA7cvUxChbfKxLSrDCpwe6MVUqz4PDdyCt5tXhEJmKekxG1o1ucY3LVj8Vi9rRbzAkKPCzW"
            "qC8cPcUtpLHNG8qUKkQrN4Xuwa9W8rsBiUKwZv1ToLyVhLrJe42pvHYBXicp4E8qec5E4m6SX11KuXE"
            "RFcV48Mhiie2NyaxdtNtNzQ5XZ5hjBkxRujqejpF4SNHvdAGKRBbvhkiPLA25FD3xoCbrN26z72";
        const std::string data =
            "0220d9132bbdb219e4e2d99af9c507e3597f86b615814f36672d501034861792bbcf21a46d1a2eb12bace4"
            "a29100b942f987494f3aefc8efb2d5af4d4d8de3e0871525aa14905af60ca17a1bb80e0cf9c3b46908a0f1"
            "4f72567a2f140c3a3bd2ef074c010000006d737b226f726967696e223a2268747470733a2f2f6b656f7364"
            "2e696e76616c6964222c2274797065223a22776562617574686e2e676574222c226368616c6c656e676522"
            "3a226f69567235794848304a4336453962446675347142735a6a527a70416c5131505a50436e5974766850"
            "556b3d227d";
        const auto object = Signature::from(sig).value();
        CHECK(Serializer::encode(object).value().hexString() == data);
        CHECK(Serializer::decode<Signature>(Bytes::from(data).value()).value().toString() == sig);
    }

    TEST_CASE("time point") {
        const std::string data = "f8b88a3cd5620400";
        const auto object = TimePoint::from(int64_t(1234567890123000));
        const ABI noAbi;
        CHECK(Serializer::encode(object).value().hexString() == data);
        CHECK(Serializer::decode(Bytes::from(data).value(), "time_point", noAbi).value() ==
              "2009-02-13T23:31:30.123");
        CHECK(Serializer::stringify(object) == "\"2009-02-13T23:31:30.123\"");
    }

    TEST_CASE("time point sec") {
        const std::string data = "d2029649";
        const auto object = TimePointSec::from(uint32_t(1234567890));
        const ABI noAbi;
        CHECK(Serializer::encode(object).value().hexString() == data);
        CHECK(Serializer::decode(Bytes::from(data).value(), "time_point_sec", noAbi).value() ==
              "2009-02-13T23:31:30");
        CHECK(Serializer::stringify(object) == "\"2009-02-13T23:31:30\"");
    }

    TEST_CASE("optionals") {
        const ABI noAbi;
        CHECK(Serializer::decode(Bytes::from("00").value(), "public_key?", noAbi).value() ==
              json(nullptr));
        CHECK(Serializer::decode(Bytes::from("0101").value(), "bool?", noAbi).value() == true);
        CHECK(Serializer::encode(json(nullptr), "signature?", noAbi).value().hexString() == "00");
        CHECK_FALSE(Serializer::decodeObject(json(nullptr), "bool", noAbi).has_value());
        CHECK_FALSE(Serializer::encode(json(nullptr), "bool", noAbi).has_value());
    }

    TEST_CASE("api") {
        const ABI noAbi;
        CHECK_FALSE(Serializer::decodeObject(json("foo"), "santa", noAbi).has_value());
    }

    TEST_CASE("decoding errors") {
        const auto abi = ABI::from(parse(R"({
            "structs": [
                {"base": "", "name": "type1", "fields": [{"name": "foo", "type": "type2?"}]},
                {"base": "", "name": "type2", "fields": [{"name": "bar", "type": "type3[]"}]},
                {"base": "", "name": "type3", "fields": [{"name": "baz", "type": "int8"}]}
            ]})")).value();
        const json object = parse(R"({"foo": {"bar": [{"baz": "not int"}]}})");
        const auto objectResult = Serializer::decodeObject(object, "type1", abi);
        REQUIRE_FALSE(objectResult.has_value());
        CHECK(objectResult.error().message ==
              "Decoding error at root<type1>.foo<type2?>.bar<type3[]>.0.baz<int8>: Invalid number");
        const auto data = Bytes::fromString("beefbeef", BytesEncoding::utf8).value();
        const auto dataResult = Serializer::decode(data, "type1", abi);
        REQUIRE_FALSE(dataResult.has_value());
        CHECK(dataResult.error().message ==
              "Decoding error at root<type1>.foo<type2?>.bar<type3[]>.6.baz<int8>: Read past end "
              "of buffer");
    }

    TEST_CASE("variant") {
        const auto abi = ABI::from(parse(R"({
            "structs": [{"base": "", "name": "struct", "fields": [{"name": "field1", "type": "bool"}]}],
            "variants": [{"name": "foo", "types": ["uint8", "string[]", "struct", "struct?"]}]
        })")).value();
        CHECK(Serializer::decode(Bytes::from("00ff").value(), "foo", abi).value() ==
              json::array({"uint8", 255}));
        CHECK(Serializer::decodeObject(json::array({"uint8", 255}), "foo", abi).value() ==
              json::array({"uint8", 255}));
        CHECK(Serializer::encode(json::array({"uint8", 255}), "foo", abi).value().hexString() ==
              "00ff");
        CHECK(Serializer::encode(parse(R"(["struct?", {"field1": true}])"), "foo", abi)
                  .value()
                  .hexString() == "030101");
        CHECK_FALSE(Serializer::decode(Bytes::from("04ff").value(), "foo", abi).has_value());
        CHECK_FALSE(Serializer::encode(json::array({"uint64", 255}), "foo", abi).has_value());
    }

    TEST_CASE("alias") {
        const auto abi = ABI::from(parse(R"({
            "types": [
                {"new_type_name": "super_string", "type": "string"},
                {"new_type_name": "super_foo", "type": "foo"}
            ],
            "structs": [{"base": "", "name": "foo", "fields": [{"name": "bar", "type": "string"}]}]
        })")).value();
        CHECK(Serializer::encode(json("foo"), "super_string", abi).value().hexString() ==
              "03666f6f");
        CHECK(Serializer::decode(Bytes::from("03666f6f").value(), "super_string", abi).value() ==
              "foo");
        CHECK(Serializer::encode(parse(R"({"bar":"foo"})"), "super_foo", abi).value().hexString() ==
              "03666f6f");
        CHECK(Serializer::decode(Bytes::from("03666f6f").value(), "super_foo", abi).value() ==
              parse(R"({"bar":"foo"})"));
        CHECK(Serializer::decodeObject(parse(R"({"bar":"foo"})"), "super_foo", abi).value() ==
              parse(R"({"bar":"foo"})"));
    }

    TEST_CASE("synthesize abi") {
        const ABI synthesized = Serializer::synthesize<MyTransactionVariant>();
        const json expected = parse(R"({
            "version": "eosio::abi/1.1",
            "types": [{"new_type_name": "my_transaction", "type": "transaction"}],
            "variants": [{"name": "my_variant", "types": ["string", "my_transaction"]}],
            "structs": [
                {"base": "", "name": "permission_level", "fields": [
                    {"name": "actor", "type": "name"}, {"name": "permission", "type": "name"}]},
                {"base": "", "name": "action", "fields": [
                    {"name": "account", "type": "name"}, {"name": "name", "type": "name"},
                    {"name": "authorization", "type": "permission_level[]"},
                    {"name": "data", "type": "bytes"}]},
                {"base": "", "name": "transaction_extension", "fields": [
                    {"name": "type", "type": "uint16"}, {"name": "data", "type": "bytes"}]},
                {"base": "", "name": "transaction_header", "fields": [
                    {"name": "expiration", "type": "time_point_sec"},
                    {"name": "ref_block_num", "type": "uint16"},
                    {"name": "ref_block_prefix", "type": "uint32"},
                    {"name": "max_net_usage_words", "type": "varuint32"},
                    {"name": "max_cpu_usage_ms", "type": "uint8"},
                    {"name": "delay_sec", "type": "varuint32"}]},
                {"base": "transaction_header", "name": "transaction", "fields": [
                    {"name": "context_free_actions", "type": "action[]"},
                    {"name": "actions", "type": "action[]"},
                    {"name": "transaction_extensions", "type": "transaction_extension[]"}]}
            ],
            "actions": [], "tables": [], "ricardian_clauses": [], "action_results": []
        })");
        const json actual = synthesized.toJSON();
        CHECK(actual["types"] == expected["types"]);
        CHECK(actual["variants"] == expected["variants"]);
        REQUIRE(actual["structs"].size() == expected["structs"].size());
        for (size_t i = 0; i < expected["structs"].size(); i++) {
            // array order matters, object key order does not
            CHECK(nlohmann::json(actual["structs"][i]) == nlohmann::json(expected["structs"][i]));
        }
    }

    TEST_CASE("circular alias") {
        const auto abi = ABI::from(parse(R"({
            "types": [
                {"new_type_name": "a", "type": "a"},
                {"new_type_name": "b1", "type": "b2"},
                {"new_type_name": "b2", "type": "b1"},
                {"new_type_name": "c1", "type": "c2"},
                {"new_type_name": "c2", "type": "c3"}
            ],
            "structs": [
                {"base": "", "name": "c3", "fields": [{"name": "f", "type": "c4"}]},
                {"base": "", "name": "c4", "fields": [{"name": "f", "type": "c1"}]}
            ]})")).value();
        CHECK_FALSE(Serializer::decode(Bytes::from("beef").value(), "a", abi).has_value());
        CHECK_FALSE(Serializer::decode(Bytes::from("beef").value(), "b1", abi).has_value());
        CHECK_FALSE(Serializer::decode(Bytes::from("beef").value(), "c1", abi).has_value());
        CHECK_FALSE(
            Serializer::encode(parse(R"({"f": {"f": {}}})"), "c1", abi).has_value());
    }

    TEST_CASE("complex type") {
        const json object = parse(R"({
            "things": ["transaction[]", [{
                "actions": [{
                    "account": "eosio.token",
                    "name": "transfer",
                    "authorization": [{"actor": "foo", "permission": "active"}],
                    "data": "000000000000285d000000000000ae39e80300000000000003454f53000000000b68656c6c6f207468657265"
                }],
                "context_free_actions": [],
                "delay_sec": 123,
                "expiration": "2018-02-15T00:00:00",
                "max_cpu_usage_ms": 99,
                "max_net_usage_words": 0,
                "ref_block_num": 0,
                "ref_block_prefix": 0,
                "transaction_extensions": []
            }]],
            "self": {
                "things": ["do_you_even", 2],
                "self": {
                    "things": ["do_you_even", "-170141183460469231731687303715884105727"]
                }
            }
        })");
        const auto complex = structFrom<Complex>(object).value();
        const auto encoded = Serializer::encode(complex).value();
        const auto recoded = Serializer::decode<Complex>(encoded).value();
        CHECK(deepEqual(Serializer::objectify(recoded), object));
        CHECK(recoded.self->self->things.get_if<DoYouEven>() != nullptr);
    }

    TEST_CASE("typestresser abi") {
        const auto abi = ABI::from(loadFixture("typestresser.abi.json")).value();
        const json object = parse(R"({
            "bool": true,
            "int8": 127,
            "uint8": 255,
            "int16": 32767,
            "uint16": 65535,
            "int32": 2147483647,
            "uint32": 4294967295,
            "int64": "9223372036854775807",
            "uint64": "18446744073709551615",
            "int128": "170141183460469231731687303715884105727",
            "uint128": "340282366920938463463374607431768211455",
            "varint32": 2147483647,
            "varuint32": 4294967295,
            "float32": "3.1415925",
            "float64": "3.141592653589793",
            "float128": "0xbeefbeefbeefbeefbeefbeefbeefbeef",
            "time_point": "2020-02-02T02:02:02.222",
            "time_point_sec": "2020-02-02T02:02:02",
            "block_timestamp_type": "2020-02-02T02:02:02.500",
            "name": "foobar",
            "bytes": "beef",
            "string": "hello",
            "checksum160": "ffffffffffffffffffffffffffffffffffffffff",
            "checksum256": "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "checksum512": "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "public_key": "PUB_K1_5AHoNnWetuDhKWSDx3WUf8W7Dg5xjHCMc4yHmmSiaJCFvvAgnB",
            "signature": "SIG_K1_KfPLgpw35iX8nfDzhbcmSBCr7nEGNEYXgmmempQspDJYBCKuAEs5rm3s4ZuLJY428Ca8ZhvR2Dkwu118y3NAoMDxhicRj9",
            "symbol": "7,PI",
            "symbol_code": "PI",
            "asset": "3.1415926 PI",
            "extended_asset": {"quantity": "3.1415926 PI", "contract": "pi.token"},
            "alias1": true,
            "alias2": true,
            "alias3": {"bool": true},
            "alias4": ["int8", 1],
            "alias5": [true, true],
            "alias6": null,
            "extension": {
                "message": "hello",
                "extension": {"message": "world", "extension": null}
            }
        })");
        const auto data = Serializer::encode(object, "all_types", abi).value();
        CHECK(data.hexString() ==
              "017fffff7fffffffffff7fffffffffffffffffffffff7fffffffffffffffffffffffffffffffffffffff"
              "ffffffff7ffffffffffffffffffffffffffffffffffeffffff0fffffffff0fda0f4940182d4454fb2109"
              "40beefbeefbeefbeefbeefbeefbeefbeefb07d56318e9d05009a2d365e35d4914b000000005c73285d02"
              "beef0568656c6c6fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
              "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
              "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff0002"
              "23e0ae8aacb41b06dc74af1a56b2eb69133f07f7f75bd1d5e53316bff195edf400205150a67288c3b393"
              "fdba9061b05019c54b12bdac295fc83bebad7cd63c7bb67d5cb8cc220564da006240a58419f64d06a5c6"
              "e1fc62889816a6c3dfdd231ed38907504900000000005049000000000000765edf010000000007504900"
              "00000000765edf0100000000075049000000000000000053419a81ab0101010001020101000568656c6c"
              "6f0105776f726c6400");
        const auto decoded = Serializer::decode(data, "all_types", abi).value();
        CHECK(deepEqual(decoded, object));
    }

    TEST_CASE("object-only any coding") {
        const auto decoded = structFrom<AnyStruct>(parse(R"({
            "foo": "hello",
            "bar": [1, "two", false],
            "account": "foobar1234"
        })")).value();
        CHECK(deepEqual(Serializer::objectify(decoded),
                        parse(R"({"foo":"hello","bar":[1,"two",false],"account":"foobar1234"})")));
        const ABI abi = Serializer::synthesize<AnyStruct>();
        const auto decoded2 = Serializer::decodeObject(parse(R"({
            "foo": {"nested": "obj"},
            "bar": [],
            "baz": {"b": {"a": {"z": "zz"}}},
            "account": "foo"
        })"), "my_struct", abi).value();
        CHECK(deepEqual(decoded2, parse(R"({
            "foo": {"nested": "obj"},
            "bar": [],
            "baz": {"b": {"a": {"z": "zz"}}},
            "account": "foo"
        })")));
        CHECK_FALSE(Serializer::decode<AnyStruct>(Bytes::from("beef").value()).has_value());
        CHECK_FALSE(Serializer::encode(decoded).has_value());
    }

    TEST_CASE("coding with type descriptors") {
        const ABI noAbi;
        CHECK(Serializer::decode(Bytes::from("020000ffff").value(), "uint16[]", noAbi).value() ==
              json::array({0, 65535}));
        CHECK(Serializer::decode(Bytes::from("00").value(), "transaction?", noAbi).value() ==
              json(nullptr));
        CHECK(Serializer::decodeObject(json::array({false, true, false}), "bool[]", noAbi)
                  .value() == json::array({false, true, false}));
        const std::vector<U16Struct> typed = {{.foo = 0}, {.foo = 65535}};
        CHECK(Serializer::encode(typed).value().hexString() == "020000ffff");
        const auto decoded =
            Serializer::decode<std::vector<U16Struct>>(Bytes::from("020000ffff").value()).value();
        REQUIRE(decoded.size() == 2);
        CHECK(decoded[0].foo == 0);
        CHECK(decoded[1].foo == 65535);
    }

    TEST_CASE("unicode") {
        const auto data = Serializer::encode(std::string("\xf0\x9f\x98\xb7")).value();
        const auto text = Serializer::decode<std::string>(data).value();
        CHECK(text == "\xf0\x9f\x98\xb7");
    }

    TEST_CASE("abi resolve all") {
        const auto abi = ABI::from(parse(R"({
            "types": [
                {"new_type_name": "a", "type": "a"},
                {"new_type_name": "b1", "type": "b2"},
                {"new_type_name": "b2", "type": "b1"},
                {"new_type_name": "c1", "type": "c2"},
                {"new_type_name": "c2", "type": "c3"}
            ],
            "structs": [
                {"base": "", "name": "c3", "fields": [{"name": "f", "type": "c4"}]},
                {"base": "", "name": "c4", "fields": [{"name": "f", "type": "c1"}]},
                {"base": "c4", "name": "c5", "fields": [{"name": "f2", "type": "c5[]?"}]}
            ],
            "variants": [{"name": "c6", "types": ["a", "b1", "c1", "c5"]}]
        })")).value();
        const auto types = abi.resolveAll();
        int maxId = 0;
        const auto scan = [&maxId](const std::vector<ABI::ResolvedType>& list) {
            for (const auto& t : list) {
                if (t->id > maxId) maxId = t->id;
            }
        };
        scan(types.types);
        scan(types.structs);
        scan(types.variants);
        CHECK(maxId == 9);
    }

    TEST_CASE("objectify") {
        const auto tx = Transaction::from(parse(R"({
            "ref_block_num": 123,
            "ref_block_prefix": 456,
            "expiration": 992,
            "actions": [{
                "account": "eosio.token",
                "name": "transfer",
                "authorization": [{"actor": "foo", "permission": "active"}],
                "data": "0000000000855c340000000000000e3da40100000000000001474d5a0000000007666f7220796f75"
            }]
        })")).value();
        CHECK(deepEqual(Serializer::objectify(tx), parse(R"({
            "expiration": "1970-01-01T00:16:32",
            "ref_block_num": 123,
            "ref_block_prefix": 456,
            "max_net_usage_words": 0,
            "max_cpu_usage_ms": 0,
            "delay_sec": 0,
            "context_free_actions": [],
            "actions": [{
                "account": "eosio.token",
                "name": "transfer",
                "authorization": [{"actor": "foo", "permission": "active"}],
                "data": "0000000000855c340000000000000e3da40100000000000001474d5a0000000007666f7220796f75"
            }],
            "transaction_extensions": []
        })")));
    }

    TEST_CASE("struct optional field") {
        CHECK(structFrom<OptStruct>(parse(R"({"a": "foo"})")).has_value());
        const auto missing = structFrom<OptStruct>(parse(R"({"b": "foo"})"));
        REQUIRE_FALSE(missing.has_value());
        CHECK(missing.error().message.find("encountered undefined for non-optional") !=
              std::string::npos);
    }

    TEST_CASE("abi def") {
        const auto abi = ABI::from(parse(R"({
            "types": [{"new_type_name": "b", "type": "a"}],
            "structs": [{"base": "", "name": "a", "fields": [{"name": "f", "type": "a"}]}],
            "tables": [{"name": "t", "type": "a", "index_type": "i64", "key_names": ["k"], "key_types": ["i64"]}],
            "ricardian_clauses": [{"id": "foo", "body": "bar"}],
            "variants": [{"name": "v", "types": ["a", "b"]}]
        })")).value();
        const auto data = Serializer::encode(abi).value();
        CHECK(data.hexString() ==
              "0e656f73696f3a3a6162692f312e310101620161010161000101660161000100000000000000c8036936"
              "3401016b010369363401610103666f6f036261720000010176020161016200");
        const auto decoded = Serializer::decode<ABI>(data).value();
        CHECK(decoded.equals(abi));
    }

    TEST_CASE("binary extensions strict") {
        // the ManyExtensions layout from serializer.ts as a plain ABI (the
        // synthesized/loose-coupling form: super_int resolves to uint8)
        const ABI abi = ABI::from(json{
            {"version", "eosio::abi/1.2"},
            {"types", json::array({{{"new_type_name", "super_int"}, {"type", "uint8"}}})},
            {"variants",
             json::array({{{"name", "jazz_variant"},
                           {"types", json::array({"super_int", "string"})}}})},
            {"structs",
             json::array(
                 {{{"name", "info_pair"},
                   {"base", ""},
                   {"fields", json::array({{{"name", "key"}, {"type", "string"}},
                                           {{"name", "value"}, {"type", "bytes"}}})}},
                  {{"name", "many_extensions"},
                   {"base", ""},
                   {"fields",
                    json::array({{{"name", "name"}, {"type", "string"}},
                                 {{"name", "info"}, {"type", "info_pair[]$"}},
                                 {{"name", "singleInfo"}, {"type", "info_pair$"}},
                                 {{"name", "uint32"}, {"type", "uint32$"}},
                                 {{"name", "asset"}, {"type", "asset$"}},
                                 {{"name", "checksum256"}, {"type", "checksum256$"}},
                                 {{"name", "superInt"}, {"type", "super_int$"}},
                                 {{"name", "jazz"}, {"type", "jazz_variant$"}},
                                 {{"name", "maybeJazz"}, {"type", "jazz_variant?$"}},
                                 {{"name", "dumbBool"}, {"type", "bool?$"}},
                                 {{"name", "bool"}, {"type", "bool$"}}})}}})}})
                            .value();
        const auto data = Bytes::from("03666f6f").value();
        SUBCASE("binary decode synthesizes defaults") {
            const auto res =
                Serializer::decode(data.array, "many_extensions", abi,
                                   {.strictExtensions = true})
                    .value();
            CHECK(res["name"] == "foo");
            CHECK(res["info"] == json::array());
            CHECK(res["singleInfo"]["key"] == "");
            CHECK(res["uint32"] == 0);
            CHECK(res["asset"] == "0.0000 SYS");
            CHECK(res["checksum256"] ==
                  "0000000000000000000000000000000000000000000000000000000000000000");
            CHECK(res["superInt"] == 0);
            CHECK(res["jazz"] == json::array({"super_int", 0}));
            CHECK(res["maybeJazz"].is_null());
            CHECK(res["dumbBool"].is_null());
            CHECK(res["bool"] == false);
        }
        SUBCASE("without strict the extensions stay absent") {
            const auto res = Serializer::decode(data, "many_extensions", abi).value();
            CHECK(res["uint32"].is_null());
            CHECK(res["bool"].is_null());
        }
        SUBCASE("object decode synthesizes defaults") {
            const auto res = Serializer::decodeObject(json{{"name", "foo"},
                                                           {"dumbBool", false}},
                                                      "many_extensions", abi,
                                                      {.strictExtensions = true})
                                 .value();
            CHECK(res["uint32"] == 0);
            CHECK(res["jazz"] == json::array({"super_int", 0}));
            CHECK(res["dumbBool"] == false);
            CHECK(res["bool"] == false);
        }
        SUBCASE("circular type reference errors") {
            json circular = abi.toJSON();
            circular["structs"][1]["fields"][1]["type"] = "many_extensions$";
            const ABI patched = ABI::from(circular).value();
            const auto res = Serializer::decode(data.array, "many_extensions", patched,
                                                {.strictExtensions = true});
            REQUIRE_FALSE(res.has_value());
            CHECK(res.error().message.find("Circular type reference") != std::string::npos);
        }
        SUBCASE("optional shouldnt return null") {
            // strict mode never synthesizes plain optionals
            const ABI approveAbi =
                ABI::from(json{
                    {"version", "eosio::abi/1.2"},
                    {"structs",
                     json::array(
                         {{{"name", "permission_level"},
                           {"base", ""},
                           {"fields",
                            json::array({{{"name", "actor"}, {"type", "name"}},
                                         {{"name", "permission"}, {"type", "name"}}})}},
                          {{"name", "approve"},
                           {"base", ""},
                           {"fields",
                            json::array(
                                {{{"name", "proposer"}, {"type", "name"}},
                                 {{"name", "proposal_name"}, {"type", "name"}},
                                 {{"name", "level"}, {"type", "permission_level"}},
                                 {{"name", "proposal_hash"},
                                  {"type", "checksum256?"}}})}}})}})
                    .value();
            const auto res =
                Serializer::decodeObject(json{{"proposer", "foo"},
                                              {"proposal_name", "bar"},
                                              {"level",
                                               {{"actor", "foo"}, {"permission", "bar"}}}},
                                         "approve", approveAbi, {.strictExtensions = true})
                    .value();
            CHECK(res["proposal_hash"].is_null());
        }
    }

    TEST_CASE("action_results") {
        const auto abi = ABI::from(parse(R"({
            "version": "eosio::abi/1.2",
            "structs": [
                {"name": "Result", "base": "", "fields": [{"name": "id", "type": "uint32"}]},
                {"name": "test", "base": "", "fields": [{"name": "eos_account", "type": "name"}]}
            ],
            "actions": [{"name": "test", "type": "test", "ricardian_contract": ""}],
            "action_results": [{"name": "test", "result_type": "Result"}]
        })")).value();
        const auto encoded = Serializer::encode(abi).value();
        const auto decoded = Serializer::decode<ABI>(encoded).value();
        CHECK(abi.equals(decoded));
        REQUIRE(decoded.action_results.size() == 1);
        CHECK(decoded.action_results[0].name == Name::from("test"));
        CHECK(decoded.action_results[0].result_type == "Result");
    }
}
