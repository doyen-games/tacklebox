// Fuzz the ESR decoder: dapps and linked sessions hand the wallet
// attacker-chosen esr:// strings.
#include <cstddef>
#include <cstdint>
#include <string>

#include <dwarfkit/signing_request.hpp>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string input(reinterpret_cast<const char*>(data), size);
    auto request = dwarfkit::SigningRequest::from(input);
    if (request) {
        (void)request->shouldBroadcast();
        (void)request->isIdentity();
        (void)request->getChainId();
        (void)request->getSignatureDigest();
    }
    return 0;
}
