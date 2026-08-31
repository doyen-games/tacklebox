// Port of account test/tests/account.ts.
#include <doctest/doctest.h>

#include <dwarfkit/account.hpp>
#include <dwarfkit/session.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

constexpr const char* testAccountName = "wharfkit1133";

std::shared_ptr<APIClient> accountClient() {
    return makeClient(DK_FIXTURE_DIR "/account/data", "https://jungle4.greymass.com");
}

AccountKit<> makeKit() {
    return AccountKit<>(Chains::Jungle4(), {.client = accountClient()});
}

Account<> loadTestAccount() {
    return makeKit().load(Name::from(testAccountName)).value();
}

Session makeSession() {
    SessionArgs args = mockSessionArgs();
    args.permissionLevel = PermissionLevel{Name::from(testAccountName), "active"_n};
    SessionOptions options;
    options.broadcast = true;
    options.fetch = std::make_shared<MockFetchProvider>(DK_FIXTURE_DIR "/account/data");
    return Session(args, options);
}

json decodeAction(const Action& action, const std::string& type) {
    return Serializer::decode(action.data.array, type, system_contract::abi()).value();
}

}  // namespace

TEST_SUITE("account") {
    TEST_CASE("construct") {
        const auto testAccount = loadTestAccount();
        const Account<> account({.client = accountClient(), .data = testAccount.data});
        CHECK(account.accountName() == Name::from("wharfkit1133"));
    }

    TEST_CASE("data") {
        const auto testAccount = loadTestAccount();
        CHECK(testAccount.data.account_name == Name::from("wharfkit1133"));
        CHECK(!testAccount.data.permissions.empty());
    }

    TEST_CASE("permission") {
        const auto testAccount = loadTestAccount();
        SUBCASE("returns permission object") {
            CHECK(testAccount.permission(Name::from("active")).has_value());
        }
        SUBCASE("throws error when permission does not exist") {
            const auto result = testAccount.permission(Name::from("nonexistent"));
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().message ==
                  "Permission nonexistent does not exist on account wharfkit1133.");
        }
    }

    TEST_CASE("resource") {
        const auto testAccount = loadTestAccount();
        SUBCASE("cpu") {
            const auto resources = testAccount.resource(ResourceType::cpu);
            CHECK(resources.max > 0);
            CHECK(resources.weight.has_value());
        }
        SUBCASE("net") {
            const auto resources = testAccount.resource(ResourceType::net);
            CHECK(resources.max > 0);
            CHECK(resources.weight.has_value());
        }
        SUBCASE("ram") {
            const auto resources = testAccount.resource(ResourceType::ram);
            CHECK(resources.max > 0);
            CHECK(resources.available == resources.max - resources.used);
            CHECK_FALSE(resources.weight.has_value());
        }
    }

    TEST_CASE("setPermission") {
        const auto testAccount = loadTestAccount();
        SUBCASE("basic syntax") {
            const auto permission =
                Permission::from(json{{"parent", "active"},
                                      {"perm_name", "foo"},
                                      {"required_auth",
                                       {{"accounts", json::array()},
                                        {"keys", json::array()},
                                        {"threshold", 1},
                                        {"waits", json::array()}}}})
                    .value();
            const auto action = testAccount.setPermission(permission).value();
            CHECK(action.account == Name::from("eosio"));
            CHECK(action.name == Name::from("updateauth"));
            CHECK(action.authorization[0] == PlaceholderAuth);
            const auto decoded = decodeAction(action, "updateauth");
            CHECK(decoded["account"] == "wharfkit1133");
            CHECK(decoded["parent"] == "active");
            CHECK(decoded["permission"] == "foo");
            CHECK(decoded["auth"]["threshold"] == 1);
        }
        SUBCASE("create new permission") {
            auto permission = Permission::from(json{{"parent", "active"},
                                                    {"perm_name", "unittest"},
                                                    {"required_auth",
                                                     {{"accounts", json::array()},
                                                      {"keys", json::array()},
                                                      {"threshold", 1},
                                                      {"waits", json::array()}}}})
                                  .value();
            CHECK(permission
                      .addKey("PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63")
                      .has_value());
            const auto action = testAccount.setPermission(permission).value();
            CHECK(action.name == Name::from("updateauth"));
            CHECK(action.account == Name::from("eosio"));
            CHECK(action.authorization[0] == PlaceholderAuth);
        }
        SUBCASE("remove permission") {
            const auto action = testAccount.removePermission(Name::from("unittest")).value();
            CHECK(action.account == Name::from("eosio"));
            CHECK(action.name == Name::from("deleteauth"));
            CHECK(action.authorization[0] == PlaceholderAuth);
            const auto decoded = decodeAction(action, "deleteauth");
            CHECK(decoded["account"] == "wharfkit1133");
            CHECK(decoded["permission"] == "unittest");
        }
        SUBCASE("modify existing: adding key") {
            auto permission = testAccount.permission(Name::from("active")).value();
            const std::string originalKey =
                "EOS6RMS3nvoN9StPzZizve6WdovaDkE5KkEcCDXW7LbepyAioMiK6";
            CHECK(permission
                      .addKey("PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63")
                      .has_value());
            const auto action = testAccount.setPermission(permission).value();
            CHECK(action.account == Name::from("eosio"));
            CHECK(action.name == Name::from("updateauth"));
            CHECK(action.authorization[0] == PlaceholderAuth);
            const auto decoded = decodeAction(action, "updateauth");
            CHECK(decoded["account"] == "wharfkit1133");
            CHECK(decoded["parent"] == "owner");
            CHECK(decoded["permission"] == "active");
            // The keys will be reordered due to sorting requirements
            CHECK(decoded["auth"]["keys"][0]["key"] ==
                  "PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63");
            CHECK(PublicKey::from(decoded["auth"]["keys"][1]["key"].get<std::string>())
                      .value()
                      .equals(PublicKey::from(originalKey).value()));
            CHECK(decoded["auth"]["keys"][1]["weight"] == 1);

            auto session = makeSession();
            const auto result =
                session.transact({.action = Serializer::objectify(action)});
            CHECK(result.has_value());
        }
        SUBCASE("modify existing: remove key") {
            auto permission = testAccount.permission(Name::from("active")).value();
            const std::string originalKey =
                "EOS6RMS3nvoN9StPzZizve6WdovaDkE5KkEcCDXW7LbepyAioMiK6";
            // It needs to be added here because the cached record doesn't
            // have it; it exists on-chain from the previous test.
            permission.required_auth.keys.push_back(KeyWeight{
                PublicKey::from("PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63")
                    .value(),
                1});
            permission.required_auth.sort();
            CHECK(permission
                      .removeKey("PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63")
                      .has_value());
            const auto action = testAccount.setPermission(permission).value();
            const auto decoded = decodeAction(action, "updateauth");
            CHECK(decoded["parent"] == "owner");
            CHECK(decoded["permission"] == "active");
            CHECK(PublicKey::from(decoded["auth"]["keys"][0]["key"].get<std::string>())
                      .value()
                      .equals(PublicKey::from(originalKey).value()));
            CHECK(decoded["auth"]["keys"][0]["weight"] == 1);

            auto session = makeSession();
            const auto result =
                session.transact({.action = Serializer::objectify(action)});
            CHECK(result.has_value());
        }
    }

    TEST_CASE("linkauth") {
        const auto testAccount = loadTestAccount();
        const auto action =
            testAccount.linkauth(Name::from("eosio.token"), Name::from("transfer"),
                                 Name::from("active"))
                .value();
        CHECK(action.account == Name::from("eosio"));
        CHECK(action.name == Name::from("linkauth"));
        CHECK(action.authorization[0] == PlaceholderAuth);
        const auto decoded = decodeAction(action, "linkauth");
        CHECK(decoded["account"] == "wharfkit1133");
        CHECK(decoded["code"] == "eosio.token");
        CHECK(decoded["type"] == "transfer");
        CHECK(decoded["requirement"] == "active");
    }

    TEST_CASE("unlinkauth") {
        const auto testAccount = loadTestAccount();
        const auto action =
            testAccount.unlinkauth(Name::from("eosio.token"), Name::from("transfer")).value();
        CHECK(action.account == Name::from("eosio"));
        CHECK(action.name == Name::from("unlinkauth"));
        CHECK(action.authorization[0] == PlaceholderAuth);
        const auto decoded = decodeAction(action, "unlinkauth");
        CHECK(decoded["account"] == "wharfkit1133");
        CHECK(decoded["code"] == "eosio.token");
        CHECK(decoded["type"] == "transfer");
    }

    TEST_CASE("buyRam") {
        const auto testAccount = loadTestAccount();
        SUBCASE("only amount") {
            const auto action =
                testAccount.buyRam(Asset::from("1.0000 EOS").value()).value();
            CHECK(action.account == Name::from("eosio"));
            CHECK(action.name == Name::from("buyram"));
            CHECK(action.authorization[0] == PlaceholderAuth);
            const auto decoded = decodeAction(action, "buyram");
            CHECK(decoded["payer"] == "wharfkit1133");
            CHECK(decoded["receiver"] == "wharfkit1133");
            CHECK(decoded["quant"] == "1.0000 EOS");
        }
        SUBCASE("override receiver") {
            const auto action = testAccount
                                    .buyRam(Asset::from("1.0000 EOS").value(),
                                            {.receiver = Name::from("wharfkit1112")})
                                    .value();
            const auto decoded = decodeAction(action, "buyram");
            CHECK(decoded["payer"] == "wharfkit1133");
            CHECK(decoded["receiver"] == "wharfkit1112");
            CHECK(decoded["quant"] == "1.0000 EOS");
        }
    }

    TEST_CASE("buyRamBytes") {
        const auto testAccount = loadTestAccount();
        SUBCASE("only bytes") {
            const auto action = testAccount.buyRamBytes(1024).value();
            CHECK(action.account == Name::from("eosio"));
            CHECK(action.name == Name::from("buyrambytes"));
            CHECK(action.authorization[0] == PlaceholderAuth);
            const auto decoded = decodeAction(action, "buyrambytes");
            CHECK(decoded["payer"] == "wharfkit1133");
            CHECK(decoded["receiver"] == "wharfkit1133");
            CHECK(decoded["bytes"] == 1024);
        }
        SUBCASE("override receiver") {
            const auto action =
                testAccount.buyRamBytes(1024, {.receiver = Name::from("wharfkit1112")}).value();
            const auto decoded = decodeAction(action, "buyrambytes");
            CHECK(decoded["payer"] == "wharfkit1133");
            CHECK(decoded["receiver"] == "wharfkit1112");
            CHECK(decoded["bytes"] == 1024);
        }
    }

    TEST_CASE("sellRam") {
        const auto testAccount = loadTestAccount();
        const auto action = testAccount.sellRam(1024).value();
        CHECK(action.account == Name::from("eosio"));
        CHECK(action.name == Name::from("sellram"));
        CHECK(action.authorization[0] == PlaceholderAuth);
        const auto decoded = decodeAction(action, "sellram");
        CHECK(decoded["account"] == "wharfkit1133");
        CHECK(decoded["bytes"] == 1024);
    }

    TEST_CASE("delegate") {
        const auto testAccount = loadTestAccount();
        SUBCASE("no data") {
            const auto action = testAccount.delegate({}).value();
            CHECK(action.account == Name::from("eosio"));
            CHECK(action.name == Name::from("delegatebw"));
            CHECK(action.authorization[0] == PlaceholderAuth);
            const auto decoded = decodeAction(action, "delegatebw");
            CHECK(decoded["from"] == "wharfkit1133");
            CHECK(decoded["receiver"] == "wharfkit1133");
            CHECK(decoded["stake_cpu_quantity"] == "0.0000 EOS");
            CHECK(decoded["stake_net_quantity"] == "0.0000 EOS");
            CHECK(decoded["transfer"] == false);
        }
        SUBCASE("cpu only") {
            const auto action =
                testAccount.delegate({.cpu = Asset::from("1.0000 EOS").value()}).value();
            const auto decoded = decodeAction(action, "delegatebw");
            CHECK(decoded["stake_cpu_quantity"] == "1.0000 EOS");
            CHECK(decoded["stake_net_quantity"] == "0.0000 EOS");
            CHECK(decoded["transfer"] == false);
        }
        SUBCASE("net only") {
            const auto action =
                testAccount.delegate({.net = Asset::from("1.0000 EOS").value()}).value();
            const auto decoded = decodeAction(action, "delegatebw");
            CHECK(decoded["stake_cpu_quantity"] == "0.0000 EOS");
            CHECK(decoded["stake_net_quantity"] == "1.0000 EOS");
        }
        SUBCASE("cpu and net") {
            const auto action = testAccount
                                    .delegate({.cpu = Asset::from("1.0000 EOS").value(),
                                               .net = Asset::from("0.5000 EOS").value()})
                                    .value();
            const auto decoded = decodeAction(action, "delegatebw");
            CHECK(decoded["stake_cpu_quantity"] == "1.0000 EOS");
            CHECK(decoded["stake_net_quantity"] == "0.5000 EOS");
        }
        SUBCASE("override receiver and enable transfer") {
            const auto action = testAccount
                                    .delegate({.receiver = Name::from("wharfkit1112"),
                                               .cpu = Asset::from("1.0000 EOS").value(),
                                               .net = Asset::from("0.5000 EOS").value(),
                                               .transfer = true})
                                    .value();
            const auto decoded = decodeAction(action, "delegatebw");
            CHECK(decoded["from"] == "wharfkit1133");
            CHECK(decoded["receiver"] == "wharfkit1112");
            CHECK(decoded["transfer"] == true);
        }
    }

    TEST_CASE("undelegate") {
        const auto testAccount = loadTestAccount();
        SUBCASE("no data") {
            const auto action = testAccount.undelegate({}).value();
            CHECK(action.account == Name::from("eosio"));
            CHECK(action.name == Name::from("undelegatebw"));
            CHECK(action.authorization[0] == PlaceholderAuth);
            const auto decoded = decodeAction(action, "undelegatebw");
            CHECK(decoded["from"] == "wharfkit1133");
            CHECK(decoded["receiver"] == "wharfkit1133");
            CHECK(decoded["unstake_cpu_quantity"] == "0.0000 EOS");
            CHECK(decoded["unstake_net_quantity"] == "0.0000 EOS");
        }
        SUBCASE("cpu and net") {
            const auto action = testAccount
                                    .undelegate({.cpu = Asset::from("1.0000 EOS").value(),
                                                 .net = Asset::from("0.5000 EOS").value()})
                                    .value();
            const auto decoded = decodeAction(action, "undelegatebw");
            CHECK(decoded["unstake_cpu_quantity"] == "1.0000 EOS");
            CHECK(decoded["unstake_net_quantity"] == "0.5000 EOS");
        }
        SUBCASE("override receiver") {
            const auto action = testAccount
                                    .undelegate({.receiver = Name::from("wharfkit1112"),
                                                 .cpu = Asset::from("1.0000 EOS").value(),
                                                 .net = Asset::from("0.5000 EOS").value()})
                                    .value();
            const auto decoded = decodeAction(action, "undelegatebw");
            CHECK(decoded["receiver"] == "wharfkit1112");
        }
    }

    TEST_CASE("balance") {
        const auto testAccount = loadTestAccount();
        SUBCASE("returns system token balance when no params are passed") {
            const auto balance = testAccount.balance().value();
            CHECK(balance.symbol.code().toString() == "EOS");
        }
        SUBCASE("returns proper balance when symbol is passed") {
            const auto balance = testAccount.balance("EOS").value();
            CHECK(balance.symbol.code().toString() == "EOS");
        }
        SUBCASE("returns proper balance when symbol and contract name are passed") {
            const auto balance =
                testAccount.balance("EOS", Name::from("eosio.token")).value();
            CHECK(balance.symbol.code().toString() == "EOS");
        }
    }
}
