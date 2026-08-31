// Port of wallet-plugin-cloudwallet src/types.ts. The response interfaces
// parse from the bridge's json messages; serializedTransaction accepts both a
// hex string and an array of byte values.
#pragma once

#include <dwarfkit/antelope.hpp>

namespace dwarfkit::cloudwallet {

struct WAXCloudWalletLoginResponse {
    bool verified = false;
    std::vector<std::string> pubKeys;
    std::string userAccount;
    std::optional<std::string> permission;
    // {proof: {data: {...IdentityProof fields}}} when present
    json proof;

    static Result<WAXCloudWalletLoginResponse> from(const json& value);
};

struct WAXCloudWalletSigningResponse {
    bool verified = false;
    std::vector<Signature> signatures;
    std::optional<Bytes> serializedTransaction;
    std::string type;

    static Result<WAXCloudWalletSigningResponse> from(const json& value);
};

// The ABI covering the two action layouts validateModifications decodes
// (types.ts declares them as Struct classes).
const ABI& validationAbi();

}  // namespace dwarfkit::cloudwallet
