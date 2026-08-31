#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

std::string base64Encode(std::span<const uint8_t> data);
Result<std::vector<uint8_t>> base64Decode(std::string_view text);

}  // namespace dwarfkit
