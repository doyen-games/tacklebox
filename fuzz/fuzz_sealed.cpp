// Fuzz the sealed-message path: a linked dapp's buoy channel delivers
// attacker-chosen bytes that the wallet decodes and tries to unseal.
#include <cstddef>
#include <cstdint>
#include <span>

#include <dwarfkit/antelope/chain/private_key.hpp>
#include <dwarfkit/antelope/serializer.hpp>
#include <dwarfkit/protocol_esr/sealed_messages.hpp>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    auto sealed = dwarfkit::Serializer::decode<dwarfkit::SealedMessage>(
        std::span<const uint8_t>(data, size));
    if (!sealed) return 0;
    static auto key = [] {
        auto generated = dwarfkit::PrivateKey::generate(dwarfkit::KeyType::K1);
        return *generated;
    }();
    (void)dwarfkit::unsealMessage(sealed->ciphertext, key, sealed->from, sealed->nonce);
    return 0;
}
