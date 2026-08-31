// Port of contract test/tests/{kit,contract,utils}.ts. Rows are json in
// dwarfkit, so typed-row assertions become field checks.
#include <doctest/doctest.h>

#include <dwarfkit/contract.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

std::shared_ptr<APIClient> jungleClient() {
    return makeClient(DK_FIXTURE_DIR "/contract/data", "https://jungle4.greymass.com");
}
std::shared_ptr<APIClient> eosClient() {
    return makeClient(DK_FIXTURE_DIR "/contract/data", "https://eos.greymass.com");
}

json placeholderAuthJson() {
    return json::array({{{"actor", "............1"}, {"permission", "............2"}}});
}

json transferData() {
    return json{{"from", "............1"},
                {"to", "foo"},
                {"quantity", "1.0000 EOS"},
                {"memo", "initial balance"}};
}

}  // namespace

TEST_SUITE("contract") {
    TEST_CASE("kit construct and load") {
        ContractKit kit({.client = jungleClient()});
        SUBCASE("defaults") { CHECK(kit.abiCache != nullptr); }
        SUBCASE("options: abiCache") {
            const auto cache = std::make_shared<ABICache>(jungleClient());
            ContractKit custom({.client = jungleClient()}, {.abiCache = cache});
            CHECK(custom.abiCache == cache);
        }
        SUBCASE("options: debug") {
            ContractKit debug({.client = jungleClient()}, {.debug = true});
            const auto contract = debug.load(Name::from("eosio.token")).value();
            CHECK(contract.debug);
            const auto table = contract.table(Name::from("accounts")).value();
            CHECK(table.debug);
        }
        SUBCASE("options: abis") {
            ABI abi;
            abi.version = "eosio::abi/1.2";
            ContractKit preset({.client = jungleClient()},
                               {.abis = {{Name::from("foo"), abi}, {Name::from("bar"), abi}}});
            CHECK(preset.abiCache->cacheSize() == 2);
            const auto foo = preset.load(Name::from("foo")).value();
            CHECK(foo.abi.version == "eosio::abi/1.2");
        }
    }

    TEST_CASE("system contract surfaces") {
        ContractKit kit({.client = jungleClient()});
        const auto systemContract = kit.load(Name::from("eosio")).value();
        SUBCASE("tableNames") {
            CHECK(systemContract.tableNames().size() == 26);
            const auto names = systemContract.tableNames();
            CHECK(std::find(names.begin(), names.end(), "voters") != names.end());
        }
        SUBCASE("actionNames") {
            CHECK(systemContract.actionNames().size() == 62);
            const auto names = systemContract.actionNames();
            CHECK(std::find(names.begin(), names.end(), "newaccount") != names.end());
        }
        SUBCASE("table accepts scope") {
            const auto table =
                systemContract.table(Name::from("delband"), "teamgreymass").value();
            CHECK(table.defaultScope == "teamgreymass");
            const auto rows = table.all().value();
            REQUIRE(rows.size() == 1);
            const auto row = table.get().value();
            REQUIRE(row.has_value());
            CHECK((*row)["from"] == "teamgreymass");
            CHECK((*row)["to"] == "teamgreymass");
            CHECK((*row)["net_weight"] == "1.0000 EOS");
            CHECK((*row)["cpu_weight"] == "1.0000 EOS");
            const auto cursor = table.query().value();
            CHECK(cursor.params["scope"] == "teamgreymass");
        }
        SUBCASE("actions with placeholder authorization") {
            const auto mockPublicKey =
                PrivateKey::from(mockPrivateKey).value().toPublic().value().toString();
            const json authority = {
                {"accounts", json::array()},
                {"keys", json::array({{{"key", mockPublicKey}, {"weight", 1}}})},
                {"threshold", 1},
                {"waits", json::array()}};
            const std::vector<ActionsArgs> args = {
                {Name::from("newaccount"),
                 json{{"creator", "............1"},
                      {"name", "foo"},
                      {"owner", authority},
                      {"active", authority}},
                 {}},
                {Name::from("buyrambytes"),
                 json{{"payer", "............1"}, {"receiver", "foo"}, {"bytes", 8192}},
                 {}},
                {Name::from("delegatebw"),
                 json{{"from", "............1"},
                      {"receiver", "foo"},
                      {"stake_net_quantity", "1.0000 EOS"},
                      {"stake_cpu_quantity", "1.0000 EOS"},
                      {"transfer", false}},
                 {}}};
            const auto actions = systemContract.actions(args).value();
            REQUIRE(actions.size() == 3);
            for (const auto& action : actions) {
                CHECK(action.account == Name::from("eosio"));
                REQUIRE(action.authorization.size() == 1);
                CHECK(action.authorization[0] == PlaceholderAuth);
            }
            // authorization override for all
            ActionOptions options;
            options.authorization = {PermissionLevel{"foo"_n, "bar"_n}};
            const auto overridden = systemContract.actions(args, options).value();
            for (const auto& action : overridden) {
                CHECK(action.authorization[0].actor == Name::from("foo"));
                CHECK(action.authorization[0].permission == Name::from("bar"));
            }
            // individual override wins
            std::vector<ActionsArgs> mixed = args;
            mixed[1].authorization = {PermissionLevel{"foo"_n, "bar"_n}};
            const auto individual = systemContract.actions(mixed).value();
            CHECK(individual[0].authorization[0] == PlaceholderAuth);
            CHECK(individual[1].authorization[0].actor == Name::from("foo"));
            CHECK(individual[2].authorization[0] == PlaceholderAuth);
        }
    }

    TEST_CASE("token contract action") {
        ContractKit kit({.client = jungleClient()});
        const auto tokenContract = kit.load(Name::from("eosio.token")).value();
        SUBCASE("builds the transfer action") {
            const auto action =
                tokenContract.action(Name::from("transfer"), transferData()).value();
            CHECK(action.account == Name::from("eosio.token"));
            CHECK(action.name == Name::from("transfer"));
            CHECK(action.authorization[0] == PlaceholderAuth);
            const auto encoded =
                Serializer::encode(transferData(), "transfer", tokenContract.abi).value();
            CHECK(action.data == encoded);
        }
        SUBCASE("errors with incomplete action data") {
            const auto action = tokenContract.action(
                Name::from("transfer"),
                json{{"from", "foo"}, {"to", "bar"}, {"quantity", "1.0000 EOS"}});
            CHECK_FALSE(action.has_value());
        }
        SUBCASE("errors with invalid name") {
            const auto action = tokenContract.action(Name::from("foo"), transferData());
            REQUIRE_FALSE(action.has_value());
            CHECK(action.error().message ==
                  "Contract (eosio.token) does not have an action named (foo)");
        }
        SUBCASE("authorization override") {
            ActionOptions options;
            options.authorization = {PermissionLevel{"foo"_n, "bar"_n}};
            const auto action =
                tokenContract.action(Name::from("transfer"), transferData(), options).value();
            CHECK(action.authorization[0].actor == Name::from("foo"));
            CHECK(action.authorization[0].permission == Name::from("bar"));
        }
        SUBCASE("ricardian errors") {
            CHECK_FALSE(tokenContract.ricardian(Name::from("foo")).has_value());
            CHECK_FALSE(tokenContract.ricardian(Name::from("transfer")).has_value());
        }
    }

    TEST_CASE("ricardian returns the contract text") {
        ContractKit kit({.client = eosClient()});
        const auto contract = kit.load(Name::from("eosio.token")).value();
        const auto ricardian = contract.ricardian(Name::from("transfer")).value();
        CHECK(!ricardian.empty());
    }

    TEST_CASE("readonly transactions") {
        ContractKit kit({.client = jungleClient()});
        SUBCASE("basic return") {
            const auto contract = kit.load(Name::from("abcabcabc333")).value();
            const auto result =
                contract.readonly(Name::from("returnvalue"), json{{"message", "hello"}}).value();
            CHECK(result == "Validation has passed.");
        }
        SUBCASE("dynamic encoded return") {
            const auto contract = kit.load(Name::from("testing.gm")).value();
            const auto result = contract.readonly(Name::from("callapi")).value();
            CHECK(result.contains("foo"));
        }
    }

    TEST_CASE("readonly exception formatting") {
        SUBCASE("substituted assertion message") {
            const json except = {
                {"code", 3050003},
                {"name", "eosio_assert_message_exception"},
                {"message", "eosio_assert_message assertion failure"},
                {"stack",
                 json::array({{{"context", {{"level", "error"}}},
                               {"format", "assertion failure with message: ${s}"},
                               {"data", {{"s", "insufficient balance"}}}}})}};
            CHECK(formatExceptionMessage(except) ==
                  "assertion failure with message: insufficient balance");
        }
        SUBCASE("falls back to the exception message with an empty stack") {
            const json except = {{"code", 3080004},
                                 {"name", "tx_cpu_usage_exceeded"},
                                 {"message", "transaction exceeded the current CPU usage limit"},
                                 {"stack", json::array()}};
            CHECK(formatExceptionMessage(except) ==
                  "transaction exceeded the current CPU usage limit");
        }
    }

    TEST_CASE("utils") {
        CHECK(pascalCase("hello_world") == "HelloWorld");
        CHECK(capitalize("hello") == "Hello");
        CHECK(capitalize("") == "");
        CHECK(singularize("bodies") == "body");
        CHECK(singularize("watches") == "watch");
        CHECK(singularize("buses") == "bus");
        CHECK(singularize("cats") == "cat");
        CHECK(indexPositionInWords(0) == "primary");
        CHECK(indexPositionInWords(1) == "secondary");
        // wrapIndexValue
        CHECK(wrapIndexValue(json()).is_null());
        CHECK(wrapIndexValue(json(10)) == 10);
        CHECK(wrapIndexValue(json("name")) == "name");
        // wrapScopeValue
        CHECK(wrapScopeValue(json("teamgreymass")).value() == "teamgreymass");
        CHECK(wrapScopeValue(json("0")).value() == "0");
        CHECK(wrapScopeValue(json("18446744073709551615")).value() == "18446744073709551615");
        CHECK(wrapScopeValue(json(0)).value() == 0);
        CHECK(wrapScopeValue(json(10)).value() == 10);
        CHECK(wrapScopeValue(json(uint64_t(18446744073709551615ull))).value() ==
              "18446744073709551615");
        CHECK(wrapScopeValue(json(1.5)).error().message.find(
                  "is not an integer a number can hold") != std::string::npos);
        CHECK(wrapScopeValue(json(-1)).error().message.find("underflows uint64") !=
              std::string::npos);
        CHECK(wrapScopeValue(json()).error().message == "Scope is required");
        // isAbsentScope
        CHECK(isAbsentScope(json()));
        CHECK(isAbsentScope(json("")));
        CHECK_FALSE(isAbsentScope(json(0)));
        CHECK_FALSE(isAbsentScope(json("0")));
    }

    TEST_CASE("abi blob round trip") {
        ContractKit kit({.client = jungleClient()});
        const auto contract = kit.load(Name::from("eosio.token")).value();
        const Blob blob = abiToBlob(contract.abi);
        const auto restored = blobStringToAbi(blob.toString()).value();
        CHECK(restored.version == contract.abi.version);
        CHECK(restored.tables.size() == contract.abi.tables.size());
    }
}
