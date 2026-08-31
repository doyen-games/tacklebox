// Port of protocol-esr test/tests/esr.ts
#include <doctest/doctest.h>

#include <dwarfkit/protocol_esr.hpp>

using namespace dwarfkit;

namespace {

// test/utils/mock-esr.ts mockCallbackPayload
json mockCallbackPayload() {
    return json{
        {"sig",
         "SIG_K1_K4nkCupUx3hDXSHq4rhGPpDMPPPjJyvmF3M6j7ppYUzkR3L93endwnxf3YhJSG4SSvxxU1ytD8hj39ku"
         "kTeYxjwy5H3XNJ"},
        {"tx", "b8e921a7b68d7309847e633d74963f25eb5a7d0b15b1aceb143723c234686a8d"},
        {"rbn", "0"},
        {"rid", "0"},
        {"ex", "2020-07-10T08:40:20"},
        {"req", "esr://AgABAwACE2h0dHBzOi8vZXhhbXBsZS5jb20A"},
        {"sa", "wharfkit1115"},
        {"sp", "test"},
        {"cid", "73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d"},
        {"link_ch", "https://cb.test.com/a5b24a32-cce5-4ab5-b63d-8e29f83e25a9"},
        {"link_key", "PUB_K1_6RMS3nvoN9StPzZizve6WdovaDkE5KkEcCDXW7LbepyAhzQE4R"},
        {"link_name", "anchor"}};
}

const char* sig0 =
    "SIG_K1_KdHDFseJF6paedvSbfHFZzhbtBDVAM8LxeDJsrG33sENRbUQMFHX8CvtT9wRLo4fE4QGYtbp1rF6BqNQ6Pv5Xg"
    "SocXwM67";
const char* sig1 =
    "SIG_K1_K6PhJrD6wvjzVQRwTUd82fk3Z4jznnUszjeBH7xGCAsfByCunzSN2KQ2A9ALetFwLTqnK4xvES6Bstt6NNSvGg"
    "jgM1Tcxn";
const char* sig2 =
    "SIG_K1_KBub1qmdiPpWA2XKKEZEG3EfKJBf38GETHzbd4t3CBdWLgdvFRLCqbcUsBbbYga6jmxfdSFfodMdhMYraKLhEz"
    "jSCsiuMs";

}  // namespace

TEST_SUITE("pesr-esr") {
    TEST_CASE("should extract signatures from the callback payload") {
        json payload = mockCallbackPayload();
        payload["sig0"] = sig0;
        payload["sig1"] = sig1;
        payload["sig2"] = sig2;
        const auto signatures = extractSignaturesFromCallback(payload).value();
        REQUIRE(signatures.size() == 4);
        CHECK(signatures[0].toString() == payload["sig"].get<std::string>());
        CHECK(signatures[1].toString() == sig0);
        CHECK(signatures[2].toString() == sig1);
        CHECK(signatures[3].toString() == sig2);
    }

    TEST_CASE("should extract signatures when numbering starts at sig1 (swift-eosio)") {
        json payload = mockCallbackPayload();
        payload["sig1"] = sig1;
        payload["sig2"] = sig2;
        const auto signatures = extractSignaturesFromCallback(payload).value();
        REQUIRE(signatures.size() == 3);
        CHECK(signatures[0].toString() == payload["sig"].get<std::string>());
        CHECK(signatures[1].toString() == sig1);
        CHECK(signatures[2].toString() == sig2);
    }

    TEST_CASE("should extract signatures across a gap in numbering") {
        json payload = mockCallbackPayload();
        payload["sig0"] = sig0;
        payload["sig2"] = sig2;
        const auto signatures = extractSignaturesFromCallback(payload).value();
        REQUIRE(signatures.size() == 3);
        CHECK(signatures[0].toString() == payload["sig"].get<std::string>());
        CHECK(signatures[1].toString() == sig0);
        CHECK(signatures[2].toString() == sig2);
    }

    TEST_CASE("should deduplicate signatures if needed") {
        json payload = mockCallbackPayload();
        payload["sig0"] = payload["sig"];  // duplicate of sig, should be deduped
        payload["sig1"] = sig1;
        payload["sig2"] = sig2;
        const auto signatures = extractSignaturesFromCallback(payload).value();
        REQUIRE(signatures.size() == 3);
        CHECK(signatures[0].toString() == payload["sig"].get<std::string>());
        CHECK(signatures[1].toString() == sig1);
        CHECK(signatures[2].toString() == sig2);
    }

    TEST_CASE("isCallback checks for a tx key") {
        CHECK(isCallback(mockCallbackPayload()));
        CHECK_FALSE(isCallback(json::object()));
        CHECK_FALSE(isCallback(json("tx")));
    }
}
