// Port of account test/tests/kit.ts.
#include <doctest/doctest.h>

#include <dwarfkit/account.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

std::shared_ptr<APIClient> jungleClient() {
    return makeClient(DK_FIXTURE_DIR "/account/data", "https://jungle4.greymass.com");
}

}  // namespace

TEST_SUITE("account-kit") {
    TEST_CASE("constructor") {
        SUBCASE("sets client from chain definition provided") {
            const AccountKit<> kit(Chains::Jungle4());
            CHECK(kit.client != nullptr);
        }
        SUBCASE("allow overriding of default contract") {
            const AccountKit<> kit(Chains::Jungle4(),
                                   {.contract = system_contract::contract(jungleClient()),
                                    .client = jungleClient()});
            CHECK(kit.contract.has_value());
        }
    }

    TEST_CASE("load") {
        const AccountKit<> accountKit(Chains::Jungle4(), {.client = jungleClient()});
        SUBCASE("throws error if account does not exist") {
            const auto result = accountKit.load(Name::from("nonexistent"));
            CHECK_FALSE(result.has_value());
        }
        SUBCASE("returns an Account object when account exists") {
            const auto account = accountKit.load(Name::from("teamgreymass")).value();
            CHECK(account.accountName() == Name::from("teamgreymass"));
        }
        SUBCASE("returns the default account object type on EOS") {
            const AccountKit<> kit(
                Chains::EOS(),
                {.client = makeClient(DK_FIXTURE_DIR "/account/data",
                                      "https://eos.greymass.com")});
            const auto account = kit.load(Name::from("teamgreymass")).value();
            CHECK(account.data.voter_info.has_value());
        }
        SUBCASE("returns telos account type") {
            const AccountKit<TelosAccountObject> kit(
                Chains::Telos(),
                {.client = makeClient(DK_FIXTURE_DIR "/account/data",
                                      "https://telos.greymass.com")});
            const auto account = kit.load(Name::from("teamgreymass")).value();
            REQUIRE(account.data.voter_info.has_value());
            CHECK(account.data.voter_info->last_stake >= 0);
        }
        SUBCASE("returns wax account type") {
            const AccountKit<WAXAccountObject> kit(
                Chains::WAX(),
                {.client = makeClient(DK_FIXTURE_DIR "/account/data",
                                      "https://wax.greymass.com")});
            const auto account = kit.load(Name::from("teamgreymass")).value();
            REQUIRE(account.data.voter_info.has_value());
            CHECK(account.data.voter_info->unpaid_voteshare >= 0);
            CHECK(account.data.voter_info->unpaid_voteshare_change_rate >= 0);
        }
        SUBCASE("returns wax account type from custom definition") {
            const AccountKit<WAXAccountObject> kit(
                ChainDefinition::from(
                    {.id = Checksum256::from(
                               "1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4")
                               .value(),
                     .url = "https://wax.greymass.com"}),
                {.client = makeClient(DK_FIXTURE_DIR "/account/data",
                                      "https://wax.greymass.com")});
            const auto account = kit.load(Name::from("teamgreymass")).value();
            REQUIRE(account.data.voter_info.has_value());
            CHECK(account.data.voter_info->unpaid_voteshare >= 0);
        }
    }
}
