// Port of common test/tests/chains.ts
#include <doctest/doctest.h>

#include <dwarfkit/common.hpp>

using namespace dwarfkit;

TEST_SUITE("chains") {
    TEST_CASE("chainIdsToIndices -> ChainDefinitions") {
        for (const auto& [chainId, indice] : chainIdsToIndices()) {
            const ChainDefinition* def = Chains::byIndice(indice);
            REQUIRE_MESSAGE(def != nullptr, indice);
            CHECK_MESSAGE(def->id.hexString() == chainId, indice);
        }
    }

    TEST_CASE("chainIdToIndices -> ChainNames") {
        for (const auto& [chainId, indice] : chainIdsToIndices()) {
            CHECK_MESSAGE(ChainNames().contains(indice), indice);
        }
    }

    TEST_CASE("valid data") {
        for (const auto& [chainId, indice] : chainIdsToIndices()) {
            const ChainDefinition* def = Chains::byIndice(indice);
            REQUIRE(def != nullptr);
            CHECK(def->name() == ChainNames().at(indice));
        }
    }

    TEST_CASE("chain definition helpers") {
        const auto& jungle = Chains::Jungle4();
        CHECK(jungle.name() == "Jungle 4 (Testnet)");
        CHECK(jungle.url == "https://jungle4.greymass.com");
        REQUIRE(jungle.systemToken.has_value());
        CHECK(jungle.systemToken->symbol.toString() == "4,EOS");
        CHECK(jungle.systemToken->contract.toString() == "eosio.token");
        const auto logo = jungle.getLogo();
        REQUIRE(logo.has_value());
        CHECK(logo->light == "https://assets.wharfkit.com/chain/jungle.png");
        // the EOS chain id resolves to the Vaulta name
        CHECK(Chains::EOS().name() == "Vaulta");
        // explorer urls
        const ExplorerDefinition explorer{.prefix = "https://bloks.io/transaction/",
                                          .suffix = ""};
        CHECK(explorer.url("abc") == "https://bloks.io/transaction/abc");
        // from json round trip
        const auto def = ChainDefinition::from(json{
            {"id", "73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d"},
            {"url", "https://jungle4.greymass.com"}}).value();
        CHECK(def.equals(jungle));
    }
}
