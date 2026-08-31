// Port of transact-plugin-resource-provider test/tests/{filtering,plugin}.ts
// and session test/tests/plugins/transact/resource-provider.ts
#include <doctest/doctest.h>

#include <dwarfkit/plugins/transact/resource_provider.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

using Transfer = TransactPluginResourceProvider::Transfer;

std::shared_ptr<FetchProvider> pluginFetch() {
    return std::make_shared<MockFetchProvider>(DK_FIXTURE_DIR "/resource_provider/data");
}

Action makeFilterAction(const std::string& account) {
    const Transfer transfer{.from = Name::from("wharfkit1115"),
                            .to = Name::from("wharfkittest"),
                            .quantity = Asset::from("0.0001 EOS").value(),
                            .memo = "wharfkit plugin - resource provider filtering test"};
    Action action;
    action.account = Name::from(account);
    action.name = Name::from("transfer");
    action.authorization = {PermissionLevel{Name::from("wharfkit1115"), Name::from("test")}};
    action.data = Serializer::encode(transfer).value();
    return action;
}

Transaction makeFilterTransaction(const std::vector<Action>& actions) {
    Transaction tx;
    tx.expiration = TimePointSec(1700000000);
    tx.ref_block_num = 1;
    tx.ref_block_prefix = 2;
    tx.actions = actions;
    return tx;
}

bool sameAction(const Action& a, const Action& b) {
    return a.account == b.account && a.name == b.name && a.data == b.data;
}

Session makePluginSession(const std::string& permission,
                          const ResourceProviderOptions& options = {}) {
    SessionArgs args;
    args.chain = mockChainDefinition();
    args.permissionLevel = PermissionLevel::from(permission).value();
    args.walletPlugin = makeWallet();
    SessionOptions sessionOptions;
    sessionOptions.fetch = pluginFetch();
    sessionOptions.broadcast = false;
    sessionOptions.transactPlugins = {
        {std::make_shared<TransactPluginResourceProvider>(options)}};
    return Session(args, sessionOptions);
}

json makePluginAction(const std::string& actor, const std::string& memo) {
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

TEST_SUITE("resource-provider") {
    TEST_CASE("hasOriginalActions") {
        const Action mockAction = makeFilterAction("eosio.token");
        const Action newAction = makeFilterAction("eosio.foo");
        const Transaction mockTransaction = makeFilterTransaction({mockAction});
        SUBCASE("action exists alone") {
            CHECK(hasOriginalActions(mockTransaction, mockTransaction));
        }
        SUBCASE("action exists alongside others") {
            CHECK(hasOriginalActions(mockTransaction,
                                     makeFilterTransaction({mockAction, newAction})));
            CHECK(hasOriginalActions(
                mockTransaction,
                makeFilterTransaction({newAction, mockAction, newAction, newAction})));
        }
        SUBCASE("action doesn't exist") {
            CHECK_FALSE(hasOriginalActions(mockTransaction, makeFilterTransaction({newAction})));
            CHECK_FALSE(hasOriginalActions(
                mockTransaction, makeFilterTransaction({newAction, newAction, newAction})));
        }
    }

    TEST_CASE("getNewActions") {
        const Action mockAction = makeFilterAction("eosio.token");
        const Action newAction = makeFilterAction("eosio.foo");
        const Transaction mockTransaction = makeFilterTransaction({mockAction});
        SUBCASE("identical") {
            CHECK(getNewActions(mockTransaction, makeFilterTransaction({mockAction})).empty());
        }
        SUBCASE("appending action") {
            const auto diff =
                getNewActions(mockTransaction, makeFilterTransaction({mockAction, newAction}));
            REQUIRE(diff.size() == 1);
            CHECK(sameAction(diff[0], newAction));
        }
        SUBCASE("appending actions") {
            const auto diff = getNewActions(
                mockTransaction, makeFilterTransaction({mockAction, newAction, newAction}));
            REQUIRE(diff.size() == 2);
            CHECK(sameAction(diff[0], newAction));
            CHECK(sameAction(diff[1], newAction));
        }
        SUBCASE("prepending action") {
            const auto diff =
                getNewActions(mockTransaction, makeFilterTransaction({newAction, mockAction}));
            REQUIRE(diff.size() == 1);
            CHECK(sameAction(diff[0], newAction));
        }
    }

    TEST_CASE("provides free transaction for CPU and NET") {
        auto session = makePluginSession("wharfkit1113@test");
        const auto response =
            session
                .transact({.action = makePluginAction("wharfkit1113",
                                                      "wharfkit is the best... <3")})
                .value();
        REQUIRE(response.resolved.has_value());
        REQUIRE(response.transaction.has_value());
        REQUIRE(response.transaction->actions.size() == 2);
        // Ensure the noop action was properly prepended
        CHECK(response.transaction->actions[0].account == Name::from("greymassnoop"));
        CHECK(response.transaction->actions[0].authorization[0].actor ==
              Name::from("greymassfuel"));
        CHECK(response.transaction->actions[0].authorization[0].permission ==
              Name::from("cosign"));
        // Ensure the original transaction is still identical to the original
        const Transfer expected{.from = Name::from("wharfkit1113"),
                                .to = Name::from("wharfkittest"),
                                .quantity = Asset::from("0.0001 EOS").value(),
                                .memo = "wharfkit is the best... <3"};
        CHECK(response.resolved->transaction.actions[1].data ==
              Serializer::encode(expected).value());
    }

    TEST_CASE("fee-based transaction rejected with allowFees false") {
        auto session = makePluginSession("wharfkit1115@test", {.allowFees = false});
        const auto response =
            session
                .transact({.action = makePluginAction(
                               "wharfkit1115",
                               "wharfkit plugin - resource provider test (allowFees: false)")},
                          {.broadcast = false})
                .value();
        REQUIRE(response.resolved.has_value());
        REQUIRE(response.transaction.has_value());
        // the original action remains alone
        REQUIRE(response.transaction->actions.size() == 1);
        const Transfer expected{
            .from = Name::from("wharfkit1115"),
            .to = Name::from("wharfkittest"),
            .quantity = Asset::from("0.0001 EOS").value(),
            .memo = "wharfkit plugin - resource provider test (allowFees: false)"};
        CHECK(response.resolved->transaction.actions[0].data ==
              Serializer::encode(expected).value());
    }

    TEST_CASE("rejects fee-based transaction based on limit") {
        auto session = makePluginSession(
            "wharfkit1115@test",
            {.allowFees = true, .maxFee = Asset::from("0.0001 EOS").value()});
        const auto response =
            session
                .transact({.action = makePluginAction(
                               "wharfkit1115",
                               "wharfkit plugin - resource provider test (maxFee: 0.0001)")},
                          {.broadcast = false})
                .value();
        REQUIRE(response.transaction.has_value());
        REQUIRE(response.transaction->actions.size() == 1);
    }

    TEST_CASE("gracefully handles network failure when endpoint is unreachable") {
        ResourceProviderOptions options;
        options.endpoints = {{"73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d",
                              "https://unreachable.example.com"}};
        auto session = makePluginSession("wharfkit1113@test", options);
        const auto response =
            session
                .transact({.action = makePluginAction("wharfkit1113",
                                                      "wharfkit is the best... <3")},
                          {.broadcast = false})
                .value();
        REQUIRE(response.transaction.has_value());
        CHECK(response.transaction->actions.size() == 1);
    }
}
