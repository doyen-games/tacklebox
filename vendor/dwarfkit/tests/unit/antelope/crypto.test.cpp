// Port of antelope test/crypto.ts
#include <doctest/doctest.h>

#include <dwarfkit/antelope/base58.hpp>
#include <dwarfkit/antelope/chain/private_key.hpp>
#include <dwarfkit/antelope/chain/public_key.hpp>
#include <dwarfkit/antelope/chain/signature.hpp>

using namespace dwarfkit;

namespace {
std::vector<uint8_t> msg(std::string_view text) { return {text.begin(), text.end()}; }
}  // namespace

TEST_SUITE("crypto") {
    TEST_CASE("private key encoding") {
        const auto key = PrivateKey::from("5KQvfsPJ9YvGuVbLRLXVWPNubed6FWvV8yax6cNSJEzB4co3zFu").value();
        CHECK(key.type == KeyType::K1);
        CHECK(key.toWif().value() == "5KQvfsPJ9YvGuVbLRLXVWPNubed6FWvV8yax6cNSJEzB4co3zFu");
        CHECK(key.toString() == "PVT_K1_2be6BwD56MHeVD4P95bRLdnP3oB3P4QRAXAsSKh4N8Xu6d4Aux");
        CHECK(key.data.hexString() ==
              "d25968ebfce6e617bdb839b5a66cfc1fdd051d79a91094f7baceded449f84333");
        const auto r1Key =
            PrivateKey::from("PVT_R1_2dSFGZnA4oFvMHwfjeYCtK2MLLPNYWgYRXrPTcnTaLZFkDSELm").value();
        CHECK(r1Key.toString() == "PVT_R1_2dSFGZnA4oFvMHwfjeYCtK2MLLPNYWgYRXrPTcnTaLZFkDSELm");
        CHECK_FALSE(r1Key.toWif().has_value());
    }

    TEST_CASE("public key encoding") {
        const auto key = PublicKey::from("PUB_K1_6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeACcSRFs").value();
        CHECK(key.type == KeyType::K1);
        CHECK(key.toString() == "PUB_K1_6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeACcSRFs");
        CHECK(key.toLegacyString().value() == "EOS6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeABhJRin");
        CHECK(PublicKey::from("EOS6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeABhJRin").value().toString() ==
              "PUB_K1_6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeACcSRFs");
        CHECK(key.data.hexString() ==
              "02caee1a02910b18dfd5d9db0e8a4bc90f8dd34cedbbfb00c6c841a2abb2fa28cc");
        const auto r1Key = PublicKey::from("PUB_R1_8E46r5HiQF84o6V8MWQQg1vPpgfjYA4XDqT6xbtaaebxw7XbLu").value();
        CHECK(r1Key.toString() == "PUB_R1_8E46r5HiQF84o6V8MWQQg1vPpgfjYA4XDqT6xbtaaebxw7XbLu");
        CHECK_FALSE(r1Key.toLegacyString().has_value());
    }

    TEST_CASE("public key prefix") {
        const auto privKey = PrivateKey::from("5J4zo6Af9QnAeJmNEQeAR4MNhaG7SKVReAYgZC8655hpkbbBscr").value();
        const auto pubKey = privKey.toPublic().value();
        CHECK(pubKey.toString() == "PUB_K1_87DUhBcZrLhyFfBVDyu1iWZJUGURqbk6CQxwv5g6iWUD2X45Hv");
        CHECK(pubKey.toLegacyString().value() == "EOS87DUhBcZrLhyFfBVDyu1iWZJUGURqbk6CQxwv5g6iWUCy9dCUJ");
        CHECK(pubKey.toLegacyString("FIO").value() == "FIO87DUhBcZrLhyFfBVDyu1iWZJUGURqbk6CQxwv5g6iWUCy9dCUJ");
    }

    TEST_CASE("public from private") {
        const auto privKey = PrivateKey::from("5KQvfsPJ9YvGuVbLRLXVWPNubed6FWvV8yax6cNSJEzB4co3zFu").value();
        CHECK(privKey.toPublic().value().toString() ==
              "PUB_K1_6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeACcSRFs");
        const auto r1PrivKey =
            PrivateKey::from("PVT_R1_2dSFGZnA4oFvMHwfjeYCtK2MLLPNYWgYRXrPTcnTaLZFkDSELm").value();
        CHECK(r1PrivKey.toPublic().value().toString() ==
              "PUB_R1_8E46r5HiQF84o6V8MWQQg1vPpgfjYA4XDqT6xbtaaebxw7XbLu");
    }

    TEST_CASE("sign and verify") {
        const auto privKey = PrivateKey::from("5KQvfsPJ9YvGuVbLRLXVWPNubed6FWvV8yax6cNSJEzB4co3zFu").value();
        const auto pubKey = PublicKey::from("PUB_K1_6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeACcSRFs").value();
        const auto message = msg("I like turtles");
        const auto signature = privKey.signMessage(message).value();
        CHECK(signature.verifyMessage(message, pubKey) == true);
        CHECK(signature.verifyMessage(msg("beef"), pubKey) == false);
        CHECK(signature.verifyMessage(
                  message,
                  PublicKey::from("EOS7HBX4f8UknP5NNoX8ixCx4YrA8JcPhGbuQ7Xem8gmWg1nviTqR").value()) ==
              false);
        // r1
        const auto privKey2 = PrivateKey::from("PVT_R1_2dSFGZnA4oFvMHwfjeYCtK2MLLPNYWgYRXrPTcnTaLZFkDSELm").value();
        const auto pubKey2 = PublicKey::from("PUB_R1_8E46r5HiQF84o6V8MWQQg1vPpgfjYA4XDqT6xbtaaebxw7XbLu").value();
        const auto signature2 = privKey2.signMessage(message).value();
        CHECK(signature2.verifyMessage(message, pubKey2) == true);
    }

    TEST_CASE("k1 signature is deterministic") {
        // the elliptic-compatible nonce makes signing reproducible; the exact
        // byte-parity vector is asserted in k1_parity.test.cpp
        const auto privKey = PrivateKey::from("5KQvfsPJ9YvGuVbLRLXVWPNubed6FWvV8yax6cNSJEzB4co3zFu").value();
        const auto a = privKey.signMessage(msg("I like turtles")).value();
        const auto b = privKey.signMessage(msg("I like turtles")).value();
        CHECK(a.toString() == b.toString());
    }

    TEST_CASE("sign and recover") {
        const auto key = PrivateKey::from("5KQvfsPJ9YvGuVbLRLXVWPNubed6FWvV8yax6cNSJEzB4co3zFu").value();
        const auto message = msg("I like turtles");
        const auto signature = key.signMessage(message).value();
        const auto recoveredKey = signature.recoverMessage(message).value();
        CHECK(recoveredKey.toString() == "PUB_K1_6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeACcSRFs");
        CHECK(recoveredKey.toLegacyString().value() == "EOS6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeABhJRin");
        CHECK(recoveredKey.toLegacyString("FIO").value() == "FIO6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeABhJRin");
        CHECK(signature.recoverMessage(msg("beef")).value().toString() !=
              "PUB_K1_6RrvujLQN1x5Tacbep1KAk8zzKpSThAQXBCKYFfGUYeACcSRFs");
        const auto r1Key = PrivateKey::from("PVT_R1_2dSFGZnA4oFvMHwfjeYCtK2MLLPNYWgYRXrPTcnTaLZFkDSELm").value();
        const auto r1Signature = r1Key.signMessage(message).value();
        CHECK(r1Signature.recoverMessage(message).value().toString() ==
              "PUB_R1_8E46r5HiQF84o6V8MWQQg1vPpgfjYA4XDqT6xbtaaebxw7XbLu");
    }

    TEST_CASE("shared secrets") {
        const auto priv1 = PrivateKey::from("5KGNiwTYdDWVBc9RCC28hsi7tqHGUsikn9Gs8Yii93fXbkYzxGi").value();
        const auto priv2 = PrivateKey::from("5Kik3tbLSn24ScHFsj6GwLkgd1H4Wecxkzt1VX7PBBRDQUCdGFa").value();
        const auto pub1 = PublicKey::from("PUB_K1_7Wp9pzhtTfN3jSyQDCktKLqxdTAcAfgT2RrVpE6KThZraa381H").value();
        const auto pub2 = PublicKey::from("PUB_K1_6P8aGPEP79815rKGQ1dbc9eDxoEjatX7Lp696ve5tinnfwJ6nt").value();
        const std::string expected =
            "def2d32f6b849198d71118ef53dbc3b679fe2b2c174ee4242a33e1a3f34c46fc"
            "baa698fb599ca0e36f555dde2ac913a10563de2c33572155487cd8b34523de9e";
        CHECK(priv1.sharedSecret(pub2).value().hexString() == expected);
        CHECK(priv2.sharedSecret(pub1).value().hexString() == expected);
    }

    TEST_CASE("key generation") {
        CHECK(PrivateKey::generate("R1").has_value());
        CHECK(PrivateKey::generate("K1").has_value());
        CHECK_FALSE(PrivateKey::generate("XX").has_value());
        const auto k = PrivateKey::generate("K1").value();
        CHECK(PrivateKey::fromString(k.toString()).has_value());
    }

    TEST_CASE("key errors") {
        const auto badChecksum =
            PrivateKey::from("PVT_K1_2be6BwD56MHeVD4P95bRLdnP3oB3P4QRAXAsSKh4N8Xu6d4Auz");
        REQUIRE_FALSE(badChecksum.has_value());
        CHECK(badChecksum.error().details["code"] == "E_CHECKSUM");
        CHECK(badChecksum.error().details["hash"] == "ripemd160");

        const auto key1 = PrivateKey::fromString(
                              "PVT_K1_2be6BwD56MHeVD4P95bRLdnP3oB3P4QRAXAsSKh4N8Xu6d4Auz", true)
                              .value();
        CHECK(key1.toString() == "PVT_K1_2be6BwD56MHeVD4P95bRLdnP3oB3P4QRAXAsSKh4N8Xu6d4Aux");

        const auto wifBad = PrivateKey::from("5KQvfsPJ9YvGuVbLRLXVWPNubed6FWvV8yax6cNSJEzB4co3zxx");
        REQUIRE_FALSE(wifBad.has_value());
        CHECK(wifBad.error().details["code"] == "E_CHECKSUM");
        CHECK(wifBad.error().details["hash"] == "double_sha256");

        const auto key2 =
            PrivateKey::fromString("5KQvfsPJ9YvGuVbLRLXVWPNubed6FWvV8yax6cNSJEzB4co3zxx", true).value();
        CHECK(key2.toWif().value() == "5KQvfsPJ9YvGuVbLRLXVWPNubed6FWvV8yax6cNSJEzB4co3zFu");
        CHECK(PrivateKey::fromString("PVT_K1_ApBgGcJ2HeGR3szXA9JJptGCWUbSwewtGsxm3DVr86pJtb5V", true)
                  .has_value());
        CHECK(PrivateKey::fromString("PVT_K1_ApBgGcJ2HeGR3szXA9JJptGCWUbSwewtGsxm3DVr86pJtb5V")
                  .error()
                  .message.find("Checksum mismatch") != std::string::npos);
    }

    TEST_CASE("invalid private key (zero key)") {
        const std::vector<uint8_t> zeroBytes(32, 0);
        const std::string k1Str = "PVT_K1_" + Base58::encodeRipemd160Check(Bytes(zeroBytes), "K1");
        CHECK(PrivateKey::from(k1Str).error().message.find(
                  "All-zero private key is not allowed") != std::string::npos);
        const std::string r1Str = "PVT_R1_" + Base58::encodeRipemd160Check(Bytes(zeroBytes), "R1");
        CHECK(PrivateKey::from(r1Str).error().message.find(
                  "All-zero private key is not allowed") != std::string::npos);
    }
}
