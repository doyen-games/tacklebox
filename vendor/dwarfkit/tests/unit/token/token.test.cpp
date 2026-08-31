// Port of token test/tests/index.ts. Typed Serializer.decode assertions use
// the embedded ABI; the balance fixtures are re-keyed under current-antelope
// request bodies (see DIVERGENCES.md).
#include <doctest/doctest.h>

#include <dwarfkit/token.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

Token makeToken() {
    return Token({.client = makeClient(DK_FIXTURE_DIR "/token/data",
                                       "https://eos.greymass.com")});
}

}  // namespace

TEST_SUITE("token") {
    TEST_CASE("balance()") {
        const auto token = makeToken();
        SUBCASE("returns the system token balance of an account when no symbol code is passed") {
            const auto result = token.balance(Name::from("teamgreymass")).value();
            CHECK(result.symbol.code().toString() == "EOS");
        }
        SUBCASE("returns the balance of an account for a specific symbol") {
            const auto result = token.balance(Name::from("teamgreymass"), "EOS").value();
            CHECK(result.symbol.code().toString() == "EOS");
        }
        SUBCASE("returns the balance of an account for a specific symbol and contract") {
            const auto result =
                token.balance(Name::from("teamgreymass"), "USDT", Name::from("tethertether"))
                    .value();
            CHECK(result.symbol.code().toString() == "USDT");
        }
        SUBCASE("throws an error when the account does not exist") {
            const auto result = token.balance(Name::from("notanaccount"));
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().message ==
                  "Failed to fetch balance for notanaccount: Account notanaccount does not "
                  "exist.");
        }
        SUBCASE("throws an error when the symbol does not exist") {
            // a correct bounded query returns no row for an unheld symbol, so
            // the missing-row error fires (upstream's message assertion on
            // this case is vacuous, see DIVERGENCES.md)
            const auto result = token.balance(Name::from("teamgreymass"), "NOT");
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().message.starts_with(
                "Failed to fetch balance for teamgreymass:"));
        }
    }

    TEST_CASE("transfer()") {
        const auto token = makeToken();
        SUBCASE("creates a transfer action") {
            const auto action = token.transfer(Name::from("teamgreymass"),
                                               Name::from("teamgreymass"), "1.3200 EOS",
                                               "this is a test")
                                    .value();
            CHECK(action.account.toString() == "eosio.token");
            CHECK(action.name.toString() == "transfer");
            const auto decoded =
                Serializer::decode(action.data.array, "transfer", system_token::abi()).value();
            CHECK(decoded["from"] == "teamgreymass");
            CHECK(decoded["to"] == "teamgreymass");
            CHECK(decoded["quantity"] == "1.3200 EOS");
            CHECK(decoded["memo"] == "this is a test");
        }
    }

    TEST_CASE("system token contract") {
        CHECK(system_token::abi().version == "eosio::abi/1.1");
        const auto contract = system_token::contract(
            makeClient(DK_FIXTURE_DIR "/token/data", "https://eos.greymass.com"));
        CHECK(contract.account.toString() == "eosio.token");
        CHECK(contract.hasAction(Name::from("transfer")));
        CHECK(contract.hasTable(Name::from("accounts")));
    }
}
