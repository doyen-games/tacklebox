#include <doctest/doctest.h>

#include "core/util.hpp"

using tb::parseDouble;

TEST_CASE("parseDouble accepts plain decimals, signs, fractions and exponents") {
    double v = -1;
    CHECK(parseDouble("42", v));
    CHECK(v == doctest::Approx(42.0));
    CHECK(parseDouble("-12.3456", v));
    CHECK(v == doctest::Approx(-12.3456));
    CHECK(parseDouble("+0.5", v));
    CHECK(v == doctest::Approx(0.5));
    CHECK(parseDouble(".25", v));
    CHECK(v == doctest::Approx(0.25));
    CHECK(parseDouble("7.", v));
    CHECK(v == doctest::Approx(7.0));
    CHECK(parseDouble("1e3", v));
    CHECK(v == doctest::Approx(1000.0));
    CHECK(parseDouble("2.5E-2", v));
    CHECK(v == doctest::Approx(0.025));
    CHECK(parseDouble("0", v));
    CHECK(v == 0.0);
}

TEST_CASE("parseDouble rejects anything that is not exactly one number") {
    double v = 9;
    CHECK_FALSE(parseDouble("", v));
    CHECK_FALSE(parseDouble(" 1", v));
    CHECK_FALSE(parseDouble("1 ", v));
    CHECK_FALSE(parseDouble("1,5", v));
    CHECK_FALSE(parseDouble("abc", v));
    CHECK_FALSE(parseDouble("1.2.3", v));
    CHECK_FALSE(parseDouble("-", v));
    CHECK_FALSE(parseDouble(".", v));
    CHECK_FALSE(parseDouble("1e", v));
    CHECK_FALSE(parseDouble("0x10", v));
    CHECK_FALSE(parseDouble("inf", v));
    CHECK_FALSE(parseDouble("nan", v));
    CHECK_FALSE(parseDouble("1.0 WAX", v));
    CHECK(v == 9);  // untouched on failure
}
