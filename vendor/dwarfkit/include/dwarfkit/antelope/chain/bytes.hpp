// Port of antelope src/chain/bytes.ts
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <dwarfkit/antelope/utils.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

enum class BytesEncoding { hex, utf8 };

class Bytes {
public:
    static constexpr std::string_view abiName = "bytes";

    std::vector<uint8_t> array;

    Bytes() = default;
    explicit Bytes(std::vector<uint8_t> array) : array(std::move(array)) {}
    explicit Bytes(std::span<const uint8_t> array) : array(array.begin(), array.end()) {}

    static Bytes from(Bytes value) { return value; }
    static Bytes from(std::span<const uint8_t> value) { return Bytes(value); }
    static Bytes from(std::vector<uint8_t> value) { return Bytes(std::move(value)); }
    static Result<Bytes> from(std::string_view value, BytesEncoding encoding = BytesEncoding::hex) {
        return fromString(value, encoding);
    }

    static Result<Bytes> fromString(std::string_view value,
                                    BytesEncoding encoding = BytesEncoding::hex);

    static Bytes abiDefault() { return Bytes(); }

    static bool equal(const Bytes& a, const Bytes& b) { return a.equals(b); }

    static Result<Bytes> random(size_t length);

    // Number of bytes in this instance.
    size_t length() const { return array.size(); }

    // Hex string representation of this instance.
    std::string hexString() const { return arrayToHex(array); }

    // UTF-8 string representation of this instance.
    std::string utf8String() const { return {array.begin(), array.end()}; }

    // Mutating. Append bytes to this instance.
    void append(const Bytes& other) {
        array.insert(array.end(), other.array.begin(), other.array.end());
    }
    void append(std::span<const uint8_t> other) {
        array.insert(array.end(), other.begin(), other.end());
    }

    // Non-mutating, returns a copy of this instance with appended bytes.
    Bytes appending(const Bytes& other) const {
        Bytes rv(*this);
        rv.append(other);
        return rv;
    }
    Bytes appending(std::span<const uint8_t> other) const {
        Bytes rv(*this);
        rv.append(other);
        return rv;
    }

    // Mutating. Pad this instance to length, zeros in front.
    void zeropad(size_t n, bool truncate = false);

    // Non-mutating, returns a copy of this instance with zeros padded.
    Bytes zeropadded(size_t n, bool truncate = false) const {
        Bytes rv(*this);
        rv.zeropad(n, truncate);
        return rv;
    }

    // Mutating. Drop bytes from the start of this instance.
    void dropFirst(size_t n = 1) { array.erase(array.begin(), array.begin() + std::min(n, array.size())); }

    // Non-mutating, returns a copy of this instance with dropped bytes from the start.
    Bytes droppingFirst(size_t n = 1) const {
        Bytes rv(*this);
        rv.dropFirst(n);
        return rv;
    }

    Bytes copy() const { return *this; }

    bool equals(const Bytes& other) const { return array == other.array; }
    bool operator==(const Bytes&) const = default;

    std::string toString(BytesEncoding encoding = BytesEncoding::hex) const {
        return encoding == BytesEncoding::hex ? hexString() : utf8String();
    }

    json toJSON() const { return hexString(); }
};

}  // namespace dwarfkit
