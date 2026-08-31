// Port of antelope src/api/v1/history.ts
#pragma once

#include <dwarfkit/antelope/api/v1/types.hpp>

namespace dwarfkit {

class APIClient;

class HistoryAPI {
public:
    explicit HistoryAPI(APIClient* client) : client_(client) {}

    Result<api::v1::GetActionsResponse> get_actions(const Name& accountName, int32_t pos,
                                                    int32_t offset);
    Result<api::v1::GetActionsResponse> get_actions(std::string_view accountName, int32_t pos,
                                                    int32_t offset) {
        return get_actions(Name::from(accountName), pos, offset);
    }

    struct GetTransactionOptions {
        std::optional<uint32_t> blockNumHint;
        std::optional<bool> excludeTraces;
    };
    Result<api::v1::GetTransactionResponse> get_transaction(
        const Checksum256& id, const GetTransactionOptions& options = {});

    Result<api::v1::GetKeyAccountsResponse> get_key_accounts(const PublicKey& publicKey);
    Result<api::v1::GetControlledAccountsResponse> get_controlled_accounts(
        const Name& controllingAccount);
    Result<api::v1::GetControlledAccountsResponse> get_controlled_accounts(
        std::string_view controllingAccount) {
        return get_controlled_accounts(Name::from(controllingAccount));
    }

private:
    APIClient* client_;
};

}  // namespace dwarfkit
