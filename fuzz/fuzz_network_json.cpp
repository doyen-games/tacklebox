// Fuzz the vault's network deserialization: this shape is parsed from the
// decrypted payload and from imported vaults.
#include <cstddef>
#include <cstdint>
#include <string>

#include "vault/vault.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string input(reinterpret_cast<const char*>(data), size);
    tb::json parsed = tb::json::parse(input, nullptr, false);
    if (parsed.is_discarded()) return 0;
    tb::NetworkDef net = tb::networkFromJson(parsed);
    (void)tb::networkToJson(net);
    (void)net.activeEndpoint();
    (void)net.coreSymbolCode();
    return 0;
}
