// Port of antelope src/chain/blob.ts
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

class Blob {
public:
    static constexpr std::string_view abiName = "blob";

    std::vector<uint8_t> array;

    Blob() = default;
    explicit Blob(std::vector<uint8_t> array) : array(std::move(array)) {}

    // Create a new Blob instance from a base64 string, fixing up the padding
    // nodeos omits exactly like upstream.
    static Result<Blob> from(std::string_view value) { return fromString(value); }
    static Result<Blob> fromString(std::string_view value);

    bool equals(const Blob& other) const { return array == other.array; }
    bool equals(std::string_view other) const {
        const auto parsed = from(other);
        return parsed && array == parsed->array;
    }
    bool operator==(const Blob&) const = default;

    std::string base64String() const;

    // UTF-8 string representation of this instance.
    std::string utf8String() const { return {array.begin(), array.end()}; }

    std::string toString() const { return base64String(); }
    json toJSON() const { return toString(); }
};

}  // namespace dwarfkit
