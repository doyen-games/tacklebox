// Port of antelope test/webauthn.ts. The upstream test generates a fresh p256
// keypair with elliptic; here the fixture keypair comes from the R1 code path
// (both are the p256 curve).
#include <doctest/doctest.h>

#include <dwarfkit/antelope/chain/private_key.hpp>
#include <dwarfkit/antelope/serializer.hpp>

using namespace dwarfkit;

namespace {

std::vector<uint8_t> utf8(std::string_view text) { return {text.begin(), text.end()}; }

struct WaFixture {
    PublicKey waPublicKey;
    Signature waSignature;
    Checksum256 messageDigest;
    Bytes compressedKey;
};

WaFixture makeFixture() {
    const auto keyPair = PrivateKey::generate(KeyType::R1).value();
    const auto compressed = keyPair.toPublic().value().data;

    const auto hostname = utf8("example.com");
    std::vector<uint8_t> waKeyData(compressed.array);
    waKeyData.push_back(0x01);  // user presence
    waKeyData.push_back(static_cast<uint8_t>(hostname.size()));  // varuint32 rpid length
    waKeyData.insert(waKeyData.end(), hostname.begin(), hostname.end());
    PublicKey waPublicKey(KeyType::WA, Bytes(waKeyData));

    const auto messageDigest = Checksum256::hash(utf8("test message for WA keys"));
    const auto parts = crypto::sign(keyPair.data.array, messageDigest.array, KeyType::R1).value();

    ABIEncoder enc;
    enc.writeByte(static_cast<uint8_t>(parts.recid + 31));
    enc.writeArray(parts.r);
    enc.writeArray(parts.s);
    const auto authData = utf8("mockAuthenticatorData012345678901234567890123456789");
    enc.writeVaruint32(static_cast<uint32_t>(authData.size()));
    enc.writeArray(authData);
    const auto clientJson =
        utf8(R"({"type":"webauthn.get","challenge":"...","origin":"https://example.com"})");
    enc.writeVaruint32(static_cast<uint32_t>(clientJson.size()));
    enc.writeArray(clientJson);
    Signature waSignature(KeyType::WA, enc.getBytes());

    return {std::move(waPublicKey), std::move(waSignature), messageDigest, compressed};
}

}  // namespace

TEST_SUITE("webauthn") {
    TEST_CASE("WA key support") {
        const auto fx = makeFixture();

        SUBCASE("WA PublicKey encoding") {
            CHECK(fx.waPublicKey.type == KeyType::WA);
            const auto str = fx.waPublicKey.toString();
            CHECK(str.starts_with("PUB_WA_"));
            const auto parsed = PublicKey::from(str).value();
            CHECK(parsed.type == KeyType::WA);
            CHECK(parsed.data.equals(fx.waPublicKey.data));
            CHECK(parsed.toString() == str);
            const auto compressed = fx.waPublicKey.getCompressedKeyBytes();
            CHECK(compressed.length() == 33);
            CHECK(compressed.equals(fx.compressedKey));
            const auto legacy = fx.waPublicKey.toLegacyString();
            REQUIRE_FALSE(legacy.has_value());
            CHECK(legacy.error().message ==
                  "Unable to create legacy formatted string for non-K1 key");
        }

        SUBCASE("WA Signature encoding") {
            CHECK(fx.waSignature.type == KeyType::WA);
            const auto sigString = fx.waSignature.toString();
            CHECK(sigString.starts_with("SIG_WA_"));
            const auto parsed = Signature::from(sigString).value();
            CHECK(parsed.type == KeyType::WA);
            CHECK(parsed.data.equals(fx.waSignature.data));
            // and the WA signature round-trips through the binary wire format
            const auto encoded = Serializer::encode(fx.waSignature).value();
            const auto decoded = Serializer::decode<Signature>(encoded).value();
            CHECK(decoded.equals(fx.waSignature));
        }

        SUBCASE("WA sign and verify") {
            CHECK(fx.waSignature.verifyDigest(fx.messageDigest, fx.waPublicKey));
            const auto wrongDigest = Checksum256::hash(utf8("wrong message"));
            CHECK_FALSE(fx.waSignature.verifyDigest(wrongDigest, fx.waPublicKey));
            const auto other = makeFixture();
            CHECK_FALSE(fx.waSignature.verifyDigest(fx.messageDigest, other.waPublicKey));
        }

        SUBCASE("WA sign and recover") {
            const auto recovered = fx.waSignature.recoverDigest(fx.messageDigest);
            REQUIRE_FALSE(recovered.has_value());
            CHECK(recovered.error().message ==
                  "can't recover webauthn public keys, please use @wharfkit/webauthn.");
        }

        SUBCASE("PrivateKey generation for WA") {
            const auto waPrivKey = PrivateKey::generate(KeyType::WA).value();
            CHECK(waPrivKey.type == KeyType::WA);
            const auto waPubKey = waPrivKey.toPublic().value();
            CHECK(waPubKey.type == KeyType::WA);
        }
    }
}
