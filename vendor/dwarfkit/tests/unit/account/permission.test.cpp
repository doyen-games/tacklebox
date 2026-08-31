// Port of account test/tests/permission.ts.
#include <doctest/doctest.h>

#include <dwarfkit/account.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

Permission loadActivePermission() {
    const AccountKit<> kit(Chains::Jungle4(),
                           {.client = makeClient(DK_FIXTURE_DIR "/account/data",
                                                 "https://jungle4.greymass.com")});
    const auto account = kit.load(Name::from(mockAccountName)).value();
    return account.permission(Name::from("active")).value();
}

}  // namespace

TEST_SUITE("account-permission") {
    TEST_CASE("construct") {
        SUBCASE("vanilla objects") {
            const auto permission = Permission::from(
                json{{"parent", "owner"},
                     {"perm_name", "active"},
                     {"required_auth",
                      {{"threshold", 1},
                       {"keys",
                        json::array(
                            {{{"key",
                               "PUB_K1_6XXTaRpWhPwnb7CTV9zVsCBrvCpYMMPSk8E8hsJxhf6V9t8aT5"},
                              {"weight", 1}}})},
                       {"accounts",
                        json::array({{{"permission",
                                       {{"actor", "foo"}, {"permission", "bar"}}},
                                      {"weight", 1}}})},
                       {"waits", json::array({{{"wait_sec", 600}, {"weight", 1}}})}}}});
            REQUIRE(permission.has_value());
            CHECK(permission->required_auth.keys.size() == 1);
            CHECK(permission->required_auth.accounts.size() == 1);
            CHECK(permission->required_auth.waits.size() == 1);
        }
        SUBCASE("empty") {
            const auto permission =
                Permission::from(json{{"parent", "owner"},
                                      {"perm_name", "active"},
                                      {"required_auth",
                                       {{"threshold", 1},
                                        {"keys", json::array()},
                                        {"accounts", json::array()},
                                        {"waits", json::array()}}}});
            CHECK(permission.has_value());
        }
        SUBCASE("recursion") {
            const auto permission = loadActivePermission();
            const auto copy = Permission::from(permission);
            CHECK(copy.perm_name == permission.perm_name);
        }
    }

    TEST_CASE("name (getter)") {
        CHECK(loadActivePermission().name().toString() == "active");
    }

    TEST_CASE("key-based authority") {
        auto testPermission = loadActivePermission();
        SUBCASE("addKey: add (string)") {
            CHECK(testPermission
                      .addKey("PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63")
                      .has_value());
            REQUIRE(testPermission.required_auth.keys.size() == 2);
            // Will be position 0 due to authority sorting
            CHECK(testPermission.required_auth.keys[0].key.equals(
                PublicKey::from("PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63")
                    .value()));
            CHECK(testPermission.required_auth.keys[0].weight.value == 1);
        }
        SUBCASE("addKey: add (string, legacy)") {
            const auto publicKey =
                PublicKey::from("PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63")
                    .value();
            CHECK(testPermission.addKey(publicKey.toLegacyString().value()).has_value());
            REQUIRE(testPermission.required_auth.keys.size() == 2);
            CHECK(testPermission.required_auth.keys[0].key.equals(publicKey));
        }
        SUBCASE("addKey: add (PublicKey)") {
            const auto publicKey =
                PublicKey::from("PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63")
                    .value();
            CHECK(testPermission.addKey(publicKey).has_value());
            CHECK(testPermission.required_auth.keys.size() == 2);
        }
        SUBCASE("addKey: add with weight") {
            const auto publicKey =
                PublicKey::from("PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63")
                    .value();
            CHECK(testPermission.addKey(publicKey, 10).has_value());
            REQUIRE(testPermission.required_auth.keys.size() == 2);
            CHECK(testPermission.required_auth.keys[0].weight.value == 10);
        }
        SUBCASE("addKey: prevent duplicate") {
            CHECK_FALSE(
                testPermission
                    .addKey("PUB_K1_6XXTaRpWhPwnb7CTV9zVsCBrvCpYMMPSk8E8hsJxhf6V9t8aT5")
                    .has_value());
        }
        SUBCASE("removeKey: remove") {
            CHECK(testPermission.required_auth.keys.size() == 1);
            CHECK(testPermission
                      .removeKey("PUB_K1_6XXTaRpWhPwnb7CTV9zVsCBrvCpYMMPSk8E8hsJxhf6V9t8aT5")
                      .has_value());
            CHECK(testPermission.required_auth.keys.empty());
        }
        SUBCASE("removeKey: throw if key not found") {
            CHECK_FALSE(
                testPermission
                    .removeKey("PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63")
                    .has_value());
        }
    }

    TEST_CASE("account-based authority") {
        auto testPermission = loadActivePermission();
        const auto trustLevel =
            PermissionLevel{Name::from("trust.gm"), Name::from("active")};
        SUBCASE("addAccount: add (object)") {
            CHECK(testPermission.addAccount(trustLevel).has_value());
            CHECK(testPermission.required_auth.keys.size() == 1);
            REQUIRE(testPermission.required_auth.accounts.size() == 1);
            CHECK(testPermission.required_auth.accounts[0].permission == trustLevel);
        }
        SUBCASE("addAccount: add (string)") {
            CHECK(testPermission.addAccount("trust.gm@active").has_value());
            REQUIRE(testPermission.required_auth.accounts.size() == 1);
            CHECK(testPermission.required_auth.accounts[0].permission == trustLevel);
        }
        SUBCASE("addAccount: prevent duplicates") {
            auto test = Permission::from(
                            json{{"parent", "owner"},
                                 {"perm_name", "active"},
                                 {"required_auth",
                                  {{"threshold", 1},
                                   {"keys", json::array()},
                                   {"accounts",
                                    json::array({{{"permission",
                                                   {{"actor", "foo"}, {"permission", "bar"}}},
                                                  {"weight", 1}}})},
                                   {"waits", json::array()}}}})
                            .value();
            CHECK_FALSE(
                test.addAccount(PermissionLevel{Name::from("foo"), Name::from("bar")})
                    .has_value());
        }
        SUBCASE("removeAccount: remove") {
            CHECK(testPermission.addAccount(trustLevel).has_value());
            CHECK(testPermission.removeAccount(trustLevel).has_value());
            CHECK(testPermission.required_auth.keys.size() == 1);
            CHECK(testPermission.required_auth.accounts.empty());
        }
        SUBCASE("removeAccount: throw if account not found") {
            CHECK_FALSE(testPermission.removeAccount(trustLevel).has_value());
        }
    }

    TEST_CASE("wait-based authority") {
        auto testPermission = loadActivePermission();
        SUBCASE("addWait") {
            CHECK(testPermission.required_auth.waits.empty());
            testPermission.addWait(WaitWeight{100, 1});
            REQUIRE(testPermission.required_auth.waits.size() == 1);
            CHECK(testPermission.required_auth.waits[0] == WaitWeight{100, 1});
        }
        SUBCASE("removeWait") {
            testPermission.addWait(WaitWeight{100, 1});
            CHECK(testPermission.required_auth.waits.size() == 1);
            testPermission.removeWait(WaitWeight{100, 1});
            CHECK(testPermission.required_auth.waits.empty());
        }
    }
}
