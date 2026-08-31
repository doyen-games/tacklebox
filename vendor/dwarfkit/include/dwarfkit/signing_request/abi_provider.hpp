// The AbiProvider interface from @wharfkit/signing-request, implemented by
// ABICache and consumed by SigningRequest resolution.
#pragma once

#include <dwarfkit/antelope/chain/abi.hpp>

namespace dwarfkit {

struct AbiProvider {
    virtual Result<ABI> getAbi(const Name& account) = 0;
    virtual ~AbiProvider() = default;
};

}  // namespace dwarfkit
