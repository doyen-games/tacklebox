#include <doctest/doctest.h>

#include "app/autopilot_util.hpp"

using namespace tb::autopilot;
using dwarfkit::json;

TEST_CASE("computePercentAmount: basic percentages") {
    auto r = computePercentAmount("100.0000 WAX", 10.0, "");
    REQUIRE(r.has_value());
    CHECK(*r == "10.0000 WAX");

    r = computePercentAmount("100.0000 WAX", 100.0, "");
    REQUIRE(r.has_value());
    CHECK(*r == "100.0000 WAX");

    // Fractional percent via basis points: 12.5% of 100.
    r = computePercentAmount("100.0000 EOS", 12.5, "");
    REQUIRE(r.has_value());
    CHECK(*r == "12.5000 EOS");
}

TEST_CASE("computePercentAmount: floors, never rounds up") {
    // 33.33% of 0.0010 (10 units) -> 3 units, not 3.333.
    auto r = computePercentAmount("0.0010 EOS", 33.33, "");
    REQUIRE(r.has_value());
    CHECK(*r == "0.0003 EOS");
}

TEST_CASE("computePercentAmount: reserve is left untouched") {
    auto r = computePercentAmount("10.0000 WAX", 100.0, "4.0000 WAX");
    REQUIRE(r.has_value());
    CHECK(*r == "6.0000 WAX");

    // Reserve swallows the whole balance.
    CHECK_FALSE(computePercentAmount("3.0000 WAX", 50.0, "4.0000 WAX").has_value());
    // Reserve symbol must match.
    CHECK_FALSE(computePercentAmount("10.0000 WAX", 50.0, "1.0000 EOS").has_value());
    // Reserve precision must match.
    CHECK_FALSE(computePercentAmount("10.0000 WAX", 50.0, "1.00 WAX").has_value());
}

TEST_CASE("computePercentAmount: input validation") {
    CHECK_FALSE(computePercentAmount("not a balance", 10.0, "").has_value());
    CHECK_FALSE(computePercentAmount("100.0000 WAX", 0.0, "").has_value());
    CHECK_FALSE(computePercentAmount("100.0000 WAX", 101.0, "").has_value());
    // Zero-precision tokens format without a decimal point.
    auto r = computePercentAmount("1000 NFT", 25.0, "");
    REQUIRE(r.has_value());
    CHECK(*r == "250 NFT");
    // Tiny balance where the cut floors to nothing.
    CHECK_FALSE(computePercentAmount("0.0001 EOS", 1.0, "").has_value());
}

TEST_CASE("applyTemplates substitutes placeholders recursively") {
    TemplateContext context;
    context.actor = "alice";
    context.amount = "1.2345 WAX";
    context.balance = "10.0000 WAX";
    context.unixSec = 1735689600;  // 2025-01-01T00:00:00Z

    json data = {{"from", "{actor}"},
                 {"memo", "stacked {amount} of {balance} on {date} at {time}"},
                 {"nested", {{"who", "{actor}"}}},
                 {"list", json::array({"{actor}", 7})},
                 {"count", 42},
                 {"keep", "{unknown} stays"}};
    json out = applyTemplates(data, context);

    CHECK(out["from"] == "alice");
    CHECK(out["memo"] == "stacked 1.2345 WAX of 10.0000 WAX on 2025-01-01 at 00:00:00");
    CHECK(out["nested"]["who"] == "alice");
    CHECK(out["list"][0] == "alice");
    CHECK(out["list"][1] == 7);
    CHECK(out["count"] == 42);
    CHECK(out["keep"] == "{unknown} stays");
}
