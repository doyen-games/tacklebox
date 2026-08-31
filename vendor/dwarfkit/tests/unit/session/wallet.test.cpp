// Port of session test/tests/wallet.ts
#include <doctest/doctest.h>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

SessionKitArgs defaultKitArgs(const std::shared_ptr<WalletPlugin>& wallet) {
    SessionKitArgs args;
    args.appName = "demo.app";
    args.chains = mockChainDefinitions();
    args.ui = std::make_shared<MockUserInterface>();
    args.walletPlugins = {wallet};
    return args;
}

Checksum256 sum(const char* hex) { return Checksum256::from(std::string_view(hex)).value(); }

}  // namespace

TEST_SUITE("session-wallet") {
    TEST_CASE("config defaults") {
        MockWalletPluginConfigs walletPlugin;
        CHECK(walletPlugin.config().requiresChainSelect == true);
        CHECK(walletPlugin.config().requiresPermissionSelect == false);
        CHECK(walletPlugin.config().supportedChains.empty());
    }

    TEST_CASE("config override") {
        MockWalletPluginConfigs walletPlugin(
            WalletPluginConfig{.requiresChainSelect = false,
                               .requiresPermissionSelect = true,
                               .supportedChains = {mockChainId}});
        CHECK(walletPlugin.config().requiresChainSelect == false);
        CHECK(walletPlugin.config().requiresPermissionSelect == true);
        REQUIRE(walletPlugin.config().supportedChains.size() == 1);
        CHECK(walletPlugin.config().supportedChains[0] == mockChainId);
    }

    TEST_CASE("requiresChainSelect") {
        SUBCASE("true, triggers ui for user selection") {
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>(
                WalletPluginConfig{.requiresChainSelect = true,
                                   .requiresPermissionSelect = false});
            SessionKit kit(defaultKitArgs(walletPlugin), mockSessionKitOptions());
            LoginOptions options;
            options.permissionLevel = PermissionLevel::from("mock@interface").value();
            const auto result = kit.login(options).value();
            CHECK(result.response.chain == mockChainDefinitions()[0].id);
            CHECK(result.response.permissionLevel.actor == Name::from("mock"));
            CHECK(result.response.permissionLevel.permission == Name::from("interface"));
        }
        SUBCASE("false, wallet returns chain") {
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>(
                WalletPluginConfig{.requiresChainSelect = false,
                                   .requiresPermissionSelect = false});
            SessionKit kit(defaultKitArgs(walletPlugin), mockSessionKitOptions());
            LoginOptions options;
            options.permissionLevel = PermissionLevel::from("mock@interface").value();
            const auto result = kit.login(options).value();
            CHECK(result.response.chain == mockChainDefinitions()[0].id);
            CHECK(result.response.permissionLevel.actor == Name::from("mock"));
        }
    }

    TEST_CASE("requiresPermissionSelect") {
        SUBCASE("true, triggers ui for user selection") {
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>(
                WalletPluginConfig{.requiresChainSelect = false,
                                   .requiresPermissionSelect = true});
            SessionKit kit(defaultKitArgs(walletPlugin), mockSessionKitOptions());
            LoginOptions options;
            options.chain = mockChainDefinition().id;
            const auto result = kit.login(options).value();
            CHECK(result.response.chain == mockChainDefinition().id);
            CHECK(result.response.permissionLevel.actor == Name::from("mock"));
            CHECK(result.response.permissionLevel.permission == Name::from("interface"));
        }
        SUBCASE("false, walletPlugin returns it") {
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>(
                WalletPluginConfig{.requiresChainSelect = false,
                                   .requiresPermissionSelect = false});
            SessionKit kit(defaultKitArgs(walletPlugin), mockSessionKitOptions());
            LoginOptions options;
            options.chain = mockChainDefinition().id;
            const auto result = kit.login(options).value();
            CHECK(result.response.chain == mockChainDefinition().id);
            CHECK(result.response.permissionLevel.actor == Name::from("wharfkit1111"));
            CHECK(result.response.permissionLevel.permission == Name::from("test"));
        }
    }

    TEST_CASE("supportedChains") {
        SUBCASE("works on supported chain") {
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>(
                WalletPluginConfig{.requiresChainSelect = true,
                                   .requiresPermissionSelect = false,
                                   .supportedChains = {mockChainId}});
            SessionKit kit(defaultKitArgs(walletPlugin), mockSessionKitOptions());
            LoginOptions options;
            options.permissionLevel = PermissionLevel::from("mock@interface").value();
            CHECK(kit.login(options).has_value());
        }
        SUBCASE("errors on unsupported chain") {
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>(
                WalletPluginConfig{
                    .requiresChainSelect = true,
                    .requiresPermissionSelect = false,
                    .supportedChains = {
                        "f16b1833c747c43682f4386fca9cbb327929334a762755ebec17f6f23c9b8a12"}});
            SessionKit kit(defaultKitArgs(walletPlugin), mockSessionKitOptions());
            LoginOptions options;
            options.chain =
                sum("34593b65376aee3c9b06ea8a8595122b39333aaab4c76ad52587831fcc096590");
            options.permissionLevel = PermissionLevel::from("mock@interface").value();
            CHECK_FALSE(kit.login(options).has_value());
        }
        SUBCASE("works if requiresChainSelect is false and chain is specified") {
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>(
                WalletPluginConfig{
                    .requiresChainSelect = false,
                    .requiresPermissionSelect = false,
                    .supportedChains = {
                        "34593b65376aee3c9b06ea8a8595122b39333aaab4c76ad52587831fcc096590"}});
            SessionKit kit(defaultKitArgs(walletPlugin), mockSessionKitOptions());
            LoginOptions options;
            options.chain =
                sum("34593b65376aee3c9b06ea8a8595122b39333aaab4c76ad52587831fcc096590");
            options.permissionLevel = PermissionLevel::from("mock@interface").value();
            CHECK(kit.login(options).has_value());
        }
        SUBCASE("errors if requiresChainSelect is false and no chain is specified") {
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>(
                WalletPluginConfig{
                    .requiresChainSelect = false,
                    .requiresPermissionSelect = false,
                    .supportedChains = {
                        "f16b1833c747c43682f4386fca9cbb327929334a762755ebec17f6f23c9b8a12"}});
            SessionKit kit(defaultKitArgs(walletPlugin), mockSessionKitOptions());
            LoginOptions options;
            options.permissionLevel = PermissionLevel::from("mock@interface").value();
            CHECK_FALSE(kit.login(options).has_value());
        }
    }

    TEST_CASE("sign response request modification") {
        SUBCASE("allowModify: default (true)") {
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>(
                std::nullopt, json{{"testModify", true}, {"privateKey", mockPrivateKey}});
            SessionKit kit(defaultKitArgs(walletPlugin), mockSessionKitOptions());
            LoginOptions options;
            options.chain = mockChainDefinition().id;
            options.permissionLevel = PermissionLevel::from("mock@interface").value();
            const auto login = kit.login(options).value();
            const auto result = login.session->transact(
                {.action = Serializer::objectify(makeMockAction())}, {.broadcast = false});
            CHECK(result.has_value());
        }
        SUBCASE("allowModify: false") {
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>(
                std::nullopt, json{{"testModify", true}, {"privateKey", mockPrivateKey}});
            auto kitOptions = mockSessionKitOptions();
            kitOptions.allowModify = false;
            SessionKit kit(defaultKitArgs(walletPlugin), kitOptions);
            LoginOptions options;
            options.chain = mockChainDefinition().id;
            options.permissionLevel = PermissionLevel::from("mock@interface").value();
            const auto login = kit.login(options).value();
            const auto result = login.session->transact(
                {.action = Serializer::objectify(makeMockAction())}, {.broadcast = false});
            CHECK_FALSE(result.has_value());
        }
    }

    TEST_CASE("storage") {
        SUBCASE("empty data") {
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>();
            SessionKit kit(defaultKitArgs(walletPlugin), mockSessionKitOptions());
            const auto response = kit.login().value();
            CHECK(response.session->serialize().toJSON().dump() ==
                  R"({"chain":"73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d",)"
                  R"("actor":"mock","permission":"interface","walletPlugin":)"
                  R"({"id":"MockWalletPluginConfigs","data":{}}})");
        }
        SUBCASE("persists data") {
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>(
                WalletPluginConfig{.requiresChainSelect = true,
                                   .requiresPermissionSelect = false},
                json{{"foo", "baz"}});
            SessionKit kit(defaultKitArgs(walletPlugin), mockSessionKitOptions());
            const auto response = kit.login().value();
            CHECK(response.session->serialize().toJSON().dump() ==
                  R"({"chain":"73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d",)"
                  R"("actor":"mock","permission":"interface","walletPlugin":)"
                  R"({"id":"MockWalletPluginConfigs","data":{"foo":"baz"}}})");
        }
        SUBCASE("restores data alongside session") {
            SerializedSession serialized;
            serialized.chain = mockChainId;
            serialized.actor = "mock";
            serialized.permission = "interface";
            serialized.walletPlugin = {"MockWalletPluginConfigs", json{{"foo", "bar"}}};
            const auto walletPlugin = std::make_shared<MockWalletPluginConfigs>();
            SessionKit kit(defaultKitArgs(walletPlugin), mockSessionKitOptions());
            const auto session = kit.restore(serialized).value();
            REQUIRE(session);
            CHECK(session->walletPlugin->data()["foo"] == "bar");
        }
    }

    TEST_CASE("metadata logo") {
        SUBCASE("from plugin") {
            MockWalletPluginConfigs walletPlugin;
            REQUIRE(walletPlugin.metadata().logo.has_value());
            CHECK(walletPlugin.metadata().logo->toString() ==
                  "https://assets.wharfkit.com/chain/jungle.png");
        }
        SUBCASE("empty") {
            const auto metadata = WalletPluginMetadata::from(json::object());
            CHECK_FALSE(metadata.logo.has_value());
        }
        SUBCASE("string") {
            const auto metadata = WalletPluginMetadata::from(json{{"logo", "foo"}});
            REQUIRE(metadata.logo.has_value());
            CHECK(metadata.logo->toString() == "foo");
            CHECK(metadata.logo->getVariant("light") == "foo");
            CHECK(metadata.logo->light == "foo");
            CHECK(metadata.logo->getVariant("dark") == "foo");
            CHECK(metadata.logo->dark == "foo");
        }
        SUBCASE("object") {
            const auto metadata =
                WalletPluginMetadata::from(json{{"logo", {{"light", "foo"}, {"dark", "bar"}}}});
            REQUIRE(metadata.logo.has_value());
            CHECK(metadata.logo->toString() == "foo");
            CHECK(metadata.logo->light == "foo");
            CHECK(metadata.logo->dark == "bar");
        }
    }
}
