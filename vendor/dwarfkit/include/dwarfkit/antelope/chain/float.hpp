// Port of antelope src/chain/float.ts.
//
// Wharfkit's Float32/Float64 wrapper classes map to native float/double
// (BLUEPRINT.md 5.6); their toString/toJSON rules live in the serializer
// builtins. Float128 has no native counterpart and keeps its 16 raw bytes.
#pragma once

#include <string>

#include <dwarfkit/antelope/chain/bytes.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

class Float128 {
public:
    static constexpr std::string_view abiName = "float128";
    static constexpr size_t byteWidth = 16;

    Bytes data;

    Float128() : data(std::vector<uint8_t>(byteWidth, 0)) {}
    explicit Float128(Bytes data) : data(std::move(data)) {}

    static Result<Float128> from(const Bytes& value);
    // float128 uses 0x prefixed hex strings as opposed to everywhere else where
    // there is no prefix
    static Result<Float128> from(std::string_view value);

    static Float128 abiDefault() { return {}; }
    static Result<Float128> random();

    bool equals(const Float128& other) const { return data == other.data; }
    bool operator==(const Float128&) const = default;

    std::string toString() const { return "0x" + data.hexString(); }
    json toJSON() const { return toString(); }
};

}  // namespace dwarfkit
