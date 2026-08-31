// Port of sealed-messages test/tests/utils.ts (the upstream sealMessage that
// returns raw Bytes maps to the createIV/createSymmetricKey/encryptMessage
// primitives here, see DIVERGENCES.md)
#include <doctest/doctest.h>

#include <dwarfkit/protocol_esr/sealed_messages.hpp>

using namespace dwarfkit;

TEST_SUITE("sealed-messages") {
    TEST_CASE("sealMessage") {
        const auto from = PrivateKey::generate(KeyType::K1).value();
        const auto to = PrivateKey::generate(KeyType::K1).value().toPublic().value();
        const uint64_t nonce = 1234567890;
        const std::string message = "Hello, World!";

        const auto secret = from.sharedSecret(to).value();
        const auto sealed =
            encryptMessage(createIV(nonce, secret), createSymmetricKey(secret, nonce).array,
                           message)
                .value();
        CHECK(sealed.hexString() != message);
        CHECK(sealed.array.size() > 0);
        CHECK(sealed.array.size() % 16 == 0);
    }

    TEST_CASE("unsealMessage") {
        const auto from = PrivateKey::generate(KeyType::K1).value();
        const auto to = PrivateKey::generate(KeyType::K1).value();
        const uint64_t nonce = 1234567890;
        const std::string message = "Hello, World!";

        const auto sealed =
            sealMessage(message, from, to.toPublic().value(), nonce).value();
        const auto unsealed =
            unsealMessage(sealed.ciphertext, to, from.toPublic().value(), nonce).value();
        CHECK(unsealed == message);
    }

    TEST_CASE("sealedMessagePayload generates a random nonce when none is given") {
        const auto from = PrivateKey::generate(KeyType::K1).value();
        const auto to = PrivateKey::generate(KeyType::K1).value().toPublic().value();
        const auto a = sealedMessagePayload("x", from, to).value();
        const auto b = sealedMessagePayload("x", from, to).value();
        CHECK(a.nonce != b.nonce);
    }

    TEST_CASE("SealedMessage serializes as the sealed_message struct") {
        const auto key = PrivateKey::generate(KeyType::K1).value();
        SealedMessage sealed;
        sealed.from = key.toPublic().value();
        sealed.nonce = 42;
        sealed.ciphertext = Bytes(std::vector<uint8_t>{1, 2, 3});
        sealed.checksum = 7;
        const auto encoded = Serializer::encode(sealed).value();
        const auto decoded = Serializer::decode<SealedMessage>(encoded.array).value();
        CHECK(decoded.from.toString() == sealed.from.toString());
        CHECK(decoded.nonce == 42);
        CHECK(decoded.ciphertext.hexString() == "010203");
        CHECK(decoded.checksum == 7);
        const ABI abi = Serializer::synthesize<SealedMessage>();
        REQUIRE(abi.structs.size() == 1);
        CHECK(abi.structs[0].name == "sealed_message");
    }
}
