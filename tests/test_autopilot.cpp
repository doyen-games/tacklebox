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

// --- schedule timing ---------------------------------------------------------

static tb::Schedule sched(int mode, int64_t interval) {
    tb::Schedule s;
    s.timingMode = mode;
    s.intervalSec = interval;
    return s;
}

TEST_CASE("parseTimeOfDay accepts HH:MM[:SS], rejects junk") {
    auto t = parseTimeOfDay("09:30");
    REQUIRE(t.has_value());
    CHECK(*t == 9 * 3600 + 30 * 60);
    t = parseTimeOfDay("23:59:59");
    REQUIRE(t.has_value());
    CHECK(*t == 23 * 3600 + 59 * 60 + 59);
    CHECK_FALSE(parseTimeOfDay("24:00").has_value());
    CHECK_FALSE(parseTimeOfDay("nope").has_value());
    CHECK_FALSE(parseTimeOfDay("12:61").has_value());
}

TEST_CASE("parseDateTimeLocal round-trips through the formatter") {
    auto at = parseDateTimeLocal("2030-05-17 14:03:21");
    REQUIRE(at.has_value());
    CHECK(formatDateTimeLocal(*at) == "2030-05-17 14:03:21");
    // Seconds optional.
    auto noSec = parseDateTimeLocal("2030-05-17 14:03");
    REQUIRE(noSec.has_value());
    CHECK(*noSec == *at - 21);
    CHECK_FALSE(parseDateTimeLocal("2030-13-01 00:00:00").has_value());
    CHECK_FALSE(parseDateTimeLocal("gibberish").has_value());
}

TEST_CASE("computeNextRun: relative mode counts from the last run") {
    const int64_t now = 1000000;
    tb::Schedule s = sched(tb::Schedule::TimeRelative, 3600);
    // Never ran: one interval from now.
    CHECK(computeNextRun(s, now) == now + 3600);
    // Ran 10 minutes ago: the rest of the interval remains.
    s.lastRunAt = now - 600;
    CHECK(computeNextRun(s, now) == now - 600 + 3600);
    // Long overdue: due immediately (the missed-run gate lives in the tick).
    s.lastRunAt = now - 7200;
    CHECK(computeNextRun(s, now) == now + 1);
}

TEST_CASE("computeNextRun: anchored mode lands on the grid, never drifts") {
    const int64_t now = 1000000;
    tb::Schedule s = sched(tb::Schedule::TimeAnchored, 3600);
    s.startAt = now - 10 * 3600 + 500;  // grid: ...startAt + 10h = now + 500
    CHECK(computeNextRun(s, now) == s.startAt + 10 * 3600);
    // A future anchor is itself the first run.
    s.startAt = now + 5000;
    CHECK(computeNextRun(s, now) == now + 5000);
    // Exactly on a grid point: the NEXT point (strictly after now).
    s.startAt = now - 2 * 3600;
    CHECK(computeNextRun(s, now) == now + 3600);
}

TEST_CASE("computeNextRun: start and end bounds") {
    const int64_t now = 1000000;
    tb::Schedule s = sched(tb::Schedule::TimeRelative, 3600);
    // Start pushes the first run out.
    s.startAt = now + 50000;
    CHECK(computeNextRun(s, now) == now + 50000);
    // End in the past: no run possible.
    s.startAt = 0;
    s.endAt = now - 1;
    CHECK(computeNextRun(s, now) == 0);
    // End before the next grid point: done.
    tb::Schedule g = sched(tb::Schedule::TimeAnchored, 86400);
    g.startAt = now - 1000;
    g.endAt = now + 3600;  // next point now-1000+86400 is past the end
    CHECK(computeNextRun(g, now) == 0);
}

TEST_CASE("computeNextRun: daily mode picks the next local occurrence") {
    tb::Schedule s = sched(tb::Schedule::TimeDaily, 86400);
    s.dailySec = 9 * 3600;  // 09:00:00 local
    // Anchor "now" at a known local time so the expectation is exact.
    auto morning = parseDateTimeLocal("2030-06-10 08:00:00");
    auto evening = parseDateTimeLocal("2030-06-10 20:00:00");
    auto nineToday = parseDateTimeLocal("2030-06-10 09:00:00");
    auto nineNext = parseDateTimeLocal("2030-06-11 09:00:00");
    REQUIRE(morning.has_value());
    REQUIRE(evening.has_value());
    REQUIRE(nineToday.has_value());
    REQUIRE(nineNext.has_value());
    CHECK(computeNextRun(s, *morning) == *nineToday);   // later today
    CHECK(computeNextRun(s, *evening) == *nineNext);    // tomorrow
    CHECK(computeNextRun(s, *nineToday) == *nineNext);  // exactly at 9 -> next day
}
