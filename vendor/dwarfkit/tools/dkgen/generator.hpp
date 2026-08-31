// dkgen: port of @wharfkit/cli generate (BLUEPRINT.md 7), emitting a C++
// header instead of a TypeScript module. Type resolution, alias handling and
// struct dependency ordering mirror the cli's contract command.
#pragma once

#include <dwarfkit/antelope.hpp>

namespace dwarfkit::dkgen {

struct GenerateOptions {
    // Namespace under dwarfkit::gen; defaults to the sanitized account name.
    std::string namespaceName;
};

// Emit a self-contained header for the contract: abiBlob/abi(), Types
// namespace with DK_STRUCT/DK_VARIANT declarations in dependency order, and a
// Contract subclass with typed action helpers and table accessors.
Result<std::string> generateContractHeader(const std::string& contractName, const ABI& abi,
                                           const GenerateOptions& options = {});

// A C++ identifier for an on-chain name: dots removed; C++ keywords get a
// trailing underscore; a leading digit gets a leading underscore.
std::string sanitizeIdentifier(const std::string& name);

}  // namespace dwarfkit::dkgen
