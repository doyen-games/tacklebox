// Port of session test/tests/session.ts (nodejs use case folded in)
#include <doctest/doctest.h>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

TransactOptions mockTransactOptions() {
    TransactOptions options;
    options.transactPlugins = {{std::make_shared<MockTransactResourceProviderPlugin>()}};
    return options;
}

json typedActionJson() { return Serializer::objectify(makeMockAction()); }

}  // namespace

TEST_SUITE("session") {
    TEST_CASE("nodejs usage: session") {
        Session session(
            {.chain = mockChainDefinition(),
             .permissionLevel = PermissionLevel::from(mockPermissionLevel).value(),
             .walletPlugin = makeWallet()},
            {.fetch = makeMockFetch()});
        const auto response = session.transact({.action = makeMockActionJson()}).value();
        REQUIRE(!response.signatures.empty());
        CHECK(response.signatures[0].toString().starts_with("SIG_K1_"));
    }

    TEST_CASE("construct with specified abiCache") {
        auto sessionArgs = mockSessionArgs();
        auto options = mockSessionOptions();
        const auto cache = std::make_shared<ABICache>(
            makeClient(DK_FIXTURE_DIR "/session/data"));
        options.abiCache = cache;
        Session session(sessionArgs, options);
        CHECK(session.abiCache == cache);
    }

    TEST_CASE("construct with abis for entire session") {
        auto options = mockSessionOptions();
        const json abiJson = {{"version", "eosio::abi/1.2"},
                              {"structs",
                               json::array({{{"name", "transfer"},
                                             {"base", ""},
                                             {"fields",
                                              json::array({{{"name", "from"}, {"type", "name"}},
                                                           {{"name", "to"}, {"type", "name"}},
                                                           {{"name", "quantity"}, {"type", "asset"}},
                                                           {{"name", "memo"}, {"type", "string"}}})}}})},
                              {"actions",
                               json::array({{{"name", "transfer"},
                                             {"type", "transfer"},
                                             {"ricardian_contract", ""}}})}};
        options.abis = {{Name::from("eosio.token"), ABI::from(abiJson).value()}};
        Session session(mockSessionArgs(), options);
        CHECK(session.abis.size() == 1);
    }

    TEST_CASE("allowModify") {
        SUBCASE("default: true") {
            Session session(mockSessionArgs(), mockSessionOptions());
            const auto result =
                session.transact({.action = typedActionJson()}, mockTransactOptions()).value();
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions.size() == 2);
        }
        SUBCASE("true") {
            auto options = mockSessionOptions();
            options.allowModify = true;
            Session session(mockSessionArgs(), options);
            const auto result =
                session.transact({.action = typedActionJson()}, mockTransactOptions()).value();
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions.size() == 2);
        }
        SUBCASE("false") {
            auto options = mockSessionOptions();
            options.allowModify = false;
            Session session(mockSessionArgs(), options);
            const auto result =
                session.transact({.action = typedActionJson()}, mockTransactOptions()).value();
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions.size() == 1);
        }
    }

    TEST_CASE("broadcast") {
        SUBCASE("default: true") {
            Session session(mockSessionArgs(), {.fetch = makeMockFetch()});
            const auto result =
                session
                    .transact({.action =
                                   Serializer::objectify(makeMockAction("test broadcast default"))})
                    .value();
            CHECK(result.response.has_value());
        }
        SUBCASE("true") {
            auto options = mockSessionOptions();
            options.broadcast = true;
            Session session(mockSessionArgs(), options);
            const auto result =
                session
                    .transact(
                        {.action = Serializer::objectify(makeMockAction("test broadcast true"))},
                        {.broadcast = true})
                    .value();
            CHECK(result.response.has_value());
        }
        SUBCASE("false") {
            auto options = mockSessionOptions();
            options.broadcast = false;
            Session session(mockSessionArgs(), options);
            const auto result =
                session.transact({.action = typedActionJson()}, {.broadcast = false}).value();
            CHECK_FALSE(result.response.has_value());
        }
    }

    TEST_CASE("expireSeconds override: 60") {
        auto options = mockSessionOptions();
        options.expireSeconds = 60;
        Session session(mockSessionArgs(), options);
        const auto result =
            session.transact({.action = typedActionJson()}, {.broadcast = false}).value();
        const auto info = session.client()->v1.chain.get_info().value();
        const auto expectedExpiration =
            TimePointSec::fromMilliseconds(
                static_cast<double>(info.head_block_time.toMilliseconds()) + 60 * 1000);
        REQUIRE(result.transaction.has_value());
        CHECK(result.transaction->expiration.toString() == expectedExpiration.toString());
    }

    TEST_CASE("getters") {
        Session session(mockSessionArgs(), mockSessionOptions());
        const auto expected = PermissionLevel::from(mockPermissionLevel).value();
        CHECK(session.actor() == expected.actor);
        CHECK(session.permission() == expected.permission);
    }

    TEST_CASE("ui override") {
        auto options = mockSessionOptions();
        const auto ui = std::make_shared<MockUserInterface>();
        options.ui = ui;
        Session session(mockSessionArgs(), options);
        CHECK(session.ui == ui);
    }

    TEST_CASE("serialize returns valid json string") {
        Session session(mockSessionArgs(), mockSessionOptions());
        const auto serialized = session.serialize();
        CHECK(serialized.toJSON().dump() ==
              R"({"chain":"73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d",)"
              R"("actor":"wharfkit1111","permission":"test","walletPlugin":)"
              R"({"id":"wallet-plugin-privatekey","data":{"privateKey":)"
              R"("PVT_K1_25XP1Lt1Rt87hyymouSieBbgnUEAerS1yQHi9wqHC2Uek2mgzH"}}})");
        // data field is not present when empty
        CHECK_FALSE(serialized.toJSON().contains("data"));
    }

    TEST_CASE("serializes with custom data field") {
        Session session(mockSessionArgs(), mockSessionOptions());
        json data = session.data();
        data["randomField"] = "randomData";
        data["testNumber"] = 123;
        data["testBoolean"] = true;
        data["testObject"] = {{"key", "value"}};
        session.setData(data);
        const auto serialized = session.serialize();
        REQUIRE(serialized.toJSON().contains("data"));
        CHECK(serialized.data["randomField"] == "randomData");
        CHECK(serialized.data["testNumber"] == 123);
        CHECK(serialized.data["testBoolean"] == true);
        CHECK(nlohmann::json(serialized.data["testObject"]) ==
              nlohmann::json(json{{"key", "value"}}));
    }

    TEST_CASE("able to sign transaction") {
        Session session(
            {.chain = mockChainDefinition(),
             .permissionLevel = PermissionLevel::from("account@permission").value(),
             .walletPlugin = makeWallet()},
            {.fetch = makeMockFetch()});
        // a fully formed transaction (an eosio.token:transfer with a renamed
        // contract/action to break unittest caching)
        const json transactionJson = {
            {"expiration", "2022-12-07T22:39:44"},
            {"ref_block_num", 2035},
            {"ref_block_prefix", 2373626664},
            {"max_net_usage_words", 0},
            {"max_cpu_usage_ms", 0},
            {"delay_sec", 0},
            {"context_free_actions", json::array()},
            {"actions",
             json::array({{{"account", "foo"},
                           {"name", "bar"},
                           {"authorization", json::array({{{"actor", "wharfkit1111"},
                                                           {"permission", "test"}}})},
                           {"data",
                            "104208d9c1754de380b1915e5d268dca390500000000000004454f5300000000177768"
                            "6172666b6974206973207468652062657374203c33"}}})},
            {"transaction_extensions", json::array()}};
        const auto transaction = structFrom<Transaction>(transactionJson).value();
        const auto signatures = session.signTransaction(transaction).value();
        REQUIRE(signatures.size() == 1);
        CHECK(signatures[0].toString().starts_with("SIG_K1_"));
    }

    TEST_CASE("able to change api endpoint") {
        Session session(
            {.chain = mockChainDefinition(),
             .permissionLevel = PermissionLevel::from("account@permission").value(),
             .walletPlugin = makeWallet()},
            {.fetch = makeMockFetch()});
        CHECK(session.chain.url == "https://jungle4.greymass.com");
        session.setEndpoint("https://wax.greymass.com");
        CHECK(session.chain.url == "https://wax.greymass.com");
    }
}
