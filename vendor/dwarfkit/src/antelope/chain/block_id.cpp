#include <dwarfkit/antelope/chain/block_id.hpp>

namespace dwarfkit {

BlockId BlockId::fromBlockChecksum(const Checksum256& checksum, uint32_t blockNum) {
    BlockId id(checksum.array);
    id.array[0] = static_cast<uint8_t>(blockNum >> 24);
    id.array[1] = static_cast<uint8_t>(blockNum >> 16);
    id.array[2] = static_cast<uint8_t>(blockNum >> 8);
    id.array[3] = static_cast<uint8_t>(blockNum);
    return id;
}

Result<BlockId> BlockId::fromBlockChecksum(std::string_view checksum, uint32_t blockNum) {
    DK_TRY(sum, Checksum256::from(checksum));
    return fromBlockChecksum(sum, blockNum);
}

}  // namespace dwarfkit
