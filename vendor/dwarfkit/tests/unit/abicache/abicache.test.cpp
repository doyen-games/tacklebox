// Port of abicache test/tests/abi.ts (the fixture-backed cases; the flaky
// promise-retry case maps to Result retries).
#include <doctest/doctest.h>

#include <dwarfkit/abicache.hpp>

#include "../../util/mock_fetch_provider.hpp"

using namespace dwarfkit;

namespace {

std::shared_ptr<APIClient> client() {
    return test::makeClient(DK_FIXTURE_DIR "/abicache/data");
}

}  // namespace

TEST_SUITE("abicache") {
    TEST_CASE("fetches data") {
        ABICache abiCache(client());
        const auto result = abiCache.getAbi("eosio.token"_n).value();
        CHECK(result.version == "eosio::abi/1.2");
    }

    TEST_CASE("caches data") {
        ABICache abiCache(client());
        REQUIRE(abiCache.getAbi("eosio.evm"_n).has_value());
        CHECK(abiCache.cacheHas("eosio.evm"_n));
        REQUIRE(abiCache.getAbi("eosio.token"_n).has_value());
        CHECK(abiCache.cacheHas("eosio.token"_n));
    }

    TEST_CASE("no duplicate data") {
        ABICache abiCache(client());
        REQUIRE(abiCache.getAbi("eosio.token"_n).has_value());
        REQUIRE(abiCache.getAbi("eosio.token"_n).has_value());
        CHECK(abiCache.cacheHas("eosio.token"_n));
        CHECK(abiCache.cacheSize() == 1);
    }

    TEST_CASE("manually add abi") {
        ABICache abiCache(client());
        ABI abi;
        abi.version = "eosio::abi/1.2";
        abiCache.setAbi("foo"_n, abi);
        CHECK(abiCache.cacheSize() == 1);
        const auto result = abiCache.getAbi("foo"_n).value();
        CHECK(result.version == "eosio::abi/1.2");
    }

    TEST_CASE("merge abis") {
        ABICache abiCache(client());
        // a partial (merge) ABI is reconciled with the chain ABI on getAbi
        ABI partial;
        partial.version = "eosio::abi/1.2";
        partial.structs.push_back({"synthetic", "", {{"field", "name"}}});
        partial.actions.push_back({Name::from("synthact"), "synthetic", ""});
        abiCache.setAbi("eosio.token"_n, partial, true);
        const auto merged = abiCache.getAbi("eosio.token"_n).value();
        CHECK(merged.getStruct("synthetic") != nullptr);
        CHECK(merged.getStruct("transfer") != nullptr);
        CHECK(merged.getActionType("transfer").has_value());
        // and the cache now holds the reconciled ABI
        const auto again = abiCache.getAbi("eosio.token"_n).value();
        CHECK(again.getStruct("transfer") != nullptr);
    }
}
