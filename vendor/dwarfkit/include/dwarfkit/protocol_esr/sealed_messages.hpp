// Port of wharfkit/sealed-messages: AES-256-CBC sealing with a shared secret
// derived from an ECDH key pair, as used by Anchor for direct wallet messages.
// The upstream sealMessage returning raw Bytes is folded into encryptMessage;
// dwarfkit::sealMessage is the protocol-esr variant returning a SealedMessage
// (see DIVERGENCES.md).
#pragma once

#include <optional>

#include <dwarfkit/antelope.hpp>

namespace dwarfkit {

struct SealedMessage {
    DK_STRUCT("sealed_message")
    PublicKey from;
    uint64_t nonce = 0;
    Bytes ciphertext;
    uint32_t checksum = 0;
    DK_FIELDS(from, nonce, ciphertext, checksum)
};

// sha512(encode(nonce) + secret), truncated to the first 32 bytes.
Bytes createSymmetricKey(const Checksum512& secret, uint64_t nonce);

// sha512(encode(nonce) + secret); encryption uses bytes 32..48 as the AES IV.
Checksum512 createIV(uint64_t nonce, const Checksum512& secret);

// AES-256-CBC with PKCS7 padding (miniaes AES_CBC.encrypt(..., true, iv)).
Result<Bytes> encryptMessage(const Checksum512& iv, std::span<const uint8_t> symmetricKey,
                             std::string_view message);
Result<Bytes> decryptMessage(const Checksum512& iv, std::span<const uint8_t> symmetricKey,
                             const Bytes& message);

// Seal a message into the full payload struct. Without a nonce a random one
// is generated (UInt64.random()).
Result<SealedMessage> sealedMessagePayload(std::string_view message, const PrivateKey& privateKey,
                                           const PublicKey& publicKey,
                                           std::optional<uint64_t> nonce = std::nullopt);

// Decrypt a sealed message ciphertext back to its utf8 string.
Result<std::string> unsealMessage(const Bytes& message, const PrivateKey& privateKey,
                                  const PublicKey& publicKey, uint64_t nonce);

// protocol-esr src/esr.ts sealMessage: alias of sealedMessagePayload.
Result<SealedMessage> sealMessage(std::string_view message, const PrivateKey& privateKey,
                                  const PublicKey& publicKey,
                                  std::optional<uint64_t> nonce = std::nullopt);

}  // namespace dwarfkit
