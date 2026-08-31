// Port of session test/tests/transact.ts (TransactPluginResourceProvider cases
// move to that plugin item)
#include <doctest/doctest.h>

#include <dwarfkit/contract.hpp>
#include <dwarfkit/plugins/transact/resource_provider.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

struct MockData {
    json action;
    json actions;
    api::v1::GetInfoResponse info;
    Session session;
    Transaction transaction;
};

MockData mockData(const std::string& memo = "wharfkit is the best <3") {
    const auto client = makeClient(DK_FIXTURE_DIR "/session/data");
    const auto info = client->v1.chain.get_info().value();
    return {Serializer::objectify(makeMockAction(memo)),
            json::array({Serializer::objectify(makeMockAction(memo))}),
            info,
            Session(mockSessionArgs(), mockSessionOptions()),
            makeMockTransaction(info, memo)};
}

void assertValidTransactResponse(const TransactResult& result) {
    CHECK(result.chain.equals(mockChainDefinition()));
    CHECK(result.resolved.has_value());
    REQUIRE(!result.signatures.empty());
    CHECK(result.signatures[0].toString().starts_with("SIG_K1_"));
    CHECK(result.signer == PermissionLevel::from(mockPermissionLevel).value());
}

std::optional<std::vector<std::shared_ptr<AbstractTransactPlugin>>> plugins(
    std::initializer_list<std::shared_ptr<AbstractTransactPlugin>> list) {
    return std::vector<std::shared_ptr<AbstractTransactPlugin>>(list);
}

}  // namespace

TEST_SUITE("transact") {
    TEST_CASE("args: action") {
        auto data = mockData();
        SUBCASE("typed") {
            const auto result = data.session.transact({.action = data.action}).value();
            assertValidTransactResponse(result);
        }
        SUBCASE("untyped") {
            const auto result =
                data.session.transact({.action = makeMockActionJson()}).value();
            assertValidTransactResponse(result);
        }
    }

    TEST_CASE("args: action from contract kit") {
        const auto client = makeClient(DK_FIXTURE_DIR "/session/data");
        SessionArgs args = mockSessionArgs();
        args.permissionLevel = PermissionLevel{"wharfkit1125"_n, "test"_n};
        Session session(args, mockSessionOptions());
        ContractKit kit({.client = client}, {.abiCache = session.abiCache});
        const auto contract = kit.load(Name::from("eosio")).value();
        const auto action =
            contract.action(Name::from("claimrewards"), json{{"owner", "teamgreymass"}}).value();
        TransactOptions options;
        options.transactPlugins =
            plugins({std::make_shared<TransactPluginResourceProvider>()});
        SUBCASE("action") {
            const auto result =
                session.transact({.action = Serializer::objectify(action)}, options).value();
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions[0].authorization[0].actor ==
                  Name::from("wharfkit1125"));
            CHECK(result.transaction->actions[0].authorization[0].permission ==
                  Name::from("test"));
        }
        SUBCASE("to append to transaction") {
            const auto info = client->v1.chain.get_info().value();
            const auto header = info.getTransactionHeader();
            Transaction transaction;
            static_cast<TransactionHeader&>(transaction) = header;
            transaction.actions = {action};
            const auto result =
                session.transact({.transaction = Serializer::objectify(transaction)}, options)
                    .value();
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions[0].authorization[0].actor ==
                  Name::from("wharfkit1125"));
            CHECK(result.transaction->actions[0].authorization[0].permission ==
                  Name::from("test"));
        }
    }

    TEST_CASE("args: actions") {
        auto data = mockData();
        const auto result = data.session.transact({.actions = data.actions}).value();
        assertValidTransactResponse(result);
    }

    TEST_CASE("args: transaction") {
        auto data = mockData();
        SUBCASE("typed") {
            const auto result =
                data.session.transact({.transaction = Serializer::objectify(data.transaction)})
                    .value();
            assertValidTransactResponse(result);
        }
        SUBCASE("retain headers") {
            const auto result =
                data.session.transact({.transaction = Serializer::objectify(data.transaction)})
                    .value();
            assertValidTransactResponse(result);
            REQUIRE(result.transaction.has_value());
            CHECK(data.transaction.delay_sec.value == result.transaction->delay_sec.value);
            CHECK(data.transaction.expiration == result.transaction->expiration);
            CHECK(data.transaction.ref_block_num == result.transaction->ref_block_num);
            CHECK(data.transaction.ref_block_prefix == result.transaction->ref_block_prefix);
            CHECK(data.transaction.max_net_usage_words.value ==
                  result.transaction->max_net_usage_words.value);
            CHECK(data.transaction.max_cpu_usage_ms == result.transaction->max_cpu_usage_ms);
        }
    }

    TEST_CASE("args: signing request") {
        auto data = mockData();
        SUBCASE("string") {
            const auto result =
                data.session
                    .transact({.request =
                                   std::variant<SigningRequest, std::string>(std::string(
                                       "esr:gmNgZGBY1mTC_MoglIGBIVzX5uxZRqAQGDBBaSOYQMPGiXGxar2n"
                                       "tKB8Flf_YBAt6BocpBCQWJmTn5hSrOAWEq7IzMAAAA"))})
                    .value();
            assertValidTransactResponse(result);
        }
        SUBCASE("string maintains payload metadata") {
            const auto result =
                data.session
                    .transact({.request =
                                   std::variant<SigningRequest, std::string>(std::string(
                                       "esr://gmNgZGBY1mTC_MoglIGBIVzX5uxZRgEnjpsHS30fM4DAhI2nLG"
                                       "ACDRsnxsWq9Z6yZAVLMbC4-geDaPHyjMSitOzMEoXMYoWSjFSFpNTi"
                                       "EgUbY0YGRua0_HzmpMQiAA"))},
                              {.broadcast = false, .transactPlugins = plugins({})})
                    .value();
            CHECK(result.request.getInfoKey("foo") == "bar");
        }
        SUBCASE("object") {
            const auto request =
                SigningRequest::from(
                    "esr:gmNgZGBY1mTC_MoglIGBIVzX5uxZRqAQGDBBaSOYQMPGiXGxar2ntKB8Flf_YBAt6Bocp"
                    "BCQWJmTn5hSrOAWEq7IzMAAAA",
                    {.zlib = true})
                    .value();
            const auto result =
                data.session.transact({.request = std::variant<SigningRequest, std::string>(request)})
                    .value();
            assertValidTransactResponse(result);
        }
        SUBCASE("object maintains payload metadata") {
            const auto cache = std::make_shared<ABICache>(
                makeClient(DK_FIXTURE_DIR "/session/data"));
            SigningRequestCreateArguments createArgs;
            createArgs.action = data.action;
            auto request = SigningRequest::create(
                               createArgs, {.zlib = true, .abiProvider = cache.get()})
                               .value();
            request.setInfoKey("foo", std::string_view("bar"));
            CHECK(request.getInfoKey("foo") == "bar");
            const auto result =
                data.session
                    .transact({.request = std::variant<SigningRequest, std::string>(request)},
                              {.broadcast = false, .transactPlugins = plugins({})})
                    .value();
            CHECK(result.request.getInfoKey("foo") == "bar");
        }
    }

    TEST_CASE("args: invalid, no abi for contract") {
        auto data = mockData();
        json action = data.action;
        action["account"] = "";
        action["data"] = makeMockActionJson()["data"];  // object data forces an abi fetch
        const auto result = data.session.transact({.action = action});
        REQUIRE_FALSE(result.has_value());
        // the recorded jungle4 response for an empty account name
        CHECK(result.error().message.find("unable to retrieve account abi") != std::string::npos);
    }

    TEST_CASE("options: abis passed as option") {
        auto data = mockData();
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
        TransactOptions options;
        options.abis = {{Name::from("eosio.token"), ABI::from(abiJson).value()}};
        const auto result = data.session.transact({.action = data.action}, options).value();
        assertValidTransactResponse(result);
    }

    TEST_CASE("options: allowModify") {
        auto data = mockData();
        SUBCASE("true") {
            const auto result =
                data.session
                    .transact({.action = data.action},
                              {.allowModify = true,
                               .transactPlugins = plugins(
                                   {std::make_shared<MockTransactResourceProviderPlugin>()})})
                    .value();
            assertValidTransactResponse(result);
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions.size() == 2);
        }
        SUBCASE("false") {
            const auto result =
                data.session
                    .transact({.action = data.action},
                              {.allowModify = false,
                               .transactPlugins = plugins(
                                   {std::make_shared<MockTransactResourceProviderPlugin>()})})
                    .value();
            assertValidTransactResponse(result);
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions.size() == 1);
        }
    }

    TEST_CASE("options: broadcast") {
        SUBCASE("default: true") {
            Session session(mockSessionArgs(), {.fetch = makeMockFetch()});
            const auto result =
                session
                    .transact({.action = Serializer::objectify(
                                   makeMockAction("transact broadcast default"))})
                    .value();
            CHECK(result.response.has_value());
            assertValidTransactResponse(result);
        }
        SUBCASE("true") {
            auto data = mockData();
            const auto result =
                data.session
                    .transact({.action = Serializer::objectify(
                                   makeMockAction("transact broadcast true"))},
                              {.broadcast = true})
                    .value();
            CHECK(result.response.has_value());
            assertValidTransactResponse(result);
        }
        SUBCASE("false") {
            auto data = mockData();
            const auto result =
                data.session.transact({.action = data.action}, {.broadcast = false}).value();
            CHECK_FALSE(result.response.has_value());
            assertValidTransactResponse(result);
        }
    }

    TEST_CASE("options: expireSeconds override 60") {
        auto data = mockData();
        const auto result =
            data.session
                .transact({.action = data.action}, {.broadcast = false, .expireSeconds = 60})
                .value();
        const auto info = data.session.client()->v1.chain.get_info().value();
        const auto expected = TimePointSec::fromMilliseconds(
            static_cast<double>(info.head_block_time.toMilliseconds()) + 60 * 1000);
        REQUIRE(result.transaction.has_value());
        CHECK(result.transaction->expiration.toString() == expected.toString());
    }

    TEST_CASE("options: transactPlugins") {
        SUBCASE("inherit") {
            auto data = mockData();
            auto options = mockSessionOptions();
            options.transactPlugins = {
                {std::make_shared<MockTransactResourceProviderPlugin>()}};
            Session session(mockSessionArgs(), options);
            const auto result = session.transact({.action = data.action}).value();
            assertValidTransactResponse(result);
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions.size() == 2);
        }
        SUBCASE("override") {
            auto data = mockData();
            const auto result =
                data.session
                    .transact({.action = data.action},
                              {.transactPlugins = plugins(
                                   {std::make_shared<MockTransactResourceProviderPlugin>()})})
                    .value();
            assertValidTransactResponse(result);
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions.size() == 2);
        }
    }

    TEST_CASE("options: transactPluginsOptions") {
        SUBCASE("transact") {
            auto data = mockData();
            auto options = mockSessionOptions();
            options.transactPlugins = {
                {std::make_shared<MockTransactResourceProviderPlugin>()}};
            Session session(mockSessionArgs(), options);
            const auto result =
                session
                    .transact({.action = data.action},
                              {.transactPluginsOptions = json{{"disableExamplePlugin", true}}})
                    .value();
            assertValidTransactResponse(result);
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions.size() == 1);
        }
        SUBCASE("session constructor") {
            auto data = mockData();
            auto options = mockSessionOptions();
            options.transactPlugins = {
                {std::make_shared<MockTransactResourceProviderPlugin>()}};
            options.transactPluginsOptions = json{{"disableExamplePlugin", true}};
            Session session(mockSessionArgs(), options);
            const auto result = session.transact({.action = data.action}).value();
            assertValidTransactResponse(result);
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions.size() == 1);
        }
        SUBCASE("kit constructor") {
            auto data = mockData();
            auto kitOptions = mockSessionKitOptions();
            kitOptions.transactPlugins = {
                {std::make_shared<MockTransactResourceProviderPlugin>()}};
            kitOptions.transactPluginsOptions = json{{"disableExamplePlugin", true}};
            SessionKit kit(mockSessionKitArgs(), kitOptions);
            LoginOptions loginOptions;
            loginOptions.chain = Checksum256::from(std::string_view(mockChainId)).value();
            loginOptions.permissionLevel = PermissionLevel::from(mockPermissionLevel).value();
            const auto login = kit.login(loginOptions).value();
            const auto result = login.session->transact({.action = data.action}).value();
            assertValidTransactResponse(result);
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions.size() == 1);
        }
        SUBCASE("login") {
            auto kitOptions = mockSessionKitOptions();
            kitOptions.transactPlugins = {
                {std::make_shared<MockTransactResourceProviderPlugin>()}};
            SessionKit kit(mockSessionKitArgs(), kitOptions);
            LoginOptions loginOptions;
            loginOptions.chain = Checksum256::from(std::string_view(mockChainId)).value();
            loginOptions.permissionLevel = PermissionLevel::from(mockPermissionLevel).value();
            loginOptions.transactPluginsOptions = json{{"disableExamplePlugin", true}};
            const auto login = kit.login(loginOptions).value();
            const auto result =
                login.session
                    ->transact({.action = Serializer::objectify(
                                    makeMockAction("testing after login"))})
                    .value();
            assertValidTransactResponse(result);
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->actions.size() == 1);
        }
    }

    TEST_CASE("plugins: trigger") {
        auto data = mockData();
        const auto result =
            data.session
                .transact({.action = data.action},
                          {.transactPlugins = plugins({std::make_shared<MockTransactPlugin>()})})
                .value();
        assertValidTransactResponse(result);
    }

    TEST_CASE("plugins: multiple modifications") {
        auto data = mockData();
        const auto result =
            data.session
                .transact({.action = data.action},
                          {.transactPlugins = plugins(
                               {std::make_shared<MockTransactActionPrependerPlugin>(),
                                std::make_shared<MockTransactActionPrependerPlugin>()})})
                .value();
        assertValidTransactResponse(result);
        REQUIRE(result.transaction.has_value());
        REQUIRE(result.transaction->actions.size() == 3);
        CHECK(result.transaction->actions[0].account == Name::from("greymassnoop"));
        CHECK(result.transaction->actions[1].account == Name::from("greymassnoop"));
        CHECK(result.transaction->actions[2].account == Name::from("eosio.token"));
        // the two prepended authorizations are random and differ
        CHECK_FALSE(result.transaction->actions[0].authorization[0].actor ==
                    result.transaction->actions[1].authorization[0].actor);
    }

    TEST_CASE("plugins: metadata persists through mutation") {
        auto data = mockData();
        const auto result =
            data.session
                .transact(
                    {.request = std::variant<SigningRequest, std::string>(std::string(
                         "esr://gmNgZGBY1mTC_MoglIGBIVzX5uxZRgEnjpsHS30fM4DAhI2nLGACDRsnxsWq9Z6y"
                         "ZAVLMbC4-geDaPHyjMSitOzMEoXMYoWSjFSFpNTiEgUbY0YGRua0_HzmpMQiAA"))},
                    {.broadcast = false,
                     .transactPlugins =
                         plugins({std::make_shared<MockTransactActionPrependerPlugin>()})})
                .value();
        CHECK(result.request.getInfoKey("foo") == "bar");
    }

    TEST_CASE("plugins: metadata preservation from original") {
        auto data = mockData();
        const auto result =
            data.session
                .transact(
                    {.request = std::variant<SigningRequest, std::string>(std::string(
                         "esr://gmNgZGBY1mTC_MoglIGBIVzX5uxZRgEnjpsHS30fM4DAhI2nLGACDRsnxsWq9Z6y"
                         "ZAVLMbC4-geDaPHyjMSitOzMEoXMYoWSjFSFpNTiEgUbY0YGRua0_HzmpMQiAA"))},
                    {.broadcast = false,
                     .transactPlugins =
                         plugins({std::make_shared<MockMetadataFooWriterPlugin>()})})
                .value();
        CHECK(result.request.getInfoKey("foo") == "bar");
    }

    TEST_CASE("response: decoded transaction") {
        auto data = mockData();
        const auto request =
            SigningRequest::from(
                "esr:gmNgZGBY1mTC_MoglIGBIVzX5uxZRqAQGDBBaSOYQMPGiXGxar2ntKB8Flf_YBAt6BocpBCQWJ"
                "mTn5hSrOAWEq7IzMAAAA",
                {.zlib = true})
                .value();
        const auto result =
            data.session.transact({.request = std::variant<SigningRequest, std::string>(request)})
                .value();
        REQUIRE(result.transaction.has_value());
        const auto& resolvedPermission = result.transaction->actions[0].authorization[0];
        const auto expectedPermission = PermissionLevel::from(mockPermissionLevel).value();
        CHECK(resolvedPermission.actor == expectedPermission.actor);
        CHECK(resolvedPermission.permission == expectedPermission.permission);
        // transaction data was templated
        CHECK(result.transaction->actions[0].data["from"] ==
              expectedPermission.actor.toString());
    }

    TEST_CASE("response: resolved request") {
        auto data = mockData();
        const auto request =
            SigningRequest::from(
                "esr:gmNgZGBY1mTC_MoglIGBIVzX5uxZRqAQGDBBaSOYQMPGiXGxar2ntKB8Flf_YBAt6BocpBCQWJ"
                "mTn5hSrOAWEq7IzMAAAA",
                {.zlib = true})
                .value();
        const auto result =
            data.session.transact({.request = std::variant<SigningRequest, std::string>(request)})
                .value();
        REQUIRE(result.resolved.has_value());
        const auto& resolvedPermission = result.resolved->transaction.actions[0].authorization[0];
        const auto expectedPermission = PermissionLevel::from(mockPermissionLevel).value();
        CHECK(resolvedPermission.actor == expectedPermission.actor);
        CHECK(resolvedPermission.permission == expectedPermission.permission);
    }

    TEST_CASE("response: valid signatures") {
        auto data = mockData();
        const auto result = data.session.transact({.action = data.action}).value();
        REQUIRE(result.resolved.has_value());
        const auto digest =
            result.resolved->transaction.signingDigest(mockChainDefinition().id);
        REQUIRE(!result.signatures.empty());
        const auto publicKey = result.signatures[0].recoverDigest(digest).value();
        const auto expected =
            PrivateKey::from(mockPrivateKey).value().toPublic().value();
        CHECK(publicKey.toString() == expected.toString());
    }

    TEST_CASE("response: return values") {
        auto data = mockData();
        const json action = {{"account", "todoapp12345"},
                             {"name", "add"},
                             {"authorization",
                              json::array({{{"actor", mockAccountName},
                                            {"permission", mockPermissionName}}})},
                             {"data",
                              {{"author", mockAccountName}, {"description", "mock test"}}}};
        const auto result =
            data.session.transact({.action = action}, {.broadcast = true}).value();
        REQUIRE(result.returns.size() == 1);
        const auto& returned = result.returns[0];
        CHECK(returned.contract == Name::from("todoapp12345"));
        CHECK(returned.action == Name::from("add"));
        CHECK(returned.data["author"] == mockAccountName);
    }

    TEST_CASE("context_free_actions") {
        const json cfa = {{"account", "greymassfuel"},
                          {"name", "noop"},
                          {"authorization", json::array()},
                          {"data", ""}};
        SUBCASE("transact w/ action") {
            auto data = mockData();
            const auto result = data.session
                                    .transact({.action = data.action,
                                               .context_free_actions = json::array({cfa})},
                                              {.broadcast = true})
                                    .value();
            REQUIRE(result.resolved.has_value());
            CHECK(result.resolved->transaction.context_free_actions.size() == 1);
        }
        SUBCASE("transact w/ actions") {
            auto data = mockData();
            const auto result = data.session
                                    .transact({.actions = data.actions,
                                               .context_free_actions = json::array({cfa})},
                                              {.broadcast = true})
                                    .value();
            REQUIRE(result.resolved.has_value());
            CHECK(result.resolved->transaction.context_free_actions.size() == 1);
        }
    }
}
