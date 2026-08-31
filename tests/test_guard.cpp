#include <doctest/doctest.h>

#include "guard/engine.hpp"
#include "guard/rules.hpp"

using namespace tb::guard;

static WhitelistRule baseRule() {
    WhitelistRule rule;
    rule.id = "rule-1";
    rule.chainId = "chainA";
    rule.signer = "alice@active";
    rule.contract = "eosio.token";
    rule.action = "transfer";
    return rule;
}

static ActionInput transferAction(const json& data) {
    ActionInput a;
    a.contract = "eosio.token";
    a.action = "transfer";
    a.data = data;
    return a;
}

TEST_CASE("jsonEquiv treats numeric strings and numbers as equal") {
    CHECK(jsonEquiv(json(100), json("100")));
    CHECK(jsonEquiv(json("100"), json(100)));
    CHECK(jsonEquiv(json(1.5), json("1.5")));
    CHECK_FALSE(jsonEquiv(json("alice"), json("bob")));
    CHECK(jsonEquiv(json("alice"), json("alice")));
    CHECK_FALSE(jsonEquiv(json("100x"), json(100)));
}

TEST_CASE("parseAsset") {
    auto a = parseAsset("1.0000 EOS");
    REQUIRE(a.has_value());
    CHECK(a->amount == 10000);
    CHECK(a->precision == 4);
    CHECK(a->code == "EOS");

    auto b = parseAsset("-0.5000 WAX");
    REQUIRE(b.has_value());
    CHECK(b->amount == -5000);

    CHECK_FALSE(parseAsset("EOS").has_value());
    CHECK_FALSE(parseAsset("1.0 eos").has_value());
    CHECK_FALSE(parseAsset("1..0 EOS").has_value());
}

TEST_CASE("constraints") {
    std::string why;

    SUBCASE("exact") {
        ParamConstraint c;
        c.kind = ConstraintKind::Exact;
        c.values = {json("bob")};
        CHECK(c.check(json("bob"), why));
        CHECK_FALSE(c.check(json("eve"), why));
        CHECK(why.find("expected") != std::string::npos);
    }
    SUBCASE("one-of") {
        ParamConstraint c;
        c.kind = ConstraintKind::OneOf;
        c.values = {json("bob"), json("carol")};
        CHECK(c.check(json("carol"), why));
        CHECK_FALSE(c.check(json("eve"), why));
    }
    SUBCASE("numeric range") {
        ParamConstraint c;
        c.kind = ConstraintKind::Range;
        c.min = json(10);
        c.max = json(100);
        CHECK(c.check(json(10), why));
        CHECK(c.check(json(100), why));
        CHECK(c.check(json("55"), why));
        CHECK_FALSE(c.check(json(9), why));
        CHECK_FALSE(c.check(json(101), why));
    }
    SUBCASE("asset range") {
        ParamConstraint c;
        c.kind = ConstraintKind::Range;
        c.max = json("10.0000 EOS");
        CHECK(c.check(json("9.9999 EOS"), why));
        CHECK(c.check(json("10.0000 EOS"), why));
        CHECK_FALSE(c.check(json("10.0001 EOS"), why));
        // Symbol mismatch never passes.
        CHECK_FALSE(c.check(json("1.0000 WAX"), why));
        CHECK(why.find("symbol") != std::string::npos);
    }
    SUBCASE("open-ended range") {
        ParamConstraint c;
        c.kind = ConstraintKind::Range;
        c.min = json(0);
        CHECK(c.check(json(1000000), why));
        CHECK_FALSE(c.check(json(-1), why));
    }
}

TEST_CASE("identity matching with wildcards") {
    WhitelistRule rule = baseRule();
    CHECK(rule.matchesIdentity("chainA", "alice", "active", "eosio.token", "transfer"));
    CHECK_FALSE(rule.matchesIdentity("chainB", "alice", "active", "eosio.token", "transfer"));
    CHECK_FALSE(rule.matchesIdentity("chainA", "bob", "active", "eosio.token", "transfer"));
    CHECK_FALSE(rule.matchesIdentity("chainA", "alice", "owner", "eosio.token", "transfer"));

    rule.signer = "*@*";
    rule.chainId = "*";
    rule.action = "*";
    CHECK(rule.matchesIdentity("chainB", "bob", "owner", "eosio.token", "anything"));
    // The contract itself is never wildcarded.
    CHECK_FALSE(rule.matchesIdentity("chainB", "bob", "owner", "other", "anything"));
}

TEST_CASE("engine: trusted verdict with constraints") {
    WhitelistRule rule = baseRule();
    ParamConstraint to;
    to.kind = ConstraintKind::Exact;
    to.values = {json("bob")};
    ParamConstraint quantity;
    quantity.kind = ConstraintKind::Range;
    quantity.max = json("5.0000 EOS");
    rule.params = {{"to", to}, {"quantity", quantity}};

    auto actions = std::vector<ActionInput>{transferAction(
        {{"from", "alice"}, {"to", "bob"}, {"quantity", "1.0000 EOS"}, {"memo", ""}})};

    auto eval = evaluate({rule}, "chainA", "alice", "active", actions);
    REQUIRE(eval.actions.size() == 1);
    CHECK(eval.actions[0].level == VerdictLevel::Trusted);
    CHECK(eval.overall == VerdictLevel::Trusted);
    CHECK(eval.usedRuleIds == std::vector<std::string>{"rule-1"});

    // Out-of-range amount fails with an explanation.
    actions[0].data["quantity"] = "9.0000 EOS";
    eval = evaluate({rule}, "chainA", "alice", "active", actions);
    CHECK(eval.actions[0].level == VerdictLevel::ConstraintFail);
    CHECK(eval.actions[0].detail.find("quantity") != std::string::npos);
}

TEST_CASE("engine: pin mismatch blocks and reports observation") {
    WhitelistRule rule = baseRule();
    rule.pin = ContractPin{"codeAA", "abiAA", 0};
    rule.autoSign = true;

    auto actions = std::vector<ActionInput>{transferAction({{"to", "bob"}})};
    actions[0].codeHash = "codeAA";
    actions[0].abiHash = "abiAA";

    auto eval = evaluate({rule}, "chainA", "alice", "active", actions);
    CHECK(eval.actions[0].level == VerdictLevel::TrustedAuto);
    CHECK(eval.stale.empty());

    SUBCASE("code hash changed") {
        actions[0].codeHash = "codeBB";
        eval = evaluate({rule}, "chainA", "alice", "active", actions);
        CHECK(eval.actions[0].level == VerdictLevel::StalePin);
        CHECK(eval.overall == VerdictLevel::StalePin);
        REQUIRE(eval.stale.size() == 1);
        CHECK(eval.stale[0].ruleId == "rule-1");
        CHECK(eval.stale[0].observedCodeHash == "codeBB");
    }
    SUBCASE("hashes unavailable: no fast path") {
        actions[0].codeHash.clear();
        actions[0].abiHash.clear();
        eval = evaluate({rule}, "chainA", "alice", "active", actions);
        CHECK(eval.actions[0].level == VerdictLevel::StalePin);
        CHECK(eval.stale.empty());  // nothing observed, nothing to persist
    }
    SUBCASE("persisted-stale rule stays blocked even when hashes match again") {
        rule.status = RuleStatus::Stale;
        eval = evaluate({rule}, "chainA", "alice", "active", actions);
        CHECK(eval.actions[0].level == VerdictLevel::StalePin);
        CHECK(eval.actions[0].detail.find("re-approval") != std::string::npos);
    }
}

TEST_CASE("engine: unlisted and disabled") {
    WhitelistRule rule = baseRule();

    auto other = std::vector<ActionInput>{{"other.contract", "doit", json::object(), "", ""}};
    auto eval = evaluate({rule}, "chainA", "alice", "active", other);
    CHECK(eval.actions[0].level == VerdictLevel::Unlisted);

    rule.status = RuleStatus::Disabled;
    auto actions = std::vector<ActionInput>{transferAction({{"to", "bob"}})};
    eval = evaluate({rule}, "chainA", "alice", "active", actions);
    CHECK(eval.actions[0].level == VerdictLevel::Unlisted);
}

TEST_CASE("engine: strict rules reject unlisted fields") {
    WhitelistRule rule = baseRule();
    rule.unlistedParamsAny = false;
    ParamConstraint to;
    to.kind = ConstraintKind::Exact;
    to.values = {json("bob")};
    rule.params = {{"to", to}};

    auto actions = std::vector<ActionInput>{transferAction({{"to", "bob"}, {"memo", "x"}})};
    auto eval = evaluate({rule}, "chainA", "alice", "active", actions);
    CHECK(eval.actions[0].level == VerdictLevel::ConstraintFail);
    CHECK(eval.actions[0].detail.find("memo") != std::string::npos);
}

TEST_CASE("engine: weakest action dominates the overall verdict") {
    WhitelistRule rule = baseRule();

    std::vector<ActionInput> actions = {
        transferAction({{"to", "bob"}}),
        {"unknown.acct", "doit", json::object(), "", ""},
    };
    auto eval = evaluate({rule}, "chainA", "alice", "active", actions);
    CHECK(eval.actions[0].level == VerdictLevel::Trusted);
    CHECK(eval.actions[1].level == VerdictLevel::Unlisted);
    CHECK(eval.overall == VerdictLevel::Unlisted);
}

TEST_CASE("engine: specific action rule outranks wildcard") {
    WhitelistRule wildcard = baseRule();
    wildcard.id = "wild";
    wildcard.action = "*";
    wildcard.autoSign = false;

    WhitelistRule specific = baseRule();
    specific.id = "spec";
    specific.autoSign = true;

    auto actions = std::vector<ActionInput>{transferAction({{"to", "bob"}})};
    auto eval = evaluate({wildcard, specific}, "chainA", "alice", "active", actions);
    CHECK(eval.actions[0].ruleId == "spec");
    CHECK(eval.actions[0].level == VerdictLevel::TrustedAuto);
}

TEST_CASE("dotted field paths") {
    json data = {{"payload", {{"target", {{"name", "bob"}}}}}};
    const json* v = lookupField(data, "payload.target.name");
    REQUIRE(v != nullptr);
    CHECK(*v == json("bob"));
    CHECK(lookupField(data, "payload.missing") == nullptr);
    CHECK(lookupField(data, "payload.target.name.deeper") == nullptr);
}

TEST_CASE("rule JSON round trip") {
    WhitelistRule rule = baseRule();
    rule.pin = ContractPin{"cc", "aa", 1234};
    rule.autoSign = true;
    rule.status = RuleStatus::Stale;
    rule.observedCodeHash = "newcode";
    ParamConstraint c;
    c.kind = ConstraintKind::Range;
    c.min = json("0.0000 EOS");
    c.max = json("2.0000 EOS");
    rule.params["quantity"] = c;

    auto parsed = WhitelistRule::fromJSON(rule.toJSON());
    REQUIRE(parsed.has_value());
    CHECK(parsed->id == rule.id);
    CHECK(parsed->status == RuleStatus::Stale);
    CHECK(parsed->autoSign);
    REQUIRE(parsed->pin.has_value());
    CHECK(parsed->pin->codeHash == "cc");
    CHECK(parsed->observedCodeHash == "newcode");
    REQUIRE(parsed->params.count("quantity"));
    CHECK(parsed->params["quantity"].kind == ConstraintKind::Range);
    CHECK(*parsed->params["quantity"].max == json("2.0000 EOS"));
}
