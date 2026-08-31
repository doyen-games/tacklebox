// Port of contract src/utils.ts.
#pragma once

#include <dwarfkit/antelope.hpp>

namespace dwarfkit {

// PascalCase version of a snake_case or spaced string.
std::string pascalCase(const std::string& value);

std::string capitalize(const std::string& value);

std::string singularize(const std::string& word);

// primary, secondary, tertiary, fourth...
std::string indexPositionInWords(size_t index);

// Wrap a table index value the way the TS union does: numbers become uint64
// json (string beyond 32 bits), strings stay as-is (names and hex checksums
// both travel as strings), null stays null.
json wrapIndexValue(const json& value);

// Whether a scope is absent, meaning a query should fall back to its default.
// A 0 scope is present.
bool isAbsentScope(const json& value);

// Resolve a scope to the value sent as the scope of a table query.
Result<json> wrapScopeValue(const json& value);

Blob abiToBlob(const ABI& abi);
Result<ABI> blobStringToAbi(const std::string& blobString);

// Substitute ${key} placeholders in a send_transaction exception format
// string, falling back to data.s and then the exception message.
std::string formatExceptionMessage(const json& except);

}  // namespace dwarfkit
