// Port of antelope src/chain/block-id.ts
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include <dwarfkit/antelope/chain/checksum.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

class BlockId {
public:
    // eosio contract context defines this with a _type suffix for some reason
    static constexpr std::string_view abiName = "block_id_type";

    std::array<uint8_t, 32> array{};

    constexpr BlockId() = default;
    constexpr explicit BlockId(const std::array<uint8_t, 32>& array) : array(array) {}

    static Result<BlockId> from(std::span<const uint8_t> value) {
        if (value.size() != 32) {
            return err(ErrorKind::Invalid, "BlockId size mismatch, expected 32 bytes got " +
                                               std::to_string(value.size()));
        }
        BlockId rv;
        std::copy(value.begin(), value.end(), rv.array.begin());
        return rv;
    }
    static Result<BlockId> from(const Bytes& value) {
        return from(std::span<const uint8_t>(value.array));
    }
    static Result<BlockId> from(std::string_view hex) {
        DK_TRY(bytes, hexToArray(hex));
        return from(std::span<const uint8_t>(bytes));
    }

    static BlockId fromBlockChecksum(const Checksum256& checksum, uint32_t blockNum);
    static Result<BlockId> fromBlockChecksum(std::string_view checksum, uint32_t blockNum);

    static BlockId abiDefault() { return {}; }

    bool equals(const BlockId& other) const { return array == other.array; }
    bool equals(std::string_view hex) const {
        const auto other = from(hex);
        return other && array == other->array;
    }
    constexpr bool operator==(const BlockId&) const = default;

    std::string hexString() const { return arrayToHex(array); }
    std::string toString() const { return hexString(); }
    json toJSON() const { return toString(); }

    constexpr uint32_t blockNum() const {
        return (static_cast<uint32_t>(array[0]) << 24) | (static_cast<uint32_t>(array[1]) << 16) |
               (static_cast<uint32_t>(array[2]) << 8) | static_cast<uint32_t>(array[3]);
    }
};

}  // namespace dwarfkit
