#include <doctest/doctest.h>

#include <string>

#include "vault/cipher.hpp"

using namespace tb;

static SecureBytes testKeys(uint8_t seed) {
    SecureBytes keys(64);
    for (size_t i = 0; i < 64; ++i) keys.data()[i] = static_cast<uint8_t>(seed + i);
    return keys;
}

static std::span<const uint8_t> spanOf(const std::string& s) {
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

TEST_CASE("seal/open round trip") {
    SecureBytes keys = testKeys(1);
    std::string message = "the vault payload {\"keys\":[]} with some length to cross blocks....";
    std::string aad = "TBX|v1|scrypt|15,8,1|00ff";

    SealedBox box = seal(keys, spanOf(message), spanOf(aad));
    CHECK(box.ciphertext.size() % 16 == 0);
    CHECK(box.ciphertext.size() >= message.size());

    auto opened = open(keys, box, spanOf(aad));
    REQUIRE(opened.has_value());
    CHECK(opened->view() == message);
}

TEST_CASE("empty plaintext round trips") {
    SecureBytes keys = testKeys(9);
    std::string aad = "hdr";
    SealedBox box = seal(keys, {}, spanOf(aad));
    auto opened = open(keys, box, spanOf(aad));
    REQUIRE(opened.has_value());
    CHECK(opened->size() == 0);
}

TEST_CASE("tampering is detected before decryption") {
    SecureBytes keys = testKeys(1);
    std::string message = "attack at dawn";
    std::string aad = "hdr";
    SealedBox box = seal(keys, spanOf(message), spanOf(aad));

    SUBCASE("flipped ciphertext byte") {
        box.ciphertext[0] ^= 0x01;
        CHECK_FALSE(open(keys, box, spanOf(aad)).has_value());
    }
    SUBCASE("flipped IV byte") {
        box.iv[3] ^= 0x80;
        CHECK_FALSE(open(keys, box, spanOf(aad)).has_value());
    }
    SUBCASE("flipped MAC byte") {
        box.mac[31] ^= 0xff;
        CHECK_FALSE(open(keys, box, spanOf(aad)).has_value());
    }
    SUBCASE("different AAD (header swap)") {
        std::string other = "hdr2";
        CHECK_FALSE(open(keys, box, spanOf(other)).has_value());
    }
    SUBCASE("wrong key") {
        SecureBytes wrong = testKeys(2);
        CHECK_FALSE(open(wrong, box, spanOf(aad)).has_value());
    }
}

TEST_CASE("unique IV per seal") {
    SecureBytes keys = testKeys(5);
    std::string message = "same message";
    std::string aad = "hdr";
    SealedBox a = seal(keys, spanOf(message), spanOf(aad));
    SealedBox b = seal(keys, spanOf(message), spanOf(aad));
    CHECK(a.iv != b.iv);
    CHECK(a.ciphertext != b.ciphertext);
}
