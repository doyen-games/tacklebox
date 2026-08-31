// Port of antelope src/chain/checksum.ts
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include <dwarfkit/antelope/chain/bytes.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

template <size_t ByteSize, class Derived>
class ChecksumBase {
public:
    static constexpr size_t byteSize = ByteSize;

    std::array<uint8_t, ByteSize> array{};

    constexpr ChecksumBase() = default;
    constexpr explicit ChecksumBase(const std::array<uint8_t, ByteSize>& array) : array(array) {}

    static Result<Derived> from(std::span<const uint8_t> value) {
        if (value.size() != ByteSize) {
            return err(ErrorKind::Invalid, "Checksum size mismatch, expected " +
                                               std::to_string(ByteSize) + " bytes got " +
                                               std::to_string(value.size()));
        }
        Derived rv;
        std::copy(value.begin(), value.end(), rv.array.begin());
        return rv;
    }
    static Result<Derived> from(const Bytes& value) {
        return from(std::span<const uint8_t>(value.array));
    }
    static Result<Derived> from(std::string_view hex) {
        DK_TRY(bytes, hexToArray(hex));
        return from(std::span<const uint8_t>(bytes));
    }

    static Derived abiDefault() { return Derived(); }

    std::string hexString() const { return arrayToHex(array); }
    std::string toString() const { return hexString(); }
    json toJSON() const { return toString(); }

    bool equals(const Derived& other) const { return array == other.array; }
    bool equals(std::string_view hex) const {
        const auto other = from(hex);
        return other && array == other->array;
    }
    bool equals(std::span<const uint8_t> bytes) const {
        const auto other = from(bytes);
        return other && array == other->array;
    }
    constexpr bool operator==(const ChecksumBase&) const = default;
};

class Checksum256 : public ChecksumBase<32, Checksum256> {
public:
    static constexpr std::string_view abiName = "checksum256";
    using ChecksumBase::ChecksumBase;

    static Checksum256 hash(std::span<const uint8_t> data);
    static Checksum256 hash(const Bytes& data) { return hash(std::span<const uint8_t>(data.array)); }
};

class Checksum512 : public ChecksumBase<64, Checksum512> {
public:
    static constexpr std::string_view abiName = "checksum512";
    using ChecksumBase::ChecksumBase;

    static Checksum512 hash(std::span<const uint8_t> data);
    static Checksum512 hash(const Bytes& data) { return hash(std::span<const uint8_t>(data.array)); }
};

class Checksum160 : public ChecksumBase<20, Checksum160> {
public:
    static constexpr std::string_view abiName = "checksum160";
    using ChecksumBase::ChecksumBase;

    static Checksum160 hash(std::span<const uint8_t> data);
    static Checksum160 hash(const Bytes& data) { return hash(std::span<const uint8_t>(data.array)); }
};

}  // namespace dwarfkit
