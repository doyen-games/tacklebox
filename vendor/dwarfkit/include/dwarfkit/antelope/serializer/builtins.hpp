// Port of antelope src/serializer/builtins.ts: the name-keyed registry the
// dynamic (json + ABI) encode/decode path dispatches through.
#pragma once

#include <map>
#include <string>

#include <dwarfkit/antelope/serializer/traits.hpp>

namespace dwarfkit {

struct BuiltinType {
    Result<void> (*encodeJSON)(const json& value, ABIEncoder& encoder);
    Result<json> (*decodeABI)(ABIDecoder& decoder);
    Result<json> (*normalizeJSON)(const json& value);
    json (*defaultJSON)();
};

// name -> handlers for every builtin ABI type
const std::map<std::string, BuiltinType, std::less<>>& builtinTypes();

}  // namespace dwarfkit
