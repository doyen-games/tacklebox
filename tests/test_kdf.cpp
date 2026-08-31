#include <doctest/doctest.h>

#include "core/util.hpp"
#include "vault/kdf.hpp"

using namespace tb;

static std::vector<uint8_t> bytes(const char* s) {
    return std::vector<uint8_t>(s, s + std::string(s).size());
}

TEST_CASE("PBKDF2-HMAC-SHA256 matches RFC 7914 vectors") {
    std::vector<uint8_t> out(64);

    pbkdf2HmacSha256(bytes("passwd"), bytes("salt"), 1, out);
    CHECK(toHex(out) ==
          "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
          "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");

    pbkdf2HmacSha256(bytes("Password"), bytes("NaCl"), 80000, out);
    CHECK(toHex(out) ==
          "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
          "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d");
}

TEST_CASE("scrypt matches RFC 7914 vectors") {
    std::vector<uint8_t> out(64);

    // N=1024 (logN=10), r=8, p=16
    REQUIRE(scrypt(bytes("password"), bytes("NaCl"), {10, 8, 16}, out));
    CHECK(toHex(out) ==
          "fdbabe1c9d3472007856e7190d01e9fe7c6ad7cbc8237830e77376634b373162"
          "2eaf30d92e22a3886ff109279d9830dac727afb94a83ee6d8360cbdfa2cc0640");

    // N=16384 (logN=14), r=8, p=1
    REQUIRE(scrypt(bytes("pleaseletmein"), bytes("SodiumChloride"), {14, 8, 1}, out));
    CHECK(toHex(out) ==
          "7023bdcb3afd7348461c06cd81fd38ebfda8fbba904f8e3ea9b543f6545da1f2"
          "d5432955613f0fcf62d49705242a9af9e61e85dc0d651e40dfcf017b45575887");
}

TEST_CASE("scrypt rejects out-of-policy parameters") {
    std::vector<uint8_t> out(32);
    CHECK_FALSE(scrypt(bytes("x"), bytes("y"), {9, 8, 1}, out));    // N too small
    CHECK_FALSE(scrypt(bytes("x"), bytes("y"), {23, 8, 1}, out));   // N too large
    CHECK_FALSE(scrypt(bytes("x"), bytes("y"), {15, 0, 1}, out));   // r zero
    CHECK_FALSE(scrypt(bytes("x"), bytes("y"), {15, 8, 17}, out));  // p too large
}

TEST_CASE("deriveVaultKeys is deterministic per salt and diverges across salts") {
    SecureBytes password(std::string_view("correct horse battery staple"));
    std::vector<uint8_t> saltA(32, 0x11), saltB(32, 0x22);
    ScryptParams fast{10, 8, 1};

    SecureBytes a1 = deriveVaultKeys(password, saltA, fast);
    SecureBytes a2 = deriveVaultKeys(password, saltA, fast);
    SecureBytes b = deriveVaultKeys(password, saltB, fast);
    REQUIRE(a1.size() == 64);
    CHECK(constTimeEq(a1.span(), a2.span()));
    CHECK_FALSE(constTimeEq(a1.span(), b.span()));
}
