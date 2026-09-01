#include <doctest/doctest.h>

#include "core/deeplink.hpp"

using tb::deeplink::Kind;
using tb::deeplink::parse;
using tb::deeplink::uriFromArgs;

TEST_CASE("tacklebox request links unwrap to esr uris") {
    auto parsed = parse("tacklebox://request/gmNgZGBY1mTC_MoglIGBIVzX5uxZRqAQGMBoExgDAjRi");
    CHECK(parsed.kind == Kind::Request);
    CHECK(parsed.esr == "esr://gmNgZGBY1mTC_MoglIGBIVzX5uxZRqAQGMBoExgDAjRi");
}

TEST_CASE("slash count after the scheme does not matter") {
    CHECK(parse("tacklebox:request/AbC").esr == "esr://AbC");
    CHECK(parse("tacklebox:/request/AbC").esr == "esr://AbC");
    CHECK(parse("tacklebox://request/AbC").esr == "esr://AbC");
}

TEST_CASE("scheme and host match case-insensitively, payload case survives") {
    auto parsed = parse("TackleBox://Request/AbCdEf");
    CHECK(parsed.kind == Kind::Request);
    CHECK(parsed.esr == "esr://AbCdEf");
}

TEST_CASE("bare and open links only focus the wallet") {
    CHECK(parse("tacklebox://open").kind == Kind::Focus);
    CHECK(parse("tacklebox://").kind == Kind::Focus);
    CHECK(parse("tacklebox:").kind == Kind::Focus);
    CHECK(parse("tacklebox://request/").kind == Kind::Focus);
    CHECK(parse("tacklebox://something-else").kind == Kind::Focus);
}

TEST_CASE("standard signing-request schemes pass through untouched") {
    CHECK(parse("esr://AbC").esr == "esr://AbC");
    CHECK(parse("esr:AbC").esr == "esr:AbC");
    CHECK(parse("esr-anchor://AbC").kind == Kind::Request);
    CHECK(parse("eosio:AbC").kind == Kind::Request);
    CHECK(parse("  esr://AbC  ").esr == "esr://AbC");
}

TEST_CASE("everything else is not a deep link") {
    CHECK(parse("").kind == Kind::None);
    CHECK(parse("https://example.com").kind == Kind::None);
    CHECK(parse("--desktop").kind == Kind::None);
    CHECK(parse("esrx://nope").kind == Kind::None);
    CHECK(parse("C:\\Users\\someone\\wallet.tbx").kind == Kind::None);
}

TEST_CASE("uriFromArgs picks the first deep link and ignores flags") {
    const char* argv[] = {"tacklebox.exe", "--desktop", "tacklebox://request/AbC", "extra"};
    auto uri = uriFromArgs(4, const_cast<char**>(argv));
    REQUIRE(uri.has_value());
    CHECK(*uri == "tacklebox://request/AbC");

    const char* bare[] = {"tacklebox.exe", "--desktop"};
    CHECK_FALSE(uriFromArgs(2, const_cast<char**>(bare)).has_value());
}
