// Port of session test/tests/abi.ts (ABICache against the session fixtures)
#include <doctest/doctest.h>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

TEST_SUITE("session-abi") {
    TEST_CASE("fetches data") {
        ABICache abiCache(makeClient(DK_FIXTURE_DIR "/session/data"));
        const auto result = abiCache.getAbi(Name::from("eosio.token")).value();
        CHECK(result.version == "eosio::abi/1.2");
    }

    TEST_CASE("caches data") {
        ABICache abiCache(makeClient(DK_FIXTURE_DIR "/session/data"));
        REQUIRE(abiCache.getAbi(Name::from("eosio.evm")).has_value());
        CHECK(abiCache.cacheHas(Name::from("eosio.evm")));
        REQUIRE(abiCache.getAbi(Name::from("eosio.token")).has_value());
        CHECK(abiCache.cacheHas(Name::from("eosio.token")));
    }

    TEST_CASE("no duplicate data") {
        ABICache abiCache(makeClient(DK_FIXTURE_DIR "/session/data"));
        REQUIRE(abiCache.getAbi(Name::from("eosio.token")).has_value());
        REQUIRE(abiCache.getAbi(Name::from("eosio.token")).has_value());
        CHECK(abiCache.cacheHas(Name::from("eosio.token")));
        CHECK(abiCache.cacheSize() == 1);
    }

    TEST_CASE("manually add abi") {
        ABICache abiCache(makeClient(DK_FIXTURE_DIR "/session/data"));
        ABI abi;
        abi.version = "eosio::abi/1.2";
        abiCache.setAbi(Name::from("foo"), abi);
        CHECK(abiCache.cacheSize() == 1);
        const auto result = abiCache.getAbi(Name::from("foo")).value();
        CHECK(result.version == "eosio::abi/1.2");
    }
}
