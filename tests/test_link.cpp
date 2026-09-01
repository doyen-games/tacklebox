#include <doctest/doctest.h>

#include "app/link.hpp"

using namespace tb;

TEST_CASE("callback url template scrubbing") {
    // Plain buoy callbacks pass through untouched.
    CHECK(scrubCallbackUrl("https://cb.anchor.link/5f2c...uuid") ==
          "https://cb.anchor.link/5f2c...uuid");
    // Signature templates vanish - a rejection substitutes nothing.
    CHECK(scrubCallbackUrl("https://dapp.example/cb?sig={{sig}}") ==
          "https://dapp.example/cb?sig=");
    CHECK(scrubCallbackUrl("https://dapp.example/{{sig0}}/{{bn}}/done") ==
          "https://dapp.example///done");
    // Unterminated templates are left alone rather than mangled.
    CHECK(scrubCallbackUrl("https://dapp.example/cb?sig={{sig") ==
          "https://dapp.example/cb?sig={{sig");
    CHECK(scrubCallbackUrl("") == "");
}
