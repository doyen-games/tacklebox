#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

// zlib-wrapped deflate (pako.deflate / pako.inflate equivalents), via miniz.
Result<std::vector<uint8_t>> zlibCompress(std::span<const uint8_t> data);
Result<std::vector<uint8_t>> zlibUncompress(std::span<const uint8_t> data);

// raw deflate without the zlib wrapper (pako deflateRaw / inflateRaw), used by
// the ESR encoding.
Result<std::vector<uint8_t>> deflateRaw(std::span<const uint8_t> data);
Result<std::vector<uint8_t>> inflateRaw(std::span<const uint8_t> data);

}  // namespace dwarfkit
