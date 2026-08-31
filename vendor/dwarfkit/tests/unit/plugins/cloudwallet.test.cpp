// Port of wallet-plugin-cloudwallet test/tests/common.ts. The upstream
// login-and-sign test is commented out there because it needs a window; the
// WebViewBridge interface makes it runnable here with a scripted fake bridge.
#include <doctest/doctest.h>

#include <deque>

#include <dwarfkit/plugins/wallet/cloudwallet.hpp>
#include <dwarfkit/session.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

constexpr const char* mockSig =
    "SIG_K1_K4nkCupUx3hDXSHq4rhGPpDMPPPjJyvmF3M6j7ppYUzkR3L93endwnxf3YhJSG4SSvxxU1ytD8hj39kukTe"
    "Yxjwy5H3XNJ";

// Scripted popup: answers login with a login response, and the signing page
// exchange with a ready event followed by a response echoing the posted
// transaction.
class FakeWebViewBridge final : public WebViewBridge {
public:
    Result<void> open(const std::string& url) override {
        openedUrls.push_back(url);
        return {};
    }
    Result<json> awaitMessage(std::chrono::milliseconds, CancelToken) override {
        if (openedUrls.back().find("/cloud-wallet/login") != std::string::npos) {
            return json{{"verified", true},
                        {"userAccount", "wharfkit1115"},
                        {"permission", "test"},
                        {"pubKeys", json::array()}};
        }
        if (!readyDelivered) {
            readyDelivered = true;
            return json::object();
        }
        readyDelivered = false;
        return json{{"verified", true},
                    {"signatures", json::array({mockSig})},
                    {"type", "TRANSACTION"},
                    {"serializedTransaction", lastPosted.value("transaction", "")}};
    }
    Result<void> postMessage(const json& message) override {
        lastPosted = message;
        return {};
    }
    void close() override { closes++; }

    std::vector<std::string> openedUrls;
    json lastPosted;
    bool readyDelivered = false;
    int closes = 0;
};

Result<Action> makeValidationAction(const Name& account, const Name& name, const json& data,
                                    const Name& actor) {
    DK_TRY(encoded, Serializer::encode(data, name.toString(), cloudwallet::validationAbi()));
    Action action;
    action.account = account;
    action.name = name;
    action.authorization = {PermissionLevel{actor, "active"_n}};
    action.data = Bytes(encoded.array);
    return action;
}

}  // namespace

TEST_SUITE("cloudwallet") {
    TEST_CASE("login and sign") {
        auto bridge = std::make_shared<FakeWebViewBridge>();
        WalletPluginCloudWalletOptions pluginOptions;
        pluginOptions.bridge = bridge;
        // the recorded fixtures are on jungle4; override the supported set
        pluginOptions.supportedChains = {
            "73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d"};
        auto plugin = std::make_shared<WalletPluginCloudWallet>(pluginOptions);

        SessionKitArgs args = mockSessionKitArgs();
        args.walletPlugins = {plugin};
        SessionKitOptions options;
        options.fetch = std::make_shared<MockFetchProvider>(DK_FIXTURE_DIR "/cloudwallet/data");
        options.storage = std::make_shared<MockStorage>();
        SessionKit kit(args, options);

        LoginOptions loginOptions;
        loginOptions.chain =
            Checksum256::from("73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d")
                .value();
        loginOptions.permissionLevel = PermissionLevel::from("wharfkit1115@test").value();
        auto loginResult = kit.login(loginOptions);
        const std::string loginError = loginResult ? "" : loginResult.error().message;
        REQUIRE_MESSAGE(loginResult.has_value(), loginError);
        auto session = loginResult->session;
        REQUIRE(session != nullptr);
        CHECK(session->actor() == Name::from("wharfkit1115"));
        CHECK(session->permission() == Name::from("test"));
        CHECK(bridge->openedUrls.size() == 1);
        CHECK(bridge->openedUrls[0].starts_with(
            "https://www.mycloudwallet.com/cloud-wallet/login?v=1.6.5"));
        // upstream: localStorage.setItem('connectedType', 'web')
        CHECK(plugin->data()["connectedType"] == "web");

        const auto result = session->transact(
            {.action = json{{"account", "eosio.token"},
                            {"name", "transfer"},
                            {"authorization", json::array({{{"actor", "wharfkit1115"},
                                                            {"permission", "test"}}})},
                            {"data",
                             {{"from", "wharfkit1115"},
                              {"to", "wharfkittest"},
                              {"quantity", "0.0001 EOS"},
                              {"memo", "wharfkit/session wallet plugin template"}}}}},
            {.broadcast = false});
        const std::string transactError = result ? "" : result.error().message;
        REQUIRE_MESSAGE(result.has_value(), transactError);
        CHECK(result->signer.actor == Name::from("wharfkit1115"));
        CHECK(result->signatures.size() == 1);
        // the signing exchange posted a TRANSACTION message with the version
        CHECK(bridge->lastPosted["type"] == "TRANSACTION");
        CHECK(bridge->lastPosted["version"] == "1.6.5");
        CHECK(bridge->openedUrls.back() ==
              "https://www.mycloudwallet.com/cloud-wallet/signing/");
    }

    TEST_CASE("options override url and supported chains") {
        WalletPluginCloudWalletOptions pluginOptions;
        pluginOptions.url = "https://example.com";
        pluginOptions.supportedChains = {"abc"};
        const WalletPluginCloudWallet plugin(pluginOptions);
        CHECK(plugin.url == "https://example.com");
        CHECK(plugin.config().supportedChains == std::vector<std::string>{"abc"});
        CHECK(plugin.hasLogout());
    }

    TEST_CASE("validateModifications") {
        const Name actor = Name::from("wharfkit1115");
        const auto original = [&] {
            Transaction tx;
            tx.actions = {makeValidationAction(
                              "eosio.token"_n, "transfer"_n,
                              json{{"from", "wharfkit1115"},
                                   {"to", "wharfkittest"},
                                   {"quantity", "1.00000000 WAX"},
                                   {"memo", "test"}},
                              actor)
                              .value()};
            return tx;
        }();
        SUBCASE("identical transaction passes") {
            CHECK(cloudwallet::validateModifications(original, original).has_value());
        }
        SUBCASE("missing original action fails") {
            Transaction modified;
            const auto result = cloudwallet::validateModifications(original, modified);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().message ==
                  "The modified transaction does not contain all the original actions.");
        }
        SUBCASE("wax fee transfer to txfee.wam is allowed") {
            Transaction modified = original;
            modified.actions.push_back(
                makeValidationAction("eosio.token"_n, "transfer"_n,
                                     json{{"from", "wharfkit1115"},
                                          {"to", "txfee.wam"},
                                          {"quantity", "0.12345678 WAX"},
                                          {"memo", "WAX fee for 1234"}},
                                     actor)
                    .value());
            CHECK(cloudwallet::validateModifications(original, modified).has_value());
        }
        SUBCASE("ram purchase for the user is allowed") {
            Transaction modified = original;
            modified.actions.push_back(
                makeValidationAction("eosio"_n, "buyrambytes"_n,
                                     json{{"payer", "somepayer111"},
                                          {"receiver", "wharfkit1115"},
                                          {"bytes", 1024}},
                                     actor)
                    .value());
            CHECK(cloudwallet::validateModifications(original, modified).has_value());
        }
        SUBCASE("other actions authorized by the user are rejected") {
            Transaction modified = original;
            modified.actions.push_back(
                makeValidationAction("eosio.token"_n, "transfer"_n,
                                     json{{"from", "wharfkit1115"},
                                          {"to", "attacker1111"},
                                          {"quantity", "100.00000000 WAX"},
                                          {"memo", "drain"}},
                                     actor)
                    .value());
            const auto result = cloudwallet::validateModifications(original, modified);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().message ==
                  "The modified transaction contains one or more actions that are not allowed.");
        }
        SUBCASE("actions not authorized by the user pass through") {
            Transaction modified = original;
            modified.actions.push_back(
                makeValidationAction("some.other"_n, "transfer"_n,
                                     json{{"from", "cosigner1111"},
                                          {"to", "elsewhere111"},
                                          {"quantity", "1.00000000 WAX"},
                                          {"memo", ""}},
                                     Name::from("cosigner1111"))
                    .value());
            CHECK(cloudwallet::validateModifications(original, modified).has_value());
        }
    }
}
