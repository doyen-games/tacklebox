#include <doctest/doctest.h>

#include <dwarfkit/signing_request.hpp>

#include "app/link.hpp"

using namespace tb;
namespace dk = dwarfkit;

TEST_CASE("request broadcast flag forwards to the transact options") {
    // Wharfkit-style: broadcast false, the dapp pushes after signing.
    dk::SigningRequest request;
    request.setBroadcast(false);
    dk::TransactArgs args;
    args.request = request;
    CHECK(esrBroadcastFlag(args) == false);

    // eosio.to-style: broadcast true, the wallet pushes.
    request.setBroadcast(true);
    args.request = request;
    CHECK(esrBroadcastFlag(args) == true);

    // Action-built transactions carry no request: nothing to forward.
    CHECK(esrBroadcastFlag(dk::TransactArgs{}) == std::nullopt);

    // A garbage uri yields nothing rather than a wrong default.
    dk::TransactArgs bad;
    bad.request = std::string("esr:not-a-real-request");
    CHECK(esrBroadcastFlag(bad) == std::nullopt);
}

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
