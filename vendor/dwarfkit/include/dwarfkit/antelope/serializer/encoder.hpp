// Port of antelope src/serializer/encoder.ts (the ABIEncoder class; abiEncode
// lives in serializer.hpp)
#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include <dwarfkit/antelope/chain/bytes.hpp>

namespace dwarfkit {

class ABIEncoder {
public:
    // Write a single byte.
    void writeByte(uint8_t byte) { data_.push_back(byte); }

    // Write an array of bytes.
    void writeArray(std::span<const uint8_t> bytes) {
        data_.insert(data_.end(), bytes.begin(), bytes.end());
    }

    template <class T>
        requires std::is_integral_v<T>
    void writeInt(T value) {
        for (size_t i = 0; i < sizeof(T); i++) {
            data_.push_back(static_cast<uint8_t>(static_cast<std::make_unsigned_t<T>>(value) >>
                                                 (8 * i)));
        }
    }

    void writeFloat32(float value) {
        static_assert(sizeof(float) == 4);
        writeInt(std::bit_cast<uint32_t>(value));
    }

    void writeFloat64(double value) {
        static_assert(sizeof(double) == 8);
        writeInt(std::bit_cast<uint64_t>(value));
    }

    void writeVaruint32(uint32_t v) {
        for (;;) {
            if (v >> 7) {
                data_.push_back(static_cast<uint8_t>(0x80 | (v & 0x7f)));
                v = v >> 7;
            } else {
                data_.push_back(static_cast<uint8_t>(v));
                break;
            }
        }
    }

    void writeVarint32(int32_t v) {
        writeVaruint32((static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31));
    }

    void writeString(std::string_view v) {
        writeVaruint32(static_cast<uint32_t>(v.size()));
        data_.insert(data_.end(), v.begin(), v.end());
    }

    std::span<const uint8_t> getData() const { return data_; }

    Bytes getBytes() const { return Bytes(data_); }

private:
    std::vector<uint8_t> data_;
};

}  // namespace dwarfkit
