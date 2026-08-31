// Port of signing-request src/base64u.ts: URL-safe Base64 variant, no padding.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dwarfkit::base64u {

std::string encode(std::span<const uint8_t> data, bool urlSafe = true);
// accepts both urlsafe and standard alphabets; unknown characters decode as 0,
// exactly like upstream
std::vector<uint8_t> decode(std::string_view input);

}  // namespace dwarfkit::base64u
