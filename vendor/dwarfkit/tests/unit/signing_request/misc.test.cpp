// Port of signing-request test/misc.ts
#include <doctest/doctest.h>

#include <dwarfkit/signing_request.hpp>

using namespace dwarfkit;

TEST_SUITE("sr-misc") {
    TEST_CASE("should create chain id") {
        const auto id = ChainId::from(uint8_t(1)).value();
        CHECK(id.equals("aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906"));
        CHECK(variantChainId(id.chainVariant()).value().chainName() == ChainName::EOS);
        const auto id2 =
            ChainId::from("beefbeef06b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906")
                .value();
        CHECK(id2.chainName() == ChainName::UNKNOWN);
        CHECK(id2.chainVariant().variantName() == "chain_id");
        CHECK(variantChainId(id2.chainVariant()).value() == id2);
        CHECK_FALSE(ChainId::from(uint8_t(99)).has_value());
        CHECK_FALSE(variantChainId(ChainIdVariant(ChainAlias(uint8_t(0)))).has_value());
    }

    TEST_CASE("should set request flags") {
        RequestFlags flags;
        CHECK(flags.background() == false);
        CHECK(flags.broadcast() == false);
        flags.setBackground(true);
        CHECK(flags.background() == true);
        CHECK(flags.broadcast() == false);
        CHECK(flags.value == 2);
        flags.setBroadcast(true);
        CHECK(flags.background() == true);
        CHECK(flags.broadcast() == true);
        CHECK(flags.value == 3);
        flags.setBackground(false);
        CHECK(flags.background() == false);
        CHECK(flags.broadcast() == true);
        CHECK(flags.value == 1);
        flags.setBroadcast(false);
        CHECK(flags.background() == false);
        CHECK(flags.broadcast() == false);
        CHECK(flags.value == 0);
    }
}
