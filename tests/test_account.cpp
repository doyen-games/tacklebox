#include <doctest/doctest.h>

#include "app/account_util.hpp"

using namespace tb;

TEST_CASE("account name validation") {
    bool premium = false;
    CHECK(acct::validateAccountName("myaccount123", &premium) == "");
    CHECK_FALSE(premium);
    CHECK(acct::validateAccountName("alice", &premium) == "");
    CHECK(premium);  // short = needs a bid/suffix
    CHECK(acct::validateAccountName("sub.acct1234", &premium) == "");
    CHECK(premium);  // dotted = suffix-owned
    CHECK_FALSE(acct::validateAccountName("").empty());
    CHECK_FALSE(acct::validateAccountName("Account12345").empty());  // uppercase
    CHECK_FALSE(acct::validateAccountName("account67890").empty());  // 6-9 and 0
    CHECK_FALSE(acct::validateAccountName("waytoolongname").empty());
    CHECK_FALSE(acct::validateAccountName(".leadingdot1").empty());
    CHECK_FALSE(acct::validateAccountName("trailingdot.").empty());
}

TEST_CASE("authority building sorts and validates") {
    // Known-valid K1 keys from dwarfkit's own crypto test vectors.
    const std::string keyA =
        "PUB_K1_6P8aGPEP79815rKGQ1dbc9eDxoEjatX7Lp696ve5tinnfwJ6nt";
    const std::string keyB =
        "PUB_K1_7Wp9pzhtTfN3jSyQDCktKLqxdTAcAfgT2RrVpE6KThZraa381H";

    acct::AuthorityDraft draft;
    draft.threshold = 2;
    draft.keys = {{keyB, 1}, {keyA, 1}};
    draft.accounts = {{"zzzzcosigner", "active", 1},
                      {"mycontract11", "eosio.code", 1},
                      {"mycontract11", "active", 1}};
    std::string why;
    auto auth = acct::makeAuthority(draft, &why);
    REQUIRE(auth.has_value());
    CHECK((*auth)["threshold"] == 2);
    // Keys ascending by canonical string.
    REQUIRE((*auth)["keys"].size() == 2);
    CHECK((*auth)["keys"][0]["key"].get<std::string>() <
          (*auth)["keys"][1]["key"].get<std::string>());
    // Accounts ascending by (actor, permission) name values; eosio.code rides
    // along like any other permission.
    REQUIRE((*auth)["accounts"].size() == 3);
    CHECK((*auth)["accounts"][0]["permission"]["actor"] == "mycontract11");
    CHECK((*auth)["accounts"][2]["permission"]["actor"] == "zzzzcosigner");
    CHECK((*auth)["waits"].is_array());

    // Failure paths.
    acct::AuthorityDraft empty;
    CHECK_FALSE(acct::makeAuthority(empty, &why).has_value());
    acct::AuthorityDraft badKey;
    badKey.keys = {{"not-a-key", 1}};
    CHECK_FALSE(acct::makeAuthority(badKey, &why).has_value());
    acct::AuthorityDraft dupe;
    dupe.keys = {{keyA, 1}, {keyA, 1}};
    CHECK_FALSE(acct::makeAuthority(dupe, &why).has_value());
    acct::AuthorityDraft unreachable;
    unreachable.threshold = 5;
    unreachable.keys = {{keyA, 1}};
    CHECK_FALSE(acct::makeAuthority(unreachable, &why).has_value());
    CHECK(why.find("threshold") != std::string::npos);
}

TEST_CASE("stake formatting follows the core symbol precision") {
    CHECK(acct::formatStake("1.5", "4,EOS") == "1.5000 EOS");
    CHECK(acct::formatStake("2", "8,WAX") == "2.00000000 WAX");
    CHECK(acct::formatStake("0", "4,EOS") == "0.0000 EOS");
    CHECK(acct::formatStake("1.5 EOS", "4,EOS") == "1.5000 EOS");  // symbol ok
    CHECK_FALSE(acct::formatStake("1.5 WAX", "4,EOS").has_value());  // wrong symbol
    CHECK_FALSE(acct::formatStake("-1", "4,EOS").has_value());
    CHECK_FALSE(acct::formatStake("abc", "4,EOS").has_value());
    CHECK_FALSE(acct::formatStake("1", "EOS").has_value());  // malformed symbol
}

TEST_CASE("stakeBreakdown: liquid + self + delegated + refunding sum up") {
    dwarfkit::json raw = {
        {"core_liquid_balance", "1234.5678 EOS"},
        {"voter_info", {{"staked", 2000000}}},  // 200.0000 EOS total stake
        {"self_delegated_bandwidth",
         {{"cpu_weight", "150.0000 EOS"}, {"net_weight", "10.0000 EOS"}}},
        {"refund_request",
         {{"cpu_amount", "3.0000 EOS"}, {"net_amount", "2.0000 EOS"}}}};
    auto b = tb::acct::stakeBreakdown(raw, "4,EOS");
    CHECK(b.any);
    CHECK(b.available == "1234.5678 EOS");
    CHECK(b.stakedSelf == "160.0000 EOS");
    CHECK(b.stakedDelegated == "40.0000 EOS");
    CHECK(b.refunding == "5.0000 EOS");
    CHECK(b.total == "1439.5678 EOS");
}

TEST_CASE("stakeBreakdown: voter_info.staked arrives as a string above 32 bits") {
    // nodeos quotes int64 fields past 32 bits, so on a precision-8 chain the
    // total stake is a string; the delegated-out delta must survive that.
    dwarfkit::json raw = {
        {"core_liquid_balance", "20740.80682753 WAX"},
        {"voter_info", {{"staked", "80000000000000"}}},  // 800000.00000000 WAX
        {"self_delegated_bandwidth",
         {{"cpu_weight", "618000.00000000 WAX"}, {"net_weight", "358.35761564 WAX"}}}};
    auto b = tb::acct::stakeBreakdown(raw, "8,WAX");
    CHECK(b.stakedSelf == "618358.35761564 WAX");
    CHECK(b.stakedDelegated == "181641.64238436 WAX");
    CHECK(b.total == "820740.80682753 WAX");
    // Anything that is not a whole number is ignored rather than mis-summed.
    raw["voter_info"]["staked"] = "lots";
    CHECK(tb::acct::stakeBreakdown(raw, "8,WAX").stakedDelegated == "0.00000000 WAX");
}

TEST_CASE("displayAsset trims the fraction without rounding") {
    CHECK(tb::acct::displayAsset("20740.80682753 WAX") == "20740.8068 WAX");
    CHECK(tb::acct::displayAsset("0.99999999 WAX") == "0.9999 WAX");
    CHECK(tb::acct::displayAsset("1234.5678 EOS") == "1234.5678 EOS");
    CHECK(tb::acct::displayAsset("1.5 EOS") == "1.5 EOS");
    CHECK(tb::acct::displayAsset("42 EOS") == "42 EOS");
    CHECK(tb::acct::displayAsset("-") == "-");
    CHECK(tb::acct::displayAsset("1.23456789 WAX", 0) == "1 WAX");
}

TEST_CASE("stakeBreakdown: no staking fields reports any=false") {
    dwarfkit::json raw = {{"created", "2021-01-01T00:00:00.000"}};
    auto b = tb::acct::stakeBreakdown(raw, "4,EOS");
    CHECK_FALSE(b.any);
    // Wrong-symbol assets are ignored rather than mis-summed.
    dwarfkit::json mixed = {{"core_liquid_balance", "5.0000 WAX"}};
    auto m = tb::acct::stakeBreakdown(mixed, "4,EOS");
    CHECK_FALSE(m.any);
    CHECK(m.total == "0.0000 EOS");
}
