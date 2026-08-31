// Port of antelope test/integer.ts.
//
// Cases exercising the JS Int wrapper's C++11 emulation (mixed-width adds,
// casts between native widths, division behaviors) are not ported: native C++
// integers ARE that behavior. What remains: the 128-bit types, overflow
// behaviors, and the toJSON rules.
#include <doctest/doctest.h>

#include <dwarfkit/antelope/chain/integer.hpp>

using namespace dwarfkit;

TEST_SUITE("integer") {
    TEST_CASE("from") {
        CHECK(UInt128::from("340282366920938463463374607431768211455").value() == UInt128::max());
        CHECK(UInt128::from("340282366920938463463374607431768211456").error().message ==
              "Number 340282366920938463463374607431768211456 overflows uint128");
        CHECK(UInt128::from("-42").error().message == "Number -42 underflows uint128");
        CHECK(UInt128::from("banana").error().message == "Invalid number");
        CHECK(Int128::from("170141183460469231731687303715884105727").value() == Int128::max());
        CHECK(Int128::from("-170141183460469231731687303715884105728").value() == Int128::min());
        CHECK(Int128::from("170141183460469231731687303715884105728").error().message ==
              "Number 170141183460469231731687303715884105728 overflows int128");
        CHECK(Int128::from("-170141183460469231731687303715884105729").error().message ==
              "Number -170141183460469231731687303715884105729 underflows int128");
    }

    TEST_CASE("overflow behaviors") {
        // truncate keeps the two's complement bit pattern
        CHECK(UInt128::from("-1", OverflowBehavior::Truncate).value() == UInt128::max());
        // clamp pins to the representable range
        CHECK(UInt128::from("-42", OverflowBehavior::Clamp).value() == UInt128::zero());
        CHECK(UInt128::from("340282366920938463463374607431768211456", OverflowBehavior::Clamp)
                  .value() == UInt128::max());
        CHECK(Int128::from("-999999999999999999999999999999999999999999", OverflowBehavior::Clamp)
                  .value() == Int128::min());
    }

    TEST_CASE("cast") {
        // UInt128.from('340282366920938463463374607431768211455').cast(UInt64)
        CHECK(UInt128::max().toUInt64() == 18446744073709551615ull);
    }

    TEST_CASE("subtract") {
        // UInt64.from(-1, 'truncate').subtracting(Int128.from('19446744070000000000'))
        const auto a = Int128::from("18446744073709551615").value();
        const auto b = Int128::from("19446744070000000000").value();
        const auto difference = a.subtracting(b);
        CHECK(difference.toString() == "-999999996290448385");
        CHECK(difference.toUInt64() == 17446744077419103231ull);
        // wrap-around
        CHECK(UInt128::max().adding(UInt128(uint64_t(1))) == UInt128::zero());
    }

    TEST_CASE("to primitive") {
        CHECK(intToJSON(uint64_t(1459536)).dump() == "1459536");
        CHECK(intToJSON(uint64_t(14595364149838066048ull)).dump() == "\"14595364149838066048\"");
        CHECK(intToJSON(int64_t(-1)).dump() == "-1");
        CHECK(intToJSON(int64_t(4294967295)).dump() == "4294967295");
        CHECK(intToJSON(int64_t(4294967296)).dump() == "\"4294967296\"");
        CHECK(intToJSON(int64_t(-4294967295)).dump() == "-4294967295");
        CHECK(intToJSON(int64_t(-4294967296)).dump() == "\"-4294967296\"");
        CHECK(UInt128(uint64_t(1459536)).toJSON().dump() == "1459536");
        CHECK(UInt128::from("14595364149838066048").value().toJSON().dump() ==
              "\"14595364149838066048\"");
        CHECK(Int128::from("-14595364149838066048").value().toJSON().dump() ==
              "\"-14595364149838066048\"");
        CHECK(Int128(int64_t(-42)).toJSON().dump() == "-42");
        CHECK(UInt128::from("14595364149838066048").value().toString() == "14595364149838066048");
        CHECK(VarUInt(7).toJSON().dump() == "7");
        CHECK(VarInt(-7).toJSON().dump() == "-7");
    }

    TEST_CASE("zero") {
        CHECK(Int128::from(0).equals(Int128::zero()));
        CHECK(UInt128::from(uint64_t(0)).equals(UInt128::zero()));
    }

    TEST_CASE("byte array") {
        const auto bytes = UInt128(uint64_t(1)).byteArray();
        CHECK(bytes[0] == 1);
        CHECK(bytes[15] == 0);
        CHECK(UInt128::fromByteArray(bytes) == UInt128(uint64_t(1)));
        const auto negBytes = Int128(int64_t(-1)).byteArray();
        CHECK(negBytes[0] == 0xff);
        CHECK(negBytes[15] == 0xff);
        CHECK(Int128::fromByteArray(negBytes) == Int128(int64_t(-1)));
    }

    TEST_CASE("random") {
        CHECK(UInt128::random().value().byteArray().size() == 16);
        CHECK(UInt128::random().value() != UInt128::random().value());
    }
}
