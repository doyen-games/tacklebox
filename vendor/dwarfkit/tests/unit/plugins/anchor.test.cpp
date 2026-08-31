// Port of wallet-plugin-anchor test/tests/{chains,mode,index}.ts. The
// choice-screen and popup/router suites cover the interactive browser UX that
// the C++ port replaces with option-driven mode selection (DIVERGENCES.md);
// the login-and-sign flow stubs buoy with a fake WebSocketProvider like the
// upstream sinon receive stub.
#include <doctest/doctest.h>

#include <dwarfkit/plugins/wallet/anchor.hpp>
#include <dwarfkit/session.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;
using anchor::AnchorMode;

namespace {

const char* vaultaId = "aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906";
const char* jungle4Id = "73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d";
const char* waxId = "1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4";

json mockCallbackPayload() {
    const auto mockPublicKey =
        PrivateKey::from(mockPrivateKey).value().toPublic().value().toString();
    return json{
        {"sig",
         "SIG_K1_K4nkCupUx3hDXSHq4rhGPpDMPPPjJyvmF3M6j7ppYUzkR3L93endwnxf3YhJSG4SSvxxU1ytD8hj39"
         "kukTeYxjwy5H3XNJ"},
        {"tx", "b8e921a7b68d7309847e633d74963f25eb5a7d0b15b1aceb143723c234686a8d"},
        {"rbn", "0"},
        {"rid", "0"},
        {"ex", "2020-07-10T08:40:20"},
        {"req", "esr://AgABAwACE2h0dHBzOi8vZXhhbXBsZS5jb20A"},
        {"sa", "wharfkit1115"},
        {"sp", "test"},
        {"cid", jungle4Id},
        {"link_ch", "https://cb.test.com/a5b24a32-cce5-4ab5-b63d-8e29f83e25a9"},
        {"link_key", mockPublicKey},
        {"link_name", "anchor"}};
}

// The upstream sinon.stub(buoy, 'receive') equivalent: every receive yields
// the mock payload.
class FakeWebSocketProvider final : public WebSocketProvider {
public:
    explicit FakeWebSocketProvider(std::string payload) : payload_(std::move(payload)) {}

    Result<void> connect(std::string_view) override { return {}; }
    Result<Bytes> receive(std::chrono::milliseconds, CancelToken) override {
        return Bytes(std::vector<uint8_t>(payload_.begin(), payload_.end()));
    }
    Result<void> send(std::span<const uint8_t>) override { return {}; }
    void close() override {}

private:
    std::string payload_;
};

}  // namespace

TEST_SUITE("anchor-chains") {
    TEST_CASE("resolves the built-in defaults") {
        CHECK(anchor::resolveWebAuthenticatorUrl(vaultaId) == "https://vaulta.anchorwallet.io");
        CHECK(anchor::resolveWebAuthenticatorUrl(jungle4Id) ==
              "https://jungle4.anchorwallet.io");
    }
    TEST_CASE("returns nullopt for chains without web support") {
        CHECK_FALSE(anchor::resolveWebAuthenticatorUrl(waxId).has_value());
    }
    TEST_CASE("returns nullopt when no chain is given") {
        CHECK_FALSE(anchor::resolveWebAuthenticatorUrl(std::nullopt).has_value());
    }
    TEST_CASE("overrides win over defaults") {
        CHECK(anchor::resolveWebAuthenticatorUrl(vaultaId,
                                                 {{vaultaId, "http://localhost:5173"}}) ==
              "http://localhost:5173");
    }
    TEST_CASE("overrides add chains that have no default") {
        CHECK(anchor::resolveWebAuthenticatorUrl(waxId, {{waxId, "https://wax.example.com"}}) ==
              "https://wax.example.com");
    }
    TEST_CASE("chain id matching is case insensitive") {
        std::string upper = vaultaId;
        for (auto& c : upper) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        CHECK(anchor::resolveWebAuthenticatorUrl(upper) == "https://vaulta.anchorwallet.io");
    }
    TEST_CASE("strips trailing slashes so callers can append paths") {
        CHECK(anchor::resolveWebAuthenticatorUrl(vaultaId,
                                                 {{vaultaId, "https://example.com/"}}) ==
              "https://example.com");
    }
    TEST_CASE("exposes exactly the two supported chains by default") {
        const auto& urls = anchor::DEFAULT_WEB_AUTHENTICATOR_URLS();
        CHECK(urls.size() == 2);
        CHECK(urls.count(vaultaId) == 1);
        CHECK(urls.count(jungle4Id) == 1);
    }
}

TEST_SUITE("anchor-mode") {
    TEST_CASE("reads a stored mode") {
        CHECK(anchor::readMode(json{{"mode", "web"}}) == AnchorMode::web);
        CHECK(anchor::readMode(json{{"mode", "app"}}) == AnchorMode::app);
    }
    TEST_CASE("returns nullopt for empty storage") {
        CHECK_FALSE(anchor::readMode(json::object()).has_value());
    }
    TEST_CASE("ignores a garbage stored mode") {
        CHECK_FALSE(anchor::readMode(json{{"mode", "sideways"}}).has_value());
    }
    TEST_CASE("infers app mode from a v1.x native session") {
        // Sessions serialized before 2.0 have no mode, but always carry a
        // buoy channel.
        CHECK(anchor::readMode(json{{"channelUrl", "https://cb.anchor.link/abc"},
                                    {"channelName", "laptop"}}) == AnchorMode::app);
    }
    TEST_CASE("infers web mode from web transport keys") {
        CHECK(anchor::readMode(json{{"encryptionKey", "PVT_K1_x"},
                                    {"messageKey", "PUB_K1_y"}}) == AnchorMode::web);
    }
    TEST_CASE("an explicit mode beats inference") {
        CHECK(anchor::readMode(json{{"mode", "web"},
                                    {"channelUrl", "https://cb.anchor.link/abc"}}) ==
              AnchorMode::web);
    }
    TEST_CASE("writes a mode into storage") {
        json data = json::object();
        anchor::writeMode(data, AnchorMode::web);
        CHECK(data["mode"] == "web");
        anchor::writeMode(data, AnchorMode::app);
        CHECK(data["mode"] == "app");
    }
    TEST_CASE("ledger transport support is never available") {
        CHECK_FALSE(anchor::ledgerTransportAvailable());
    }
}

TEST_SUITE("anchor-login-options") {
    TEST_CASE("reads a namespaced per-call mode") {
        CHECK(anchor::readLoginOptions("anchor", json{{"anchor", {{"mode", "web"}}}})
                  .value()
                  .mode == AnchorMode::web);
        CHECK(anchor::readLoginOptions("anchor", json{{"anchor", {{"mode", "app"}}}})
                  .value()
                  .mode == AnchorMode::app);
    }
    TEST_CASE("an absent bag is not an error") {
        CHECK_FALSE(anchor::readLoginOptions("anchor", json()).value().mode.has_value());
        CHECK_FALSE(
            anchor::readLoginOptions("anchor", json::object()).value().mode.has_value());
    }
    TEST_CASE("an entry with no mode is not an error") {
        CHECK_FALSE(anchor::readLoginOptions("anchor", json{{"anchor", json::object()}})
                        .value()
                        .mode.has_value());
    }
    TEST_CASE("another plugin's options are ignored") {
        CHECK_FALSE(anchor::readLoginOptions("anchor", json{{"other", {{"mode", "web"}}}})
                        .value()
                        .mode.has_value());
    }
    TEST_CASE("the id decides which entry is read") {
        CHECK(anchor::readLoginOptions(
                  "other", json{{"anchor", {{"mode", "web"}}}, {"other", {{"mode", "app"}}}})
                  .value()
                  .mode == AnchorMode::app);
    }
    TEST_CASE("a bad mode errors like setMode does") {
        const auto result =
            anchor::readLoginOptions("anchor", json{{"anchor", {{"mode", "sideways"}}}});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message.find("Invalid Anchor mode") != std::string::npos);
    }
    TEST_CASE("an un-nested value errors rather than being ignored") {
        const auto result = anchor::readLoginOptions("anchor", json{{"anchor", "web"}});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message.find("Invalid Anchor login options") != std::string::npos);
    }
}

TEST_SUITE("anchor-plugin") {
    TEST_CASE("login and sign") {
        const auto payload = mockCallbackPayload();
        const auto fakeWs = std::make_shared<FakeWebSocketProvider>(payload.dump());

        auto plugin = std::make_shared<WalletPluginAnchor>(
            WalletPluginAnchorOptions{.buoyWs = fakeWs});

        SessionKitArgs args = mockSessionKitArgs();
        args.walletPlugins = {plugin};
        SessionKitOptions options;
        options.fetch = std::make_shared<MockFetchProvider>(DK_FIXTURE_DIR "/anchor/data");
        options.storage = std::make_shared<MockStorage>();
        SessionKit kit(args, options);

        LoginOptions loginOptions;
        loginOptions.chain = Checksum256::from(jungle4Id).value();
        loginOptions.permissionLevel = PermissionLevel::from("wharfkit1115@test").value();
        auto loginResult = kit.login(loginOptions);
        const std::string loginError = loginResult ? "" : loginResult.error().message;
        REQUIRE_MESSAGE(loginResult.has_value(), loginError);
        auto session = loginResult->session;
        REQUIRE(session != nullptr);
        CHECK(session->chain.id.hexString() == jungle4Id);
        CHECK(session->actor() == Name::from("wharfkit1115"));
        CHECK(session->permission() == Name::from("test"));

        // The plugin persisted the buoy channel from the login callback.
        CHECK(plugin->data()["channelName"] == "anchor");
        CHECK(plugin->getMode() == AnchorMode::app);

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
        CHECK(result->signer.permission == Name::from("test"));
        CHECK(result->signatures.size() == 1);
    }

    TEST_CASE("mode override routes login") {
        auto plugin = std::make_shared<WalletPluginAnchor>(
            WalletPluginAnchorOptions{.mode = AnchorMode::web});
        CHECK(plugin->getMode() == AnchorMode::web);
        plugin->setMode(std::nullopt);
        CHECK_FALSE(plugin->getMode().has_value());
    }

    TEST_CASE("web authenticator url resolution on the plugin") {
        WalletPluginAnchor plugin;
        CHECK(plugin.webAuthenticatorUrl(jungle4Id) == "https://jungle4.anchorwallet.io");
        CHECK_FALSE(plugin.webAuthenticatorUrl(waxId).has_value());
    }
}
