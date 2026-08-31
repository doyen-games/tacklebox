// Sanity vectors for the trezor-backed digest primitives (NIST / RFC 4231).
#include <doctest/doctest.h>

#include <dwarfkit/antelope/utils.hpp>
#include <dwarfkit/core/hash.hpp>

using namespace dwarfkit;

namespace {
std::vector<uint8_t> bytes(std::string_view text) { return {text.begin(), text.end()}; }
}  // namespace

TEST_SUITE("core") {
    TEST_CASE("hash primitives") {
        CHECK(arrayToHex(sha256(bytes("abc"))) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        CHECK(arrayToHex(sha256(bytes(""))) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        CHECK(arrayToHex(sha512(bytes("abc"))) ==
              "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
              "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
        CHECK(arrayToHex(ripemd160(bytes("abc"))) ==
              "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc");
        const std::vector<uint8_t> key(20, 0x0b);
        CHECK(arrayToHex(hmacSha256(key, bytes("Hi There"))) ==
              "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
        CHECK(arrayToHex(hmacSha512(key, bytes("Hi There"))) ==
              "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
              "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854");
    }

    TEST_CASE("hex") {
        CHECK(arrayToHex(std::vector<uint8_t>{0x00, 0xff, 0x0a}) == "00ff0a");
        CHECK(hexToArray("00FF0a").value() == std::vector<uint8_t>{0x00, 0xff, 0x0a});
        CHECK(hexToArray("abc").error().message == "Odd number of hex digits");
        CHECK(hexToArray("zz").error().message == "Expected hex string");
    }

    TEST_CASE("secure random") {
        const auto a = secureRandom(32).value();
        const auto b = secureRandom(32).value();
        CHECK(a.size() == 32);
        CHECK(a != b);
        CHECK(secureRandom(0).value().empty());
    }
}
