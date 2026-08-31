#include <dwarfkit/protocol_esr/sealed_messages.hpp>

#include <cstring>
#include <mutex>

#include <aes/aes.h>

namespace dwarfkit {

namespace {

void ensureAesInit() {
    static std::once_flag once;
    std::call_once(once, [] { aes_init(); });
}

Bytes hashSource(const Checksum512& secret, uint64_t nonce) {
    // Serializer.encode({object: nonce}) is the 8 little-endian bytes
    std::vector<uint8_t> data(8);
    for (int i = 0; i < 8; i++) {
        data[static_cast<size_t>(i)] = static_cast<uint8_t>(nonce >> (i * 8));
    }
    return Bytes(std::move(data)).appending(std::span<const uint8_t>(secret.array));
}

}  // namespace

Bytes createSymmetricKey(const Checksum512& secret, uint64_t nonce) {
    const Checksum512 key = Checksum512::hash(hashSource(secret, nonce));
    return Bytes(std::vector<uint8_t>(key.array.begin(), key.array.begin() + 32));
}

Checksum512 createIV(uint64_t nonce, const Checksum512& secret) {
    return Checksum512::hash(hashSource(secret, nonce));
}

Result<Bytes> encryptMessage(const Checksum512& iv, std::span<const uint8_t> symmetricKey,
                             std::string_view message) {
    if (symmetricKey.size() != 32) {
        return err(ErrorKind::Invalid, "Symmetric key must be 32 bytes");
    }
    ensureAesInit();
    aes_encrypt_ctx ctx;
    if (aes_encrypt_key256(symmetricKey.data(), &ctx) != EXIT_SUCCESS) {
        return err(ErrorKind::Internal, "AES key setup failed");
    }
    // PKCS7 padding to a whole number of 16-byte blocks
    const size_t padded = (message.size() / AES_BLOCK_SIZE + 1) * AES_BLOCK_SIZE;
    std::vector<uint8_t> buffer(padded);
    std::memcpy(buffer.data(), message.data(), message.size());
    const uint8_t pad = static_cast<uint8_t>(padded - message.size());
    for (size_t i = message.size(); i < padded; i++) {
        buffer[i] = pad;
    }
    uint8_t ivBytes[AES_BLOCK_SIZE];
    std::memcpy(ivBytes, iv.array.data() + 32, AES_BLOCK_SIZE);
    if (aes_cbc_encrypt(buffer.data(), buffer.data(), static_cast<int>(padded), ivBytes, &ctx) !=
        EXIT_SUCCESS) {
        return err(ErrorKind::Internal, "AES encryption failed");
    }
    return Bytes(std::move(buffer));
}

Result<Bytes> decryptMessage(const Checksum512& iv, std::span<const uint8_t> symmetricKey,
                             const Bytes& message) {
    if (symmetricKey.size() != 32) {
        return err(ErrorKind::Invalid, "Symmetric key must be 32 bytes");
    }
    if (message.array.empty() || message.array.size() % AES_BLOCK_SIZE != 0) {
        return err(ErrorKind::Invalid, "Invalid ciphertext length");
    }
    ensureAesInit();
    aes_decrypt_ctx ctx;
    if (aes_decrypt_key256(symmetricKey.data(), &ctx) != EXIT_SUCCESS) {
        return err(ErrorKind::Internal, "AES key setup failed");
    }
    std::vector<uint8_t> buffer(message.array.size());
    uint8_t ivBytes[AES_BLOCK_SIZE];
    std::memcpy(ivBytes, iv.array.data() + 32, AES_BLOCK_SIZE);
    if (aes_cbc_decrypt(message.array.data(), buffer.data(),
                        static_cast<int>(message.array.size()), ivBytes, &ctx) != EXIT_SUCCESS) {
        return err(ErrorKind::Internal, "AES decryption failed");
    }
    const uint8_t pad = buffer.back();
    if (pad == 0 || pad > AES_BLOCK_SIZE || pad > buffer.size()) {
        return err(ErrorKind::Invalid, "Invalid message padding");
    }
    buffer.resize(buffer.size() - pad);
    return Bytes(std::move(buffer));
}

Result<SealedMessage> sealedMessagePayload(std::string_view message, const PrivateKey& privateKey,
                                           const PublicKey& publicKey,
                                           std::optional<uint64_t> nonce) {
    uint64_t nonceValue;
    if (nonce) {
        nonceValue = *nonce;
    } else {
        DK_TRY(random, secureRandom(8));
        nonceValue = 0;
        for (int i = 0; i < 8; i++) {
            nonceValue |= static_cast<uint64_t>(random[static_cast<size_t>(i)]) << (i * 8);
        }
    }
    DK_TRY(secret, privateKey.sharedSecret(publicKey));
    const Checksum512 iv = createIV(nonceValue, secret);
    const Bytes symmetricKey = createSymmetricKey(secret, nonceValue);
    DK_TRY(ciphertext, encryptMessage(iv, symmetricKey.array, message));
    const Checksum256 checksumHash = Checksum256::hash(std::span<const uint8_t>(iv.array));
    uint32_t checksum = 0;
    for (int i = 0; i < 4; i++) {
        checksum |= static_cast<uint32_t>(checksumHash.array[static_cast<size_t>(i)]) << (i * 8);
    }
    DK_TRY(from, privateKey.toPublic());
    SealedMessage sealed;
    sealed.from = from;
    sealed.nonce = nonceValue;
    sealed.ciphertext = std::move(ciphertext);
    sealed.checksum = checksum;
    return sealed;
}

Result<std::string> unsealMessage(const Bytes& message, const PrivateKey& privateKey,
                                  const PublicKey& publicKey, uint64_t nonce) {
    DK_TRY(secret, privateKey.sharedSecret(publicKey));
    const Checksum512 iv = createIV(nonce, secret);
    const Bytes symmetricKey = createSymmetricKey(secret, nonce);
    DK_TRY(decrypted, decryptMessage(iv, symmetricKey.array, message));
    return decrypted.utf8String();
}

Result<SealedMessage> sealMessage(std::string_view message, const PrivateKey& privateKey,
                                  const PublicKey& publicKey, std::optional<uint64_t> nonce) {
    return sealedMessagePayload(message, privateKey, publicKey, nonce);
}

}  // namespace dwarfkit
