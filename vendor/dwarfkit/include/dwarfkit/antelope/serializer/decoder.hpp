// Port of antelope src/serializer/decoder.ts (the ABIDecoder class; abiDecode
// lives in serializer.hpp)
#pragma once

#include <bit>
#include <cstdint>
#include <span>
#include <string>

#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

class ABIDecoder {
public:
    explicit ABIDecoder(std::span<const uint8_t> array, bool ignoreInvalidUTF8 = false)
        : array_(array), ignoreInvalidUTF8_(ignoreInvalidUTF8) {}

    bool canRead(size_t bytes = 1) const { return pos_ + bytes <= array_.size(); }

    // strictExtensions decode mode: absent binary-extension fields synthesize
    // their type's default value instead of staying absent.
    bool strictExtensions = false;

    Result<void> setPosition(size_t pos) {
        if (pos > array_.size()) {
            return err(ErrorKind::Invalid, "Invalid position");
        }
        pos_ = pos;
        return {};
    }

    size_t getPosition() const { return pos_; }

    Result<void> advance(size_t bytes) {
        DK_CHECK(ensureBytes(bytes));
        pos_ += bytes;
        return {};
    }

    // Read one byte.
    Result<uint8_t> readByte() {
        DK_CHECK(ensureBytes(1));
        return array_[pos_++];
    }

    template <class T>
        requires std::is_integral_v<T>
    Result<T> readInt() {
        DK_CHECK(ensureBytes(sizeof(T)));
        std::make_unsigned_t<T> value = 0;
        for (size_t i = 0; i < sizeof(T); i++) {
            value |= static_cast<std::make_unsigned_t<T>>(array_[pos_ + i]) << (8 * i);
        }
        pos_ += sizeof(T);
        return static_cast<T>(value);
    }

    Result<float> readFloat32() {
        DK_TRY(bits, readInt<uint32_t>());
        return std::bit_cast<float>(bits);
    }

    Result<double> readFloat64() {
        DK_TRY(bits, readInt<uint64_t>());
        return std::bit_cast<double>(bits);
    }

    Result<uint32_t> readVaruint32() {
        uint32_t v = 0;
        int bit = 0;
        for (;;) {
            DK_TRY(b, readByte());
            v |= static_cast<uint32_t>(b & 0x7f) << bit;
            bit += 7;
            if (!(b & 0x80)) {
                break;
            }
        }
        return v;
    }

    Result<int32_t> readVarint32() {
        DK_TRY(v, readVaruint32());
        if (v & 1) {
            return static_cast<int32_t>((~v >> 1) | 0x80000000u);
        }
        return static_cast<int32_t>(v >> 1);
    }

    Result<std::span<const uint8_t>> readArray(size_t length) {
        DK_CHECK(ensureBytes(length));
        const auto rv = array_.subspan(pos_, length);
        pos_ += length;
        return rv;
    }

    Result<std::string> readString();

private:
    Result<void> ensureBytes(size_t bytes) const {
        if (!canRead(bytes)) {
            return err(ErrorKind::Invalid, "Read past end of buffer");
        }
        return {};
    }

    std::span<const uint8_t> array_;
    size_t pos_ = 0;
    bool ignoreInvalidUTF8_ = false;
};

}  // namespace dwarfkit
