// Port of wallet-plugin-cleos test/tests/common.ts. The upstream test is
// commented out (needs a window-less rework upstream); it runs here with a
// prompt-recording UI.
#include <doctest/doctest.h>

#include <dwarfkit/plugins/wallet/cleos.hpp>
#include <dwarfkit/session.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

class RecordingUserInterface final : public MockUserInterface {
public:
    Result<PromptResponse> prompt(const PromptArgs& args, CancelToken token) override {
        prompts.push_back(args);
        return MockUserInterface::prompt(args, token);
    }
    std::vector<PromptArgs> prompts;
};

}  // namespace

TEST_SUITE("cleos") {
    TEST_CASE("login and sign") {
        auto ui = std::make_shared<RecordingUserInterface>();
        SessionKitArgs args = mockSessionKitArgs(ui);
        args.walletPlugins = {std::make_shared<WalletPluginCleos>()};
        SessionKitOptions options;
        options.fetch = std::make_shared<MockFetchProvider>(DK_FIXTURE_DIR "/cleos/data");
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
        // cleos signs nothing; the transaction is signed out of band
        CHECK(result->signatures.empty());

        // the afterSign hook prompted with the cleos command
        REQUIRE(!ui->prompts.empty());
        const auto& prompt = ui->prompts.back();
        CHECK(prompt.title == "Sign with cleos");
        REQUIRE(!prompt.elements.empty());
        CHECK(prompt.elements[0].type == PromptElementType::textarea);
        const std::string command = prompt.elements[0].data["content"].get<std::string>();
        CHECK(command.starts_with("cleos -u https://jungle4.greymass.com push transaction '"));
        CHECK(command.find("\"quantity\": \"0.0001 EOS\"") != std::string::npos);
        CHECK(command.find("\"memo\": \"wharfkit/session wallet plugin template\"") !=
              std::string::npos);
    }

    TEST_CASE("login requires chain and permission") {
        WalletPluginCleos plugin;
        CHECK(plugin.config().requiresChainSelect);
        CHECK(plugin.config().requiresPermissionEntry.value_or(false));
    }
}
