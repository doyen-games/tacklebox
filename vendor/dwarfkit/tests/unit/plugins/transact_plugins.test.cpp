// Ports of the cosigner, explorerlink, finality-callback and autocorrect
// plugin test suites (the checker ships no tests upstream; the autocorrect
// suites are fully commented out upstream, so construction and getException
// are covered here).
#include <doctest/doctest.h>

#include <dwarfkit/plugins/transact/autocorrect.hpp>
#include <dwarfkit/plugins/transact/cosigner.hpp>
#include <dwarfkit/plugins/transact/explorerlink.hpp>
#include <dwarfkit/plugins/transact/finality.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

Session makeSession(const char* fixtures, const std::string& permission,
                    std::vector<std::shared_ptr<AbstractTransactPlugin>> plugins,
                    const std::shared_ptr<UserInterface>& ui = nullptr) {
    SessionArgs args;
    args.chain = mockChainDefinition();
    args.permissionLevel = PermissionLevel::from(permission).value();
    args.walletPlugin = makeWallet();
    SessionOptions options;
    options.fetch = std::make_shared<MockFetchProvider>(std::string(DK_FIXTURE_DIR "/") + fixtures);
    options.transactPlugins = std::move(plugins);
    options.ui = ui;
    return Session(args, options);
}

json transferAction(const std::string& actor, const std::string& memo) {
    return json{{"authorization",
                 json::array({{{"actor", actor}, {"permission", "test"}}})},
                {"account", "eosio.token"},
                {"name", "transfer"},
                {"data",
                 {{"from", actor},
                  {"to", "wharfkittest"},
                  {"quantity", "0.0001 EOS"},
                  {"memo", memo}}}};
}

}  // namespace

TEST_SUITE("transact-plugins") {
    TEST_CASE("cosigner prepends action and signs transaction") {
        CosignerOptions options;
        options.actor = Name::from("wharfkitnoop");
        options.permission = Name::from("cosign");
        options.privateKey =
            PrivateKey::from("5JfFWg1CWsNTeXTWMyfChXXbyD31TCTknSVGwXDSpT6bPxKYLMM").value();
        auto session = makeSession("cosigner/data", "wharfkit1111@test",
                                   {std::make_shared<TransactPluginCosigner>(options)});
        const auto result =
            session.transact({.action = transferAction("wharfkit1111",
                                                       "wharfkit cosign plugin test")})
                .value();
        REQUIRE(result.transaction.has_value());
        CHECK(result.transaction->actions.size() == 2);
        CHECK(result.signatures.size() == 2);
        REQUIRE(result.response.has_value());
        CHECK((*result.response)["transaction_id"] ==
              "414ca0ffd8042b6d7f24ae1c62f652ac2fb33a1c12fd4db3b66489239e00c08a");
    }

    TEST_CASE("explorerlink plugin usage") {
        auto session = makeSession(
            "explorerlink/data", "wharfkit1111@test",
            {std::make_shared<TransactPluginExplorerLink>()});
        const auto result =
            session
                .transact({.action = transferAction(
                               "wharfkit1111",
                               "wharfkit plugin - resource provider test (maxFee: 0.0001)")},
                          {.broadcast = true})
                .value();
        CHECK(result.response.has_value());
    }

    TEST_CASE("explorerlink prompts with the explorer url when a ui is present") {
        const auto ui = std::make_shared<MockUserInterface>();
        auto session = makeSession("explorerlink/data", "wharfkit1111@test",
                                   {std::make_shared<TransactPluginExplorerLink>()}, ui);
        // give the chain an explorer definition so the link can be generated
        session.chain.explorer = ExplorerDefinition{"https://jungle4.eosq.eosnation.io/tx/", ""};
        const auto result =
            session
                .transact({.action = transferAction(
                               "wharfkit1111",
                               "wharfkit plugin - resource provider test (maxFee: 0.0001)")},
                          {.broadcast = true})
                .value();
        REQUIRE(result.response.has_value());
        const auto prompted =
            std::find_if(ui->messages.begin(), ui->messages.end(), [](const std::string& m) {
                return m.find("prompt: Transaction Complete") != std::string::npos;
            });
        CHECK(prompted != ui->messages.end());
    }

    TEST_CASE("finality callback calls onFinalityCallback when finality is reached") {
        bool called = false;
        FinalityCallbackOptions options;
        options.onFinalityCallback = [&](const api::v1::GetTransactionStatusResponse& status) {
            called = true;
            CHECK(status.state == "IRREVERSIBLE");
        };
        options.finalityCheckDelay = std::chrono::milliseconds(0);
        options.pollInterval = std::chrono::milliseconds(0);
        auto session = makeSession(
            "finality/data", "wharfkit1131@test",
            {std::make_shared<TransactPluginFinalityCallback>(options)});
        const auto result =
            session
                .transact({.action = transferAction(
                               "wharfkit1131",
                               "wharfkit plugin - resource provider test (maxFee: 0.0001)")},
                          {.broadcast = true})
                .value();
        CHECK(result.response.has_value());
        CHECK(called);
    }

    TEST_CASE("autocorrect getException") {
        CHECK(getException(json{{"error", {{"name", "tx_net_usage_exceeded"}}}})["name"] ==
              "tx_net_usage_exceeded");
        CHECK(getException(json{{"processed", {{"except", {{"name", "x"}}}}}})["name"] == "x");
        CHECK(getException(json{{"processed", {{"except", nullptr}}}}).is_null());
    }

    TEST_CASE("autocorrect requires a ui") {
        auto session = makeSession("finality/data", "wharfkit1131@test",
                                   {std::make_shared<TransactPluginAutoCorrect>()});
        const auto result = session.transact(
            {.action = transferAction("wharfkit1131", "autocorrect no ui")});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message.find("requires a UI") != std::string::npos);
    }
}
