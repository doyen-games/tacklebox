// Port of session test/tests/kit.ts
#include <doctest/doctest.h>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

LoginOptions defaultLoginOptions() {
    LoginOptions options;
    options.chain = Checksum256::from(std::string_view(mockChainId)).value();
    options.permissionLevel = PermissionLevel::from(mockPermissionLevel).value();
    return options;
}

void assertSessionMatchesMockSession(const std::shared_ptr<Session>& session) {
    REQUIRE(session);
    CHECK(session->appName == "unittest");
    CHECK(session->allowModify == true);
    CHECK(session->broadcast == true);
    CHECK(session->expireSeconds == 120);
    CHECK(session->chain.equals(mockChainDefinitions()[0]));
    CHECK(session->walletPlugin->id() == "wallet-plugin-privatekey");
}

Checksum256 sum(const char* hex) { return Checksum256::from(std::string_view(hex)).value(); }

}  // namespace

TEST_SUITE("kit") {
    TEST_CASE("chains definitions from the common module") {
        auto args = mockSessionKitArgs();
        args.chains = {Chains::Jungle4(), Chains::EOS()};
        SessionKit kit(args, mockSessionKitOptions());
        LoginOptions options;
        options.chain = Chains::EOS().id;
        options.permissionLevel = PermissionLevel::from(mockPermissionLevel).value();
        const auto result = kit.login(options).value();
        CHECK(result.response.chain == Chains::EOS().id);
    }

    TEST_CASE("abis passing for all sessions") {
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
        auto options = mockSessionKitOptions();
        options.abis = {{Name::from("eosio.token"), ABI::from(abiJson).value()}};
        SessionKit kit(mockSessionKitArgs(), options);
        CHECK(kit.abis.size() == 1);
        const auto result = kit.login(defaultLoginOptions()).value();
        CHECK(result.session->abis.size() == 1);
    }

    TEST_CASE("expireSeconds") {
        SUBCASE("default: 120") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            const auto login = kit.login(defaultLoginOptions()).value();
            const auto result =
                login.session
                    ->transact({.action = Serializer::objectify(makeMockAction())},
                               {.broadcast = false})
                    .value();
            const auto info = login.session->client()->v1.chain.get_info().value();
            const auto expected = TimePointSec::fromMilliseconds(
                static_cast<double>(info.head_block_time.toMilliseconds()) + 120 * 1000);
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->expiration.toString() == expected.toString());
        }
        SUBCASE("override: 60") {
            auto options = mockSessionKitOptions();
            options.expireSeconds = 60;
            SessionKit kit(mockSessionKitArgs(), options);
            const auto login = kit.login(defaultLoginOptions()).value();
            const auto result =
                login.session
                    ->transact({.action = Serializer::objectify(makeMockAction())},
                               {.broadcast = false})
                    .value();
            const auto info = login.session->client()->v1.chain.get_info().value();
            const auto expected = TimePointSec::fromMilliseconds(
                static_cast<double>(info.head_block_time.toMilliseconds()) + 60 * 1000);
            REQUIRE(result.transaction.has_value());
            CHECK(result.transaction->expiration.toString() == expected.toString());
        }
    }

    TEST_CASE("transactPlugins") {
        SUBCASE("default") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            REQUIRE(kit.transactPlugins.size() == 1);
            CHECK(kit.transactPlugins[0]->id() == "base-transact-plugin");
        }
        SUBCASE("override") {
            auto options = mockSessionKitOptions();
            options.transactPlugins = {{std::make_shared<MockTransactPlugin>()}};
            SessionKit kit(mockSessionKitArgs(), options);
            REQUIRE(kit.transactPlugins.size() == 1);
            CHECK(kit.transactPlugins[0]->id() == "mock-transact-plugin");
        }
    }

    TEST_CASE("login") {
        SUBCASE("default") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            const auto result = kit.login().value();
            // MockUserInterface answers with the first chain and mock@interface
            CHECK(result.session->chain.equals(mockChainDefinitions()[0]));
            CHECK(result.session->walletPlugin->id() == "wallet-plugin-privatekey");
        }
        SUBCASE("chain override") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            auto options = defaultLoginOptions();
            options.chain =
                sum("aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906");
            const auto result = kit.login(options).value();
            CHECK(result.session->chain.id ==
                  sum("aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906"));
        }
        SUBCASE("errors on unknown chain") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            const auto result = kit.getChainDefinition(
                sum("c054efbc59625be7ce0d69ef26124fd349420b98fef2ed23fead4c558b9826b1"));
            CHECK_FALSE(result.has_value());
        }
        SUBCASE("chains subset") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            LoginOptions options;
            options.permissionLevel = PermissionLevel::from(mockPermissionLevel).value();
            options.chains = {
                {sum("1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4"),
                 sum("4667b205c6838ef70ff7988f6e8257e8be0e1284a2f59699054a018f743b1d11"),
                 sum("34593b65376aee3c9b06ea8a8595122b39333aaab4c76ad52587831fcc096590")}};
            const auto result = kit.login(options).value();
            CHECK(result.session->chain.id ==
                  sum("1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4"));
        }
        SUBCASE("chains subset with invalid selection errors") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            auto options = defaultLoginOptions();
            options.chain =
                sum("34593b65376aee3c9b06ea8a8595122b39333aaab4c76ad52587831fcc096590");
            options.chains = {
                {sum("1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4"),
                 sum("4667b205c6838ef70ff7988f6e8257e8be0e1284a2f59699054a018f743b1d11")}};
            const auto result = kit.login(options);
            CHECK_FALSE(result.has_value());
        }
        SUBCASE("chains subset with valid selection") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            auto options = defaultLoginOptions();
            options.chain =
                sum("4667b205c6838ef70ff7988f6e8257e8be0e1284a2f59699054a018f743b1d11");
            options.chains = {
                {sum("1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4"),
                 sum("4667b205c6838ef70ff7988f6e8257e8be0e1284a2f59699054a018f743b1d11")}};
            const auto result = kit.login(options).value();
            CHECK(result.session->chain.id ==
                  sum("4667b205c6838ef70ff7988f6e8257e8be0e1284a2f59699054a018f743b1d11"));
        }
        SUBCASE("permissionLevel typed") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            auto options = defaultLoginOptions();
            options.permissionLevel = PermissionLevel::from("mock@interface").value();
            const auto result = kit.login(options).value();
            CHECK(result.session->permissionLevel ==
                  PermissionLevel::from("mock@interface").value());
        }
    }

    TEST_CASE("logout") {
        SUBCASE("no param") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            const auto login = kit.login().value();
            CHECK(kit.getSessions().value().size() == 1);
            REQUIRE(kit.logout().has_value());
            CHECK(kit.getSessions().value().empty());
        }
        SUBCASE("session param") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            const auto login = kit.login().value();
            CHECK(kit.getSessions().value().size() == 1);
            REQUIRE(kit.logout(*login.session).has_value());
            CHECK(kit.getSessions().value().empty());
        }
        SUBCASE("serialized session param") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            const auto login = kit.login().value();
            CHECK(kit.getSessions().value().size() == 1);
            REQUIRE(kit.logout(login.session->serialize()).has_value());
            CHECK(kit.getSessions().value().empty());
        }
    }

    TEST_CASE("restore") {
        SUBCASE("session") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            const auto login = kit.login().value();
            const auto serialized = login.session->serialize();
            // a second kit with fresh storage restores from the serialized form
            SessionKit other(mockSessionKitArgs(), mockSessionKitOptions());
            const auto restored = other.restore(serialized).value();
            assertSessionMatchesMockSession(restored);
        }
        SUBCASE("session data") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            const auto login = kit.login().value();
            json data = login.session->data();
            data["customField"] = "data value";
            login.session->setData(data);
            REQUIRE(kit.persistSession(*login.session).has_value());
            const auto restored = kit.restore().value();
            REQUIRE(restored);
            CHECK(restored->data()["customField"] == "data value");
        }
        SUBCASE("session by chain id") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            LoginOptions loginWax;
            loginWax.chain = Chains::WAX().id;
            loginWax.permissionLevel = PermissionLevel::from("mock1@interface").value();
            REQUIRE(kit.login(loginWax).has_value());
            LoginOptions loginJungle;
            loginJungle.chain = Chains::Jungle4().id;
            loginJungle.permissionLevel = PermissionLevel::from("mock2@interface").value();
            REQUIRE(kit.login(loginJungle).has_value());
            LoginOptions loginEos;
            loginEos.chain = Chains::EOS().id;
            loginEos.permissionLevel = PermissionLevel::from("mock3@interface").value();
            REQUIRE(kit.login(loginEos).has_value());

            const auto sessions = kit.restoreAll().value();
            REQUIRE(sessions.size() == 3);
            CHECK(sessions[0]->actor() == Name::from("mock1"));
            CHECK(sessions[0]->chain.id == Chains::WAX().id);
            CHECK(sessions[1]->actor() == Name::from("mock2"));
            CHECK(sessions[1]->chain.id == Chains::Jungle4().id);
            CHECK(sessions[2]->actor() == Name::from("mock3"));
            CHECK(sessions[2]->chain.id == Chains::EOS().id);

            RestoreArgs eosArgs;
            eosArgs.chain = Chains::EOS().id;
            const auto restoredEOS = kit.restore(eosArgs).value();
            REQUIRE(restoredEOS);
            CHECK(restoredEOS->actor() == Name::from("mock3"));
            CHECK(restoredEOS->chain.id == Chains::EOS().id);

            RestoreArgs jungleArgs;
            jungleArgs.chain = Chains::Jungle4().id;
            const auto restoredJungle = kit.restore(jungleArgs).value();
            REQUIRE(restoredJungle);
            CHECK(restoredJungle->actor() == Name::from("mock2"));
            CHECK(restoredJungle->chain.id == Chains::Jungle4().id);
        }
        SUBCASE("no session returns null") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            const auto restored = kit.restore().value();
            CHECK(restored == nullptr);
        }
        SUBCASE("can restore with just actor, permission and chain id") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            const auto login = kit.login().value();
            const auto serialized = login.session->serialize();
            SessionKit other(mockSessionKitArgs(), mockSessionKitOptions());
            // restore with a serialized wallet reference but no stored session
            RestoreArgs args;
            args.chain = sum(serialized.chain.c_str());
            args.actor = serialized.actor;
            args.permission = serialized.permission;
            args.walletPlugin = serialized.walletPlugin;
            const auto restored = other.restore(args).value();
            assertSessionMatchesMockSession(restored);
        }
        SUBCASE("errors if wallet not found") {
            auto args = mockSessionKitArgs();
            args.walletPlugins = {std::make_shared<MockWalletPluginConfigs>()};
            SessionKit kit(args, mockSessionKitOptions());
            const auto login = kit.login().value();
            const auto serialized = login.session->serialize();
            SessionKit other(mockSessionKitArgs(), mockSessionKitOptions());
            const auto restored = other.restore(serialized);
            CHECK_FALSE(restored.has_value());
        }
    }

    TEST_CASE("restoreAll") {
        SUBCASE("restores no sessions") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            CHECK(kit.restore().value() == nullptr);
            CHECK(kit.getSessions().value().empty());
        }
        SUBCASE("restores all sessions") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            for (const char* actor : {"mock1@interface", "mock2@interface", "mock3@interface"}) {
                LoginOptions options;
                options.chain = mockChainDefinition().id;
                options.permissionLevel = PermissionLevel::from(actor).value();
                REQUIRE(kit.login(options).has_value());
            }
            const auto sessions = kit.restoreAll().value();
            REQUIRE(sessions.size() == 3);
            CHECK(sessions[0]->actor() == Name::from("mock1"));
            CHECK(sessions[1]->actor() == Name::from("mock2"));
            CHECK(sessions[2]->actor() == Name::from("mock3"));
        }
    }

    TEST_CASE("setEndpoint") {
        auto args = mockSessionKitArgs();
        args.chains = {mockChainDefinition()};
        SessionKit kit(args, mockSessionKitOptions());
        CHECK(kit.chains[0].url == "https://jungle4.greymass.com");
        REQUIRE(kit.setEndpoint(mockChainDefinition().id, "https://wax.greymass.com").has_value());
        CHECK(kit.chains[0].url == "https://wax.greymass.com");
        REQUIRE(kit.setEndpoint(mockChainDefinition().id, "https://telos.greymass.com").has_value());
        CHECK(kit.chains[0].url == "https://telos.greymass.com");
    }

    TEST_CASE("ui wallet selection") {
        SUBCASE("one wallet used without selection") {
            SessionKit kit(mockSessionKitArgs(), mockSessionKitOptions());
            LoginOptions options;
            options.permissionLevel = PermissionLevel::from(mockPermissionLevel).value();
            const auto result = kit.login(options).value();
            assertSessionMatchesMockSession(result.session);
        }
        SUBCASE("multiple wallets force selection") {
            auto args = mockSessionKitArgs();
            args.walletPlugins = {makeWallet(), makeWallet()};
            SessionKit kit(args, mockSessionKitOptions());
            LoginOptions options;
            options.permissionLevel = PermissionLevel::from(mockPermissionLevel).value();
            const auto result = kit.login(options).value();
            assertSessionMatchesMockSession(result.session);
        }
        SUBCASE("invalid wallet index errors") {
            class FailingUI : public MockUserInterface {
            public:
                Result<UserInterfaceLoginResponse> login(LoginContext&) override {
                    UserInterfaceLoginResponse response;
                    response.chainId = Checksum256::from(std::string_view(mockChainId)).value();
                    response.permissionLevel = PermissionLevel::from(mockPermissionLevel).value();
                    response.walletPluginIndex = 999999;
                    return response;
                }
            };
            auto args = mockSessionKitArgs(std::make_shared<FailingUI>());
            args.walletPlugins = {makeWallet(), makeWallet()};
            SessionKit kit(args, mockSessionKitOptions());
            const auto result = kit.login();
            CHECK_FALSE(result.has_value());
        }
    }
}
