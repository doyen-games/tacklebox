#include <dwarfkit/antelope/api/v1/history.hpp>

#include <dwarfkit/antelope/api/client.hpp>

namespace dwarfkit {

using namespace api::v1;

Result<GetActionsResponse> HistoryAPI::get_actions(const Name& accountName, int32_t pos,
                                                   int32_t offset) {
    return client_->call<GetActionsResponse>(
        {.path = "/v1/history/get_actions",
         .params = json{{"account_name", accountName.toString()}, {"pos", pos},
                        {"offset", offset}}});
}

Result<GetTransactionResponse> HistoryAPI::get_transaction(const Checksum256& id,
                                                           const GetTransactionOptions& options) {
    json params = {{"id", id.hexString()}};
    if (options.blockNumHint) {
        params["block_num_hint"] = *options.blockNumHint;
    }
    if (options.excludeTraces == true) {
        params["traces"] = false;
    }
    return client_->call<GetTransactionResponse>(
        {.path = "/v1/history/get_transaction", .params = std::move(params)});
}

Result<GetKeyAccountsResponse> HistoryAPI::get_key_accounts(const PublicKey& publicKey) {
    return client_->call<GetKeyAccountsResponse>(
        {.path = "/v1/history/get_key_accounts",
         .params = json{{"public_key", publicKey.toString()}}});
}

Result<GetControlledAccountsResponse> HistoryAPI::get_controlled_accounts(
    const Name& controllingAccount) {
    return client_->call<GetControlledAccountsResponse>(
        {.path = "/v1/history/get_controlled_accounts",
         .params = json{{"controlling_account", controllingAccount.toString()}}});
}

}  // namespace dwarfkit
