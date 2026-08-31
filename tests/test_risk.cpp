#include <doctest/doctest.h>

#include "guard/risk.hpp"

using namespace tb::guard;
using dwarfkit::json;

static bool hasFlag(const std::vector<RiskFlag>& flags, const char* code,
                    RiskSeverity severity) {
    for (const auto& f : flags)
        if (f.code == code && f.severity == severity) return true;
    return false;
}

TEST_CASE("permission changes and code deploys are critical") {
    RiskContext ctx;
    ctx.signerActor = "alice";
    std::vector<RiskActionInput> actions = {
        {"eosio", "updateauth", {{"account", "alice"}, {"permission", "active"}}},
        {"eosio", "setcode", {{"account", "somecontract"}}},
    };
    auto flags = analyze(actions, ctx);
    CHECK(hasFlag(flags, "perm-change", RiskSeverity::Critical));
    CHECK(hasFlag(flags, "code-deploy", RiskSeverity::Critical));
}

TEST_CASE("drain and large transfers") {
    RiskContext ctx;
    ctx.signerActor = "alice";
    ctx.coreBalance = "100.0000 EOS";

    std::vector<RiskActionInput> actions = {
        {"eosio.token", "transfer",
         {{"from", "alice"}, {"to", "bob"}, {"quantity", "95.0000 EOS"}, {"memo", ""}}}};
    CHECK(hasFlag(analyze(actions, ctx), "drain-transfer", RiskSeverity::Critical));

    actions[0].data["quantity"] = "60.0000 EOS";
    CHECK(hasFlag(analyze(actions, ctx), "large-transfer", RiskSeverity::Warn));

    actions[0].data["quantity"] = "1.0000 EOS";
    auto flags = analyze(actions, ctx);
    CHECK_FALSE(hasFlag(flags, "large-transfer", RiskSeverity::Warn));
    CHECK_FALSE(hasFlag(flags, "drain-transfer", RiskSeverity::Critical));

    // Somebody else's transfer of their funds is not our drain.
    actions[0].data["from"] = "carol";
    actions[0].data["quantity"] = "95.0000 EOS";
    CHECK_FALSE(hasFlag(analyze(actions, ctx), "drain-transfer", RiskSeverity::Critical));
}

TEST_CASE("phishing-style memos") {
    RiskContext ctx;
    ctx.signerActor = "alice";
    std::vector<RiskActionInput> actions = {
        {"eosio.token", "transfer",
         {{"from", "alice"},
          {"to", "bob"},
          {"quantity", "0.0001 EOS"},
          {"memo", "Visit https://evil.example to CLAIM your airdrop"}}}};
    CHECK(hasFlag(analyze(actions, ctx), "memo-phish", RiskSeverity::Warn));
}

TEST_CASE("first interaction with an unknown contract") {
    RiskContext ctx;
    ctx.signerActor = "alice";
    ctx.knownContracts = {"eosio.token", "known.dapp"};
    std::vector<RiskActionInput> actions = {{"mystery.dapp", "doit", json::object()}};
    CHECK(hasFlag(analyze(actions, ctx), "first-contact", RiskSeverity::Info));

    actions[0].contract = "known.dapp";
    CHECK_FALSE(hasFlag(analyze(actions, ctx), "first-contact", RiskSeverity::Info));
}

TEST_CASE("critical flags sort first") {
    RiskContext ctx;
    ctx.signerActor = "alice";
    std::vector<RiskActionInput> actions = {
        {"eosio.msig", "propose", json::object()},
        {"eosio", "updateauth", json::object()},
    };
    auto flags = analyze(actions, ctx);
    REQUIRE(flags.size() >= 2);
    CHECK(flags[0].severity == RiskSeverity::Critical);
}
