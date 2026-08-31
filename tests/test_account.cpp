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
