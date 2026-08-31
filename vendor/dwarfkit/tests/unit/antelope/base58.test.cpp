// Port of antelope test/base58.ts
#include <doctest/doctest.h>

#include <dwarfkit/antelope/base58.hpp>

using namespace dwarfkit;

namespace {
Bytes hexBytes(std::string_view hex) { return Bytes::from(hex).value(); }
void assertBytes(const Bytes& a, std::string_view bHex) {
    CHECK(a.hexString() == hexBytes(bHex).hexString());
}
}  // namespace

TEST_SUITE("base58") {
    TEST_CASE("decode") {
        CHECK(Base58::decode("StV1DL6CwTryKyV").value().equals(hexBytes("68656c6c6f20776f726c64")));
        CHECK(Base58::decode("StV1DL6CwTryKyV", 11).value().equals(
            hexBytes("68656c6c6f20776f726c64")));
        CHECK(Base58::decode("1111").value().equals(hexBytes("00000000")));
        CHECK_FALSE(Base58::decode("000").has_value());
        CHECK_FALSE(Base58::decode("0", 1).has_value());
        CHECK_FALSE(Base58::decode("zzz", 2).has_value());
    }

    TEST_CASE("encode") {
        CHECK(Base58::encode(Bytes::from("hello world", BytesEncoding::utf8).value()) ==
              "StV1DL6CwTryKyV");
        CHECK(Base58::encode("0000").value() == "11");
    }

    TEST_CASE("decode check") {
        assertBytes(
            Base58::decodeCheck("5KQvfsPJ9YvGuVbLRLXVWPNubed6FWvV8yax6cNSJEzB4co3zFu").value(),
            "80d25968ebfce6e617bdb839b5a66cfc1fdd051d79a91094f7baceded449f84333");
        CHECK_FALSE(
            Base58::decodeCheck("5KQVfsPJ9YvGuVbLRLXVWPNubed6FWvV8yax6cNSJEzB4co3zFu").has_value());
    }

    TEST_CASE("decode ripemd160 check") {
        assertBytes(
            Base58::decodeRipemd160Check("6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeABhJRin")
                .value(),
            "02caee1a02910b18dfd5d9db0e8a4bc90f8dd34cedbbfb00c6c841a2abb2fa28cc");
        CHECK_FALSE(
            Base58::decodeRipemd160Check("6RrVujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeABhJRin")
                .has_value());
        assertBytes(
            Base58::decodeRipemd160Check("6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeACcSRFs", 33,
                                         "K1")
                .value(),
            "02caee1a02910b18dfd5d9db0e8a4bc90f8dd34cedbbfb00c6c841a2abb2fa28cc");
        assertBytes(
            Base58::decodeRipemd160Check("6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeACcSRFs", 33,
                                         "K1")
                .value(),
            "02caee1a02910b18dfd5d9db0e8a4bc90f8dd34cedbbfb00c6c841a2abb2fa28cc");
        CHECK_FALSE(Base58::decodeRipemd160Check("6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeACcSRFs",
                                                 std::nullopt, "ZZ")
                        .has_value());
    }

    TEST_CASE("encode check") {
        CHECK(Base58::encodeCheck("80d25968ebfce6e617bdb839b5a66cfc1fdd051d79a91094f7baceded449f84333")
                  .value() == "5KQvfsPJ9YvGuVbLRLXVWPNubed6FWvV8yax6cNSJEzB4co3zFu");
    }

    TEST_CASE("encode ripemd160 check") {
        CHECK(Base58::encodeRipemd160Check(
                  "02caee1a02910b18dfd5d9db0e8a4bc90f8dd34cedbbfb00c6c841a2abb2fa28cc")
                  .value() == "6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeABhJRin");
        CHECK(Base58::encodeRipemd160Check(
                  "02caee1a02910b18dfd5d9db0e8a4bc90f8dd34cedbbfb00c6c841a2abb2fa28cc", "K1")
                  .value() == "6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeACcSRFs");
    }
}
