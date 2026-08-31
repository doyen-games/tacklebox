// Port of antelope test/chain.ts. Cases needing types from later Phase 1 items
// (Action, Transaction, Authority, keys, ABI, Serializer) join as those land.
#include <doctest/doctest.h>

#include <dwarfkit/antelope/chain/asset.hpp>
#include <dwarfkit/antelope/chain/authority.hpp>
#include <dwarfkit/antelope/chain/block_id.hpp>
#include <dwarfkit/antelope/chain/blob.hpp>
#include <dwarfkit/antelope/chain/bytes.hpp>
#include <dwarfkit/antelope/chain/checksum.hpp>
#include <dwarfkit/antelope/chain/float.hpp>
#include <dwarfkit/antelope/chain/name.hpp>
#include <dwarfkit/antelope/chain/time.hpp>
#include <dwarfkit/antelope/chain/transaction.hpp>

using namespace dwarfkit;

namespace {

struct TestTransfer {
    DK_STRUCT("transfer")
    Name from;
    Name to;
    Asset quantity;
    std::string memo;
    DK_FIELDS(from, to, quantity, memo)
};

}  // namespace

static_assert("foo"_n == Name::from("foo"));
static_assert("eosio.token"_n.value == Name::from("eosio.token").value);
static_assert("1.0000 EOS"_asset.units == 10000);
static_assert("4,EOS"_symbol.precision() == 4);

TEST_SUITE("chain") {
    TEST_CASE("asset") {
        CHECK(Asset::from("-1.2345 NEGS").value().toString() == "-1.2345 NEGS");
        CHECK(Asset::from("-0.2345 NEGS").value().toString() == "-0.2345 NEGS");
        CHECK(Asset::from("0.0000000000000 DUCKS").value().toString() == "0.0000000000000 DUCKS");
        CHECK(Asset::from("99999999999 DUCKS").value().toString() == "99999999999 DUCKS");
        CHECK(Asset::from("-99999999999 DUCKS").value().toString() == "-99999999999 DUCKS");
        CHECK(Asset::from("-0.0000000000001 DUCKS").value().toString() == "-0.0000000000001 DUCKS");

        auto asset = Asset::from("1.000000000 FOO").value();
        CHECK(asset.value() == 1.0);
        REQUIRE(asset.setValue(asset.value() + 0.000000001).has_value());
        CHECK(asset.value() == 1.000000001);
        REQUIRE(asset.setValue(-100).has_value());
        CHECK(asset.toString() == "-100.000000000 FOO");
        CHECK(asset.units == -100000000000);

        const auto symbol = Asset::Symbol::from("10,K").value();
        CHECK(symbol.name() == "K");
        CHECK(symbol.precision() == 10);
        CHECK(Asset::Symbol::from(symbol.value).value().toString() == symbol.toString());

        CHECK_FALSE(Asset::Symbol::from("0,0").has_value());

        const auto nft_symbol = Asset::Symbol::from("0,").value();
        CHECK(nft_symbol.name() == "");
        CHECK(nft_symbol.precision() == 0);
        CHECK(nft_symbol.value == 0);
        CHECK(nft_symbol.code().value == 0);
        CHECK(Asset::Symbol::from(nft_symbol.value).value().toString() == nft_symbol.toString());

        // test null asset
        asset = Asset::from("0 ").value();
        CHECK(asset.value() == 0);
        CHECK(asset.toString() == "0 ");

        asset = Asset::from(10, "4,POX").value();
        CHECK(asset.value() == 10);
        CHECK(asset.units == 100000);

        asset = Asset::fromUnits(1, "10,KEK").value();
        CHECK(asset.value() == 0.0000000001);
        REQUIRE(asset.setValue(asset.value() + 0.0000000001).has_value());
        CHECK(asset.units == 2);

        asset = Asset::from(3.004, "4,RAR").value();
        REQUIRE(asset.setValue(asset.value() + 1).has_value());
        CHECK(asset.toString() == "4.0040 RAR");
        CHECK(asset.value() == 4.004);

        asset = Asset::from(3.004, "8,RAR").value();
        REQUIRE(asset.setValue(asset.value() + 1).has_value());
        CHECK(asset.units == 400400000);
        CHECK(asset.toString() == "4.00400000 RAR");
        CHECK(asset.value() == 4.004);

        CHECK(symbol.convertUnits(9223372036854775807ll).error().message ==
              "Number can only safely store up to 53 bits");
        CHECK_FALSE(Asset::from("").has_value());
        CHECK_FALSE(Asset::from("1POP").has_value());
        CHECK_FALSE(Asset::from("1.0000000000000000000000 BIGS").has_value());
        CHECK_FALSE(Asset::from("1.2 horse").has_value());
        CHECK_FALSE(Asset::Symbol::from("12").has_value());
    }

    TEST_CASE("block id") {
        const std::string string =
            "048865fb643bca3b644647177f0cf363f7956794d0a7ec3bc6d29d93d9637308";
        const auto blockId = BlockId::from(string).value();
        CHECK(blockId.toString() == string);
        CHECK(blockId.blockNum() == 76047867);
        const auto blockId2 =
            BlockId::fromBlockChecksum(
                "61375f2d5fbe6bbad86e424962a190e8309394b7bff4bf3e16b0a2a71e5a617c", 7)
                .value();
        CHECK(blockId2.toString() ==
              "000000075fbe6bbad86e424962a190e8309394b7bff4bf3e16b0a2a71e5a617c");
        CHECK(blockId2.blockNum() == 7);
    }

    TEST_CASE("blob") {
        const auto expected = Bytes(std::vector<uint8_t>{0xbe, 0xef, 0xfa, 0xce});

        // Correct
        CHECK(Bytes::from(Blob::from("vu/6zg==").value().array).equals(expected));
        // Wrong padding, ensure it still works
        CHECK(Bytes::from(Blob::from("vu/6zg=").value().array).equals(expected));
        CHECK(Bytes::from(Blob::from("vu/6zg").value().array).equals(expected));
        CHECK(Bytes::from(Blob::from("vu/6zg===").value().array).equals(expected));
        // round trip
        CHECK(Blob::from("vu/6zg==").value().base64String() == "vu/6zg==");
    }

    TEST_CASE("bytes") {
        CHECK(Bytes::from("hello", BytesEncoding::utf8).value().toString(BytesEncoding::hex) ==
              "68656c6c6f");
        CHECK(Bytes::equal(Bytes::from("beef").value(), Bytes::from("beef").value()) == true);
        CHECK(Bytes::equal(Bytes::from("beef").value(), Bytes::from("face").value()) == false);
        CHECK(Bytes::from("68656c6c6f").value().toString(BytesEncoding::utf8) == "hello");
        CHECK(Bytes(std::vector<uint8_t>{0xff, 0x00, 0xff, 0x00}).copy().hexString() == "ff00ff00");
        CHECK(Checksum256::hash(Bytes::from("hello world", BytesEncoding::utf8).value())
                  .hexString() ==
              "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
        CHECK(Checksum512::hash(Bytes::from("hello world", BytesEncoding::utf8).value())
                  .hexString() ==
              "309ecc489c12d6eb4cc40f50c902f2b4d0ed77ee511a7c7a9bcd3ca86d4cd86f"
              "989dd35bc5ff499670da34255b45b0cfd830e81f605dcf7dc5542e93ae9cd76f");
        CHECK(Checksum160::hash(Bytes::from("hello world", BytesEncoding::utf8).value())
                  .hexString() == "98c615784ccb5fe5936fbc0cbe9dfdb408d92f0f");
        CHECK(Bytes::from("beef").value().zeropadded(4).toString() == "0000beef");
        CHECK(Bytes::from("beef").value().zeropadded(2).toString() == "beef");
        CHECK(Bytes::from("beef").value().zeropadded(1).toString() == "beef");
        CHECK(Bytes::from("beef").value().zeropadded(1, true).toString() == "be");
        CHECK(Bytes::from("beef").value().zeropadded(2, true).toString() == "beef");
        CHECK(Bytes::from("beef").value().zeropadded(3, true).toString() == "00beef");
    }

    TEST_CASE("time") {
        const int64_t now = 1629864000123;
        CHECK(TimePoint::fromMilliseconds(static_cast<double>(now)).toMilliseconds() == now);
        CHECK(TimePointSec::from(TimePointSec::fromMilliseconds(static_cast<double>(now)))
                  .toMilliseconds() == 1629864000000);
        CHECK_FALSE(TimePoint::from("blah").has_value());
        CHECK(BlockTimestamp::from("2021-08-25T02:37:24.500").value().toString() ==
              "2021-08-25T02:37:24.500");
        CHECK(BlockTimestamp::fromMilliseconds(static_cast<double>(now)).toMilliseconds() ==
              1629864000000);
        CHECK(TimePoint::from("2021-08-25T04:00:59").value().toString() ==
              "2021-08-25T04:00:59.000");
        // upstream builds "<value>ZZ" for strings that already carry a zone
        CHECK_FALSE(TimePoint::from("2021-08-25T04:00:59Z").has_value());
        CHECK_FALSE(TimePointSec::from("2021-08-25T04:00:59+01:00").has_value());
    }

    TEST_CASE("equality helpers") {
        const auto name = Name::from("foo");
        CHECK(name.equals("foo") == true);
        CHECK(name.equals(Name::from(6712615244595724288ull)) == true);
        CHECK(name.equals(Name::from(uint64_t(12345))) == false);
        CHECK(name.equals("bar") == false);

        const auto checksum = Checksum160::hash(Bytes::from("hello", BytesEncoding::utf8).value());
        CHECK(checksum.equals("108f07b8382412612c048d07d13f814118445acd") == true);
        CHECK(checksum.equals("108f07b8382412612c048d07d13f814118445abe") == false);

        const auto time = TimePointSec::from(1);
        CHECK(time.equals(time) == true);
        CHECK(time.equals("1970-01-01T00:00:01") == true);
        CHECK(time.equals("2020-02-20T02:20:20") == false);
        CHECK(time.equals(TimePointSec::from(1)) == true);
        CHECK(time.equals(TimePointSec::from(2)) == false);
        CHECK(time.equals(TimePoint::from(int64_t(1) * 1000000)) == true);
    }

    TEST_CASE("name") {
        CHECK(Name::from("foo").toString() == "foo");
        CHECK(Name::from("foo").value == 6712615244595724288ull);
        CHECK(Name::from(".gems").toString() == ".gems");
        CHECK(Name::from("foo....").toString() == "foo");
        CHECK(Name::from("").value == 0);
        CHECK(Name::from("").toString() == "");
        CHECK(Name::from("eosio.token").toString() == "eosio.token");
    }

    TEST_CASE("float128") {
        const std::string hex = "0100000000000000000000000000ff7f";
        const auto value = Float128::from(hex).value();
        CHECK(value.toString() == "0x" + hex);
        CHECK(Float128::from("0x" + hex).value() == value);
        CHECK(Float128::from("beef").error().message == "Invalid float128");
    }

    TEST_CASE("transaction") {
        const auto action = Action::from(json{{"authorization", json::array()},
                                              {"account", "eosio.token"},
                                              {"name", "transfer"}},
                                         TestTransfer{.from = "foo"_n,
                                                      .to = "bar"_n,
                                                      .quantity = "1.0000 EOS"_asset,
                                                      .memo = "hello"})
                                .value();
        CHECK(action.abi != nullptr);
        const auto transaction =
            Transaction::from(json{{"ref_block_num", 0},
                                   {"ref_block_prefix", 0},
                                   {"expiration", 0},
                                   {"actions", json::array({Serializer::objectify(action)})}})
                .value();
        CHECK(transaction.id().hexString() ==
              "97b4d267ce0e0bd6c78c52f85a27031bd16def0920703ca3b72c28c2c5a1a79b");
        const auto transfer = transaction.actions[0].decodeData<TestTransfer>().value();
        CHECK(transfer.from.toString() == "foo");

        json signedJson = Serializer::objectify(transaction);
        signedJson["signatures"] = json::array(
            {"SIG_K1_KdNTcLLSyzUFC4AdMxEDn58X8ZN368euanvet4jucUdSPXvLkgsG32tpcqVvnDR9Xv1f7HsTm6"
             "kocjeZzFGvUSc2yCbdEA"});
        const auto signedTx = SignedTransaction::from(signedJson).value();
        CHECK(signedTx.id().hexString() == transaction.id().hexString());
    }

    TEST_CASE("any transaction") {
        const json tx = {
            {"delay_sec", 0},
            {"expiration", "2020-07-01T17:32:13"},
            {"max_cpu_usage_ms", 0},
            {"max_net_usage_words", 0},
            {"ref_block_num", 55253},
            {"ref_block_prefix", 3306698594},
            {"actions",
             json::array(
                 {{{"account", "eosio.token"},
                   {"name", "transfer"},
                   {"authorization", json::array({{{"actor", "foo"}, {"permission", "active"}}})},
                   {"data",
                    {{"from", "donkeyhunter"},
                     {"memo", "Anchor is the best! Thank you <3"},
                     {"quantity", "0.0001 EOS"},
                     {"to", "teamgreymass"}}}}})},
        };
        const json abiJson = {
            {"structs", json::array({{{"base", ""},
                                      {"name", "transfer"},
                                      {"fields", json::array({{{"name", "from"}, {"type", "name"}},
                                                              {{"name", "to"}, {"type", "name"}},
                                                              {{"name", "quantity"}, {"type", "asset"}},
                                                              {{"name", "memo"}, {"type", "string"}}})}}})},
            {"actions", json::array({{{"name", "transfer"},
                                      {"type", "transfer"},
                                      {"ricardian_contract", ""}}})},
        };
        const auto abi = ABI::from(abiJson).value();
        const auto r1 = Transaction::from(tx, abi).value();
        const auto r2 =
            [&] {
                std::vector<AbiProviderEntry> entries;
                AbiProviderEntry entry;
                entry.contract = "eosio.token"_n;
                entry.abi = abi;
                entries.push_back(entry);
                return Transaction::from(tx, entries).value();
            }();
        CHECK(r1.equals(r2));
        // deepEqual semantics: order-insensitive compare via unordered json
        CHECK(nlohmann::json(r1.actions[0].decodeData(abi).value()) ==
              nlohmann::json(tx["actions"][0]["data"]));
        CHECK_FALSE(Transaction::from(tx).has_value());
        CHECK_FALSE(
            [&] {
                std::vector<AbiProviderEntry> entries;
                AbiProviderEntry entry;
                entry.contract = Name::from("ethereum.token");
                entry.abi = abi;
                entries.push_back(entry);
                return Transaction::from(tx, entries).has_value();
            }());
    }

    TEST_CASE("action with no arguments") {
        const json abiJson = {
            {"structs", json::array({{{"name", "noop"}, {"base", ""}, {"fields", json::array()}}})},
            {"actions", json::array({{{"name", "noop"},
                                      {"type", "noop"},
                                      {"ricardian_contract", ""}}})},
        };
        const auto abi = ABI::from(abiJson).value();
        const json base = {{"account", "greymassnoop"},
                           {"name", "noop"},
                           {"authorization",
                            json::array({{{"actor", "greymassfuel"}, {"permission", "cosign"}}})}};
        json j1 = base;
        j1["data"] = "";
        json j2 = base;
        j2["data"] = json::object();
        json j3 = base;
        j3["data"] = json::array();
        const auto a1 = Action::from(j1, abi).value();
        const auto a2 = Action::from(j2, abi).value();
        const auto a3 = Action::from(j3, abi).value();
        CHECK(a1.equals(a2));
        CHECK(a1.equals(a3));
    }

    TEST_CASE("action can deserialize itself from abi") {
        const json abiJson = {
            {"structs", json::array({{{"name", "transfer"},
                                      {"base", ""},
                                      {"fields", json::array({{{"name", "from"}, {"type", "name"}},
                                                              {{"name", "to"}, {"type", "name"}},
                                                              {{"name", "quantity"}, {"type", "asset"}},
                                                              {{"name", "memo"}, {"type", "string"}}})}}})},
            {"actions", json::array({{{"name", "transfer"},
                                      {"type", "transfer"},
                                      {"ricardian_contract", ""}}})},
        };
        const auto abi = ABI::from(abiJson).value();
        const auto action = Action::from(json{{"account", "eosio.token"},
                                              {"name", "transfer"},
                                              {"authorization",
                                               json::array({{{"actor", "foo"},
                                                             {"permission", "bar"}}})},
                                              {"data",
                                               {{"from", "foo"},
                                                {"to", "bar"},
                                                {"quantity", "1.0000 EOS"},
                                                {"memo", "hello"}}}},
                                         abi)
                                .value();
        CHECK(action.abi != nullptr);
        const auto decoded = action.decoded().value();
        CHECK(decoded["account"] == "eosio.token");
        CHECK(decoded["data"]["from"] == "foo");
        CHECK(decoded["data"]["quantity"] == "1.0000 EOS");
    }

    TEST_CASE("action does not exist in ABI") {
        const auto abi = ABI::from(json::object()).value();
        const auto result = Action::from(json{{"account", "foo"},
                                              {"name", "bar"},
                                              {"authorization",
                                               json::array({{{"actor", "foo"},
                                                             {"permission", "bar"}}})},
                                              {"data", json::object()}},
                                         abi);
        CHECK_FALSE(result.has_value());
        CHECK(result.error().message ==
              "The action \"bar\" does not exist on the ABI provided.");
    }

    TEST_CASE("authority sorts mixed K1 and WA keys") {
        // Reported in wharfkit/antelope#8, where localeCompare returns the wrong order.
        const std::vector<std::string> input = {
            "EOS5fMyAUopVJv88Wb4szbLH2ds65jiNCjv1XWRRyvrfR6oEBdZXk",
            "PUB_WA_323xpHU17pKZ6VsygcdXxq7cgosxSyRU5KGevyjUNtw4m9Y63EzPs3SEVpsf7rjeVLa7",
            "PUB_WA_4B6ZbE2hxTvcrndvS8758EjGqRMQqVoV4vBTGgvqi27HNw8xyQz6viKrGLNLcaiRmhmbuyu584Hf",
            "PUB_WA_4vD5irsd1GdmTEhea5G8QideW3NqU8F5zgPLyD3wKDE7MUPAwo5nELCEbJEELDafLeV4Uz7djSFJ",
            "PUB_WA_5Q6G5dqajZDkqDbgEUG7a7qMpe94P6LegYdf7h8yL9efjhC6ERWuFsJM1ueygEmXzaELBokrUeH8",
            "PUB_WA_6wUAAJXLFc3edKhGb5DdWb3WmoLxLwFSspdiEYHvT6DN2X7zo6opCfv6TcAifvxRQdVYwcr84zMS",
        };
        json keys = json::array();
        for (const auto& key : input) {
            keys.push_back({{"key", key}, {"weight", 1}});
        }
        const auto auth = Authority::from(json{{"threshold", 1}, {"keys", keys}}).value();
        const std::vector<std::string> expected = {
            "PUB_K1_5fMyAUopVJv88Wb4szbLH2ds65jiNCjv1XWRRyvrfR6oDcpM7y",
            "PUB_WA_4B6ZbE2hxTvcrndvS8758EjGqRMQqVoV4vBTGgvqi27HNw8xyQz6viKrGLNLcaiRmhmbuyu584Hf",
            "PUB_WA_4vD5irsd1GdmTEhea5G8QideW3NqU8F5zgPLyD3wKDE7MUPAwo5nELCEbJEELDafLeV4Uz7djSFJ",
            "PUB_WA_5Q6G5dqajZDkqDbgEUG7a7qMpe94P6LegYdf7h8yL9efjhC6ERWuFsJM1ueygEmXzaELBokrUeH8",
            "PUB_WA_323xpHU17pKZ6VsygcdXxq7cgosxSyRU5KGevyjUNtw4m9Y63EzPs3SEVpsf7rjeVLa7",
            "PUB_WA_6wUAAJXLFc3edKhGb5DdWb3WmoLxLwFSspdiEYHvT6DN2X7zo6opCfv6TcAifvxRQdVYwcr84zMS",
        };
        REQUIRE(auth.keys.size() == expected.size());
        for (size_t i = 0; i < expected.size(); i++) {
            CHECK(auth.keys[i].key.toString() == expected[i]);
        }
    }

    TEST_CASE("authority sorts accounts and waits") {
        const auto auth =
            Authority::from(
                json{{"threshold", 1},
                     {"accounts",
                      json::array(
                          {{{"permission", {{"actor", "zzz"}, {"permission", "active"}}},
                            {"weight", 1}},
                           {{"permission", {{"actor", "aaa"}, {"permission", "zzz"}}},
                            {"weight", 1}},
                           {{"permission", {{"actor", "aaa"}, {"permission", "active"}}},
                            {"weight", 1}}})},
                     {"waits", json::array({{{"wait_sec", 3600}, {"weight", 1}},
                                            {{"wait_sec", 60}, {"weight", 1}},
                                            {{"wait_sec", 86400}, {"weight", 1}}})}})
                .value();
        REQUIRE(auth.accounts.size() == 3);
        CHECK(auth.accounts[0].permission.toString() == "aaa@active");
        CHECK(auth.accounts[1].permission.toString() == "aaa@zzz");
        CHECK(auth.accounts[2].permission.toString() == "zzz@active");
        REQUIRE(auth.waits.size() == 3);
        CHECK(auth.waits[0].wait_sec == 60);
        CHECK(auth.waits[1].wait_sec == 3600);
        CHECK(auth.waits[2].wait_sec == 86400);
    }

    TEST_CASE("authority") {
        const auto auth =
            Authority::from(
                json{{"threshold", 21},
                     {"keys",
                      json::array(
                          {{{"key", "EOS6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeABhJRin"},
                            {"weight", 20}},
                           {{"key", "PUB_R1_82ua5qburg82c9eWY1qZVNUAAD6VPHsTMoPMGDrk7s4BQgxEoc"},
                            {"weight", 2}}})},
                     {"waits", json::array({{{"wait_sec", 10}, {"weight", 1}}})}})
                .value();
        CHECK(auth.hasPermission("EOS6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeABhJRin").value());
        CHECK(auth.hasPermission("PUB_R1_82ua5qburg82c9eWY1qZVNUAAD6VPHsTMoPMGDrk7s4BQgxEoc", true)
                  .value());
        CHECK_FALSE(
            auth.hasPermission("PUB_R1_82ua5qburg82c9eWY1qZVNUAAD6VPHsTMoPMGDrk7s4BQgxEoc")
                .value());
        CHECK_FALSE(
            auth.hasPermission("PUB_K1_6E45rq9ZhnvnWNTNEEexpM8V8rqCjggUWHXJBurkVQSnEyCHQ9")
                .value());
        CHECK_FALSE(
            auth.hasPermission("PUB_K1_6E45rq9ZhnvnWNTNEEexpM8V8rqCjggUWHXJBurkVQSnEyCHQ9", true)
                .value());
    }

    TEST_CASE("packed transaction") {
        // uncompressed packed transaction
        const auto uncompressed = PackedTransaction::from(
            json{{"packed_trx",
                  "34b6c664cb1b3056b588000000000190e2a51c5f25af590000000000e94c4402308db3ee1bf7a889"
                  "00000000a8ed3232e04c9bae3b75a88900000000a8ed323210e04c9bae3b75a889529e9d0f0001"
                  "000000"}})
                                    .value();
        CHECK(uncompressed.getTransaction().has_value());

        // zlib compressed packed transaction
        const std::string compressedString =
            "78dacb3d782c659f64208be036062060345879fad9aa256213401c8605cb2633322c79c8c0e8bd651e88bf"
            "e2ad9191204c80e36d735716638b77330300024516b4";

        // compressed without a compression flag cannot be read
        const auto compressedError =
            PackedTransaction::from(json{{"packed_trx", compressedString}}).value();
        CHECK_FALSE(compressedError.getTransaction().has_value());

        // with the flag it decodes
        const auto compressedSuccess =
            PackedTransaction::from(json{{"compression", 1}, {"packed_trx", compressedString}})
                .value();
        CHECK(compressedSuccess.getTransaction().has_value());
        // and packing it back up round-trips through fromSigned
        const auto tx = compressedSuccess.getTransaction().value();
        const auto signedTx = SignedTransaction::from(Serializer::objectify(tx)).value();
        const auto repacked =
            PackedTransaction::fromSigned(signedTx, CompressionType::zlib).value();
        CHECK(repacked.getTransaction().value().id().hexString() == tx.id().hexString());
    }

    TEST_CASE("fixed size array") {
        const json data = {
            {"version", "eosio::abi/1.2"},
            {"types", json::array()},
            {"structs",
             json::array(
                 {{{"name", "basic"},
                   {"base", ""},
                   {"fields", json::array({{{"name", "input"}, {"type", "int32"}}})}},
                  {{"name", "array"},
                   {"base", ""},
                   {"fields", json::array({{{"name", "input"}, {"type", "int32[]"}}})}},
                  {{"name", "fixed"},
                   {"base", ""},
                   {"fields", json::array({{{"name", "input"}, {"type", "int32[4]"}}})}}})},
            {"actions",
             json::array({{{"name", "basic"}, {"type", "basic"}, {"ricardian_contract", ""}},
                          {{"name", "array"}, {"type", "array"}, {"ricardian_contract", ""}},
                          {{"name", "fixed"}, {"type", "fixed"}, {"ricardian_contract", ""}}})},
            {"tables", json::array()},
            {"ricardian_clauses", json::array()},
            {"error_messages", json::array()},
            {"abi_extensions", json::array()},
            {"variants", json::array()},
            {"action_results",
             json::array({{{"name", "basic"}, {"result_type", "int32"}},
                          {{"name", "array"}, {"result_type", "int32[]"}},
                          {{"name", "fixed"}, {"result_type", "int32[4]"}}})},
        };

        const auto abi = ABI::from(data).value();
        CHECK(abi.structs[0].fields[0].type == "int32");
        CHECK(abi.structs[1].fields[0].type == "int32[]");
        CHECK(abi.structs[2].fields[0].type == "int32[4]");
        CHECK(abi.action_results[0].result_type == "int32");
        CHECK(abi.action_results[1].result_type == "int32[]");
        CHECK(abi.action_results[2].result_type == "int32[4]");

        const auto encoded = Serializer::encode(abi).value();
        const auto decoded = Serializer::decode<ABI>(encoded).value();
        CHECK(decoded.equals(abi));
        CHECK(decoded.structs[2].fields[0].type == "int32[4]");
        CHECK(decoded.action_results[2].result_type == "int32[4]");

        CHECK(Serializer::encode(json{{"input", 1}}, "basic", abi).value().hexString() ==
              "01000000");
        CHECK(Serializer::encode(json{{"input", {1, 2, 3, 4}}}, "array", abi).value().hexString() ==
              "0401000000020000000300000004000000");
        CHECK(Serializer::encode(json{{"input", {1, 2, 3, 4}}}, "fixed", abi).value().hexString() ==
              "01000000020000000300000004000000");
    }

    TEST_CASE("transaction signingDigest and signingData") {
        const auto transaction =
            Transaction::from(
                json{{"expiration", "1970-01-01T00:00:00"},
                     {"ref_block_num", 0},
                     {"ref_block_prefix", 0},
                     {"max_net_usage_words", 0},
                     {"max_cpu_usage_ms", 0},
                     {"delay_sec", 0},
                     {"context_free_actions", json::array()},
                     {"transaction_extensions", json::array()},
                     {"actions",
                      json::array({{{"account", "eosio.token"},
                                    {"name", "transfer"},
                                    {"authorization",
                                     json::array({{{"actor", "corecorecore"},
                                                   {"permission", "active"}}})},
                                    {"data",
                                     "a02e45ea52a42e4580b1915e5d268dcaba0100000000000004454f5300"
                                     "00000019656f73696f2d636f7265206973207468652062657374203c33"}}})}})
                .value();
        const auto chainId = Checksum256::from(
                                 "2a02a0053e5a8cf73a56ba0fda11e4d92e0238a4a2aa74fccf46d5a910746840")
                                 .value();
        const auto data = transaction.signingData(chainId);
        CHECK(data.hexString().starts_with(
            "2a02a0053e5a8cf73a56ba0fda11e4d92e0238a4a2aa74fccf46d5a910746840"));
        CHECK(data.hexString().ends_with(std::string(64, '0')));
        CHECK(transaction.signingDigest(chainId).hexString() ==
              Checksum256::hash(data).hexString());
    }

    TEST_CASE("extended asset") {
        const auto ext = ExtendedAsset::from(Asset::from("1.0000 EOS").value(), "eosio.token"_n);
        CHECK(ext.toJSON().dump() == R"({"quantity":"1.0000 EOS","contract":"eosio.token"})");
        const auto extSym = ExtendedSymbol::from("4,EOS"_symbol, "eosio.token"_n);
        CHECK(extSym.toJSON().dump() == R"({"sym":"4,EOS","contract":"eosio.token"})");
    }
}
