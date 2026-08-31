#include <doctest/doctest.h>

#include "chain/netreg.hpp"
#include "chain/prices.hpp"

using namespace tb;
using dwarfkit::json;

TEST_CASE("priceKey uppercases the symbol code") {
    CHECK(priceKey("eosio.token", "wax") == "eosio.token/WAX");
    CHECK(priceKey("alien.worlds", "TLM") == "alien.worlds/TLM");
}

TEST_CASE("alcor tokens parse: numbers, strings, junk skipped") {
    json body = json::array({
        {{"contract", "eosio.token"}, {"symbol", "WAX"}, {"usd_price", 0.0312}},
        {{"contract", "alien.worlds"}, {"symbol", "TLM"}, {"usd_price", "0.0088"}},
        {{"contract", "no.price"}, {"symbol", "NOPE"}},                    // missing field
        {{"contract", "bad.price"}, {"symbol", "BAD"}, {"usd_price", 0}},  // non-positive
        {{"contract", ""}, {"symbol", "X"}, {"usd_price", 1.0}},           // no contract
        json("not-an-object"),
    });
    auto prices = parseAlcorTokens(body);
    REQUIRE(prices.size() == 2);
    CHECK(prices["eosio.token/WAX"] == doctest::Approx(0.0312));
    CHECK(prices["alien.worlds/TLM"] == doctest::Approx(0.0088));
    CHECK(parseAlcorTokens(json::object()).empty());
    CHECK(parseAlcorTokens(json()).empty());
}

TEST_CASE("coingecko simple/price parses only the asked id") {
    json body = {{"wax", {{"usd", 0.0301}}}, {"eos", {{"usd", 0.55}}}};
    auto wax = parseCoinGecko(body, "wax");
    REQUIRE(wax.has_value());
    CHECK(*wax == doctest::Approx(0.0301));
    CHECK_FALSE(parseCoinGecko(body, "telos").has_value());
    CHECK_FALSE(parseCoinGecko(json{{"wax", {{"eur", 0.03}}}}, "wax").has_value());
    CHECK_FALSE(parseCoinGecko(json::array(), "wax").has_value());
}

TEST_CASE("delphi datapoints: newest row wins, precision scales") {
    // Circular buffer order is not chronological; row ids recycle.
    json rows = json::array({
        {{"id", 3}, {"median", 3010}, {"timestamp", "2026-08-31T10:00:00.000"}},
        {{"id", 1}, {"median", 3123}, {"timestamp", "2026-08-31T12:34:56.000"}},
        {{"id", 2}, {"median", 2990}, {"timestamp", "2026-08-31T09:00:00.000"}},
    });
    auto usd = parseDelphiDatapoints(rows, 4);
    REQUIRE(usd.has_value());
    CHECK(*usd == doctest::Approx(0.3123));
    CHECK_FALSE(parseDelphiDatapoints(json::array(), 4).has_value());
    CHECK_FALSE(parseDelphiDatapoints(rows, -1).has_value());
    json zeroed = json::array({{{"median", 0}, {"timestamp", "2026-01-01T00:00:00.000"}}});
    CHECK_FALSE(parseDelphiDatapoints(zeroed, 4).has_value());
}

TEST_CASE("formatUsd scales decimals with magnitude") {
    CHECK(formatUsd(1234.5) == "$1,234.50");
    CHECK(formatUsd(12.3456) == "$12.35");
    CHECK(formatUsd(0.5123) == "$0.5123");
    CHECK(formatUsd(0.0088) == "$0.008800");
    CHECK(formatUsd(0.0) == "-");
    CHECK(formatUsd(-1.0) == "-");
}

TEST_CASE("oracle defaults per chain and provider") {
    // WAX chain id from the catalog, via the presets list.
    std::string waxId;
    for (const auto& preset : tb::allPresets())
        if (preset.lightSlug == "wax") waxId = preset.chainId;
    REQUIRE_FALSE(waxId.empty());

    OracleConfig alcor = oracleDefaults(waxId, OracleProvider::Alcor);
    CHECK(alcor.provider == static_cast<int>(OracleProvider::Alcor));
    CHECK(alcor.url == "https://wax.alcor.exchange");

    OracleConfig gecko = oracleDefaults(waxId, OracleProvider::CoinGecko);
    CHECK(gecko.url == "https://api.coingecko.com");
    CHECK(gecko.coreId == "wax");

    OracleConfig delphi = oracleDefaults(waxId, OracleProvider::Delphi);
    CHECK(delphi.url.empty());
    CHECK(delphi.coreId == "waxpusd");

    // Unknown chain: provider echoed but nothing prefilled, so preset
    // seeding leaves such chains off.
    OracleConfig unknown = oracleDefaults("ff00ff00", OracleProvider::Alcor);
    CHECK(unknown.url.empty());
}
