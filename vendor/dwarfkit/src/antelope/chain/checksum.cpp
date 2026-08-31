#include <dwarfkit/antelope/chain/checksum.hpp>

#include <dwarfkit/core/hash.hpp>

namespace dwarfkit {

Checksum256 Checksum256::hash(std::span<const uint8_t> data) {
    return Checksum256(sha256(data));
}

Checksum512 Checksum512::hash(std::span<const uint8_t> data) {
    return Checksum512(sha512(data));
}

Checksum160 Checksum160::hash(std::span<const uint8_t> data) {
    return Checksum160(ripemd160(data));
}

}  // namespace dwarfkit
