// Port of antelope src/utils.ts. arrayEquals and isInstanceOf have no C++
// counterpart (operator== and the type system replace them).
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

std::string arrayToHex(std::span<const uint8_t> array);

Result<std::vector<uint8_t>> hexToArray(std::string_view hex);

// Generate N random bytes, errors if a secure random source isn't available.
Result<std::vector<uint8_t>> secureRandom(size_t length);

}  // namespace dwarfkit
