// Port of token src/contracts/system.token.ts (a @wharfkit/cli generated
// contract module). The typed Types/ActionParams surfaces are dkgen output in
// dwarfkit; this module ships the embedded ABI and a preconfigured Contract.
#pragma once

#include <dwarfkit/contract/contract.hpp>

namespace dwarfkit::system_token {

// base64 ABI blob exactly as shipped in the generated module
extern const char* const abiBlob;

// the eosio.token ABI decoded from abiBlob
const ABI& abi();

// Contract preconfigured with the embedded ABI
Contract contract(const std::shared_ptr<APIClient>& client,
                  const Name& account = Name::from("eosio.token"));

}  // namespace dwarfkit::system_token
