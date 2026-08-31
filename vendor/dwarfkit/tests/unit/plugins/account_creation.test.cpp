// Tests for the account creation plugins (the upstream repos ship only
// commented-out template tests).
#include <doctest/doctest.h>

#include <dwarfkit/plugins/account_creation/anchor.hpp>
#include <dwarfkit/plugins/account_creation/jungle4.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

struct FaucetFetch final : FetchProvider {
    FetchRequest last;
    FetchResponse response{200, "", {}};
    Result<FetchResponse> fetch(const FetchRequest& request) override {
        last = request;
        return response;
    }
};

CreateAccountContext makeContext(const std::shared_ptr<UserInterface>& ui) {
    CreateAccountContextOptions options;
    options.appName = "unittest";
    options.chain = Chains::Jungle4();
    options.chains = {Chains::Jungle4()};
    options.ui = ui;
    return CreateAccountContext(options);
}

}  // namespace

TEST_SUITE("account-creation") {
    TEST_CASE("jungle4 generates valid account names") {
        const auto name = AccountCreationPluginJungle4::generateRandomAccountName();
        CHECK(name.size() == 12);
        CHECK(name.ends_with(".gm"));
        CHECK(Name::from(name).toString() == name);
    }

    TEST_CASE("jungle4 creates an account through the faucet") {
        const auto fetch = std::make_shared<FaucetFetch>();
        AccountCreationPluginJungle4 plugin(fetch);
        const auto ui = std::make_shared<MockUserInterface>();
        auto context = makeContext(ui);
        const auto response = plugin.create(context).value();
        CHECK(response.chain.equals(Chains::Jungle4()));
        CHECK(response.accountName.toString().ends_with(".gm"));
        // the faucet request carries both keys and the network id
        CHECK(fetch->last.url == "https://jungle4.greymass.com/account/create");
        const json body = json::parse(fetch->last.body);
        CHECK(body["accountName"] == response.accountName.toString());
        CHECK(body["activeKey"] == body["ownerKey"]);
        CHECK(body["network"] == Chains::Jungle4().id.hexString());
        // the user was shown the private key
        const auto prompted =
            std::find_if(ui->messages.begin(), ui->messages.end(), [](const std::string& m) {
                return m.find("prompt: Testnet Account Created!") != std::string::npos;
            });
        CHECK(prompted != ui->messages.end());
    }

    TEST_CASE("jungle4 surfaces faucet errors") {
        const auto fetch = std::make_shared<FaucetFetch>();
        fetch->response.status = 400;
        fetch->response.headers = {{"Content-Type", "application/json"}};
        fetch->response.body = R"({"message":"account already exists"})";
        AccountCreationPluginJungle4 plugin(fetch);
        const auto ui = std::make_shared<MockUserInterface>();
        auto context = makeContext(ui);
        const auto response = plugin.create(context);
        REQUIRE_FALSE(response.has_value());
        CHECK(response.error().message ==
              "There was an error creating this account (account already exists)");
    }

    TEST_CASE("jungle4 through the kit createAccount flow") {
        auto args = mockSessionKitArgs();
        SessionKitOptions options = mockSessionKitOptions();
        options.accountCreationPlugins = {
            std::make_shared<AccountCreationPluginJungle4>(std::make_shared<FaucetFetch>())};
        SessionKit kit(args, options);
        const auto response =
            kit.createAccount({.pluginId = "account-creation-plugin-jungle4"}).value();
        CHECK(response.chain.equals(Chains::Jungle4()));
        CHECK(response.accountName.toString().ends_with(".gm"));
    }

    TEST_CASE("anchor account creator builds the service url") {
        AccountCreationOptions options;
        options.scope = Name::from("wallet");
        options.supportedChains = {ChainId::from(Chains::EOS().id),
                                   ChainId::from(Chains::WAX().id)};
        const AccountCreator creator(options);
        CHECK(creator.createAccountUrl() ==
              "https://create.anchor.link/create?supported_chains="
              "aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906%2C"
              "1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4"
              "&scope=wallet");
    }

    TEST_CASE("anchor account creator requires a dialog handler") {
        AccountCreationOptions options;
        options.scope = Name::from("wallet");
        const AccountCreator creator(options);
        const auto result = creator.createAccount();
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == ErrorKind::Unsupported);
    }

    TEST_CASE("anchor plugin maps the response to a chain definition") {
        AccountCreationPluginAnchorOptions options;
        std::string opened;
        options.openDialog = [&](const std::string& url) -> Result<json> {
            opened = url;
            return json{{"cid", Chains::EOS().id.hexString()}, {"sa", "newaccount11"}};
        };
        AccountCreationPluginAnchor plugin(options);
        const auto ui = std::make_shared<MockUserInterface>();
        CreateAccountContextOptions contextOptions;
        contextOptions.appName = "unittest";
        contextOptions.chains = {Chains::EOS(), Chains::WAX()};
        contextOptions.ui = ui;
        CreateAccountContext context(contextOptions);
        const auto response = plugin.create(context).value();
        CHECK(response.chain.equals(Chains::EOS()));
        CHECK(response.accountName == Name::from("newaccount11"));
        CHECK(opened.find("https://create.anchor.link/create?") == 0);
        CHECK(opened.find("scope=wallet") != std::string::npos);
    }

    TEST_CASE("anchor plugin surfaces service errors") {
        AccountCreationPluginAnchorOptions options;
        options.openDialog = [](const std::string&) -> Result<json> {
            return json{{"error", "Popup window closed"}};
        };
        AccountCreationPluginAnchor plugin(options);
        const auto ui = std::make_shared<MockUserInterface>();
        CreateAccountContextOptions contextOptions;
        contextOptions.ui = ui;
        CreateAccountContext context(contextOptions);
        const auto response = plugin.create(context);
        REQUIRE_FALSE(response.has_value());
        CHECK(response.error().message == "Popup window closed");
    }

    TEST_CASE("anchor plugin rejects unknown chain ids") {
        AccountCreationPluginAnchorOptions options;
        options.openDialog = [](const std::string&) -> Result<json> {
            return json{{"cid", "beefbeefbeefbeefbeefbeefbeefbeefbeefbeefbeefbeefbeefbeefbeefbeef"},
                        {"sa", "newaccount11"}};
        };
        AccountCreationPluginAnchor plugin(options);
        const auto ui = std::make_shared<MockUserInterface>();
        CreateAccountContextOptions contextOptions;
        contextOptions.ui = ui;
        CreateAccountContext context(contextOptions);
        const auto response = plugin.create(context);
        REQUIRE_FALSE(response.has_value());
        CHECK(response.error().message.find("is not supported") != std::string::npos);
    }
}
