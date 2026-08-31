// Port of antelope src/api/v1/chain.ts
#pragma once

#include <dwarfkit/antelope/api/v1/types.hpp>

namespace dwarfkit {

class APIClient;

struct SendTransaction2Options {
    std::optional<bool> return_failure_trace;
    std::optional<bool> retry_trx;
    std::optional<int64_t> retry_trx_num_blocks;
};

// get_table_rows result; rows stay json unless the typed overload is used
struct GetTableRowsResponse {
    json rows = json::array();
    bool more = false;
    json next_key;  // null when absent
    std::optional<std::vector<Name>> ram_payers;
};

template <class Row>
struct GetTableRowsResponseTyped {
    std::vector<Row> rows;
    bool more = false;
    json next_key;
    std::optional<std::vector<Name>> ram_payers;
};

class ChainAPI {
public:
    explicit ChainAPI(APIClient* client) : client_(client) {}

    Result<json> get_abi(const Name& accountName);
    Result<json> get_abi(std::string_view accountName) { return get_abi(Name::from(accountName)); }

    Result<api::v1::GetCodeResponse> get_code(const Name& accountName);
    Result<api::v1::GetRawAbiResponse> get_raw_abi(const Name& accountName);
    Result<api::v1::AccountObject> get_account(const Name& accountName);
    Result<api::v1::AccountObject> get_account(std::string_view accountName) {
        return get_account(Name::from(accountName));
    }

    // params keeps the caller's key order (it feeds the request body verbatim)
    Result<api::v1::AccountsByAuthorizers> get_accounts_by_authorizers(const json& params);
    Result<api::v1::GetProtocolFeaturesResponse> get_activated_protocol_features(
        const std::optional<json>& params = std::nullopt);

    Result<api::v1::GetBlockResponse> get_block(uint32_t blockNum);
    Result<api::v1::GetBlockResponse> get_block(const BlockId& blockId);
    Result<api::v1::GetBlockResponse> get_block(std::string_view blockId);
    Result<api::v1::GetBlockHeaderStateResponse> get_block_header_state(uint32_t blockNum);
    Result<api::v1::GetBlockInfoResponse> get_block_info(uint32_t blockNum);

    Result<std::vector<Asset>> get_currency_balance(const Name& contract, const Name& accountName,
                                                    std::optional<std::string> symbol = std::nullopt);
    Result<std::vector<Asset>> get_currency_balance(std::string_view contract,
                                                    std::string_view accountName,
                                                    std::optional<std::string> symbol = std::nullopt) {
        return get_currency_balance(Name::from(contract), Name::from(accountName),
                                    std::move(symbol));
    }

    Result<api::v1::GetCurrencyStatsResponse> get_currency_stats(const Name& contract,
                                                                 const std::string& symbol);
    Result<api::v1::GetCurrencyStatsResponse> get_currency_stats(std::string_view contract,
                                                                 const std::string& symbol) {
        return get_currency_stats(Name::from(contract), symbol);
    }

    Result<api::v1::GetInfoResponse> get_info();
    Result<api::v1::GetProducerScheduleResponse> get_producer_schedule();

    Result<json> compute_transaction(const SignedTransaction& tx);
    Result<json> compute_transaction(const PackedTransaction& tx);
    Result<json> send_read_only_transaction(const SignedTransaction& tx);
    Result<json> send_read_only_transaction(const PackedTransaction& tx);
    Result<json> push_transaction(const SignedTransaction& tx);
    Result<json> push_transaction(const PackedTransaction& tx);
    Result<json> send_transaction(const SignedTransaction& tx);
    Result<json> send_transaction(const PackedTransaction& tx);
    Result<json> send_transaction2(const SignedTransaction& tx,
                                   const SendTransaction2Options& options = {});
    Result<json> send_transaction2(const PackedTransaction& tx,
                                   const SendTransaction2Options& options = {});

    // params keeps the caller's key order; the same override semantics as
    // upstream apply (code/table/scope/key_type/json/limit normalization)
    Result<GetTableRowsResponse> get_table_rows(json params) {
        return get_table_rows_impl(std::move(params), std::nullopt);
    }

    template <class Row>
    Result<GetTableRowsResponseTyped<Row>> get_table_rows(json params) {
        // if we know the row type don't ask the node to perform abi decoding
        DK_TRY(untyped, get_table_rows_impl(std::move(params), false));
        GetTableRowsResponseTyped<Row> rv;
        rv.more = untyped.more;
        rv.next_key = std::move(untyped.next_key);
        rv.ram_payers = std::move(untyped.ram_payers);
        for (const auto& row : untyped.rows) {
            if (row.is_string()) {
                DK_TRY(data, Bytes::from(row.get<std::string>()));
                DK_TRY(decoded, Serializer::decode<Row>(data));
                rv.rows.push_back(std::move(decoded));
            } else {
                DK_TRY(decoded, abi_traits<Row>::fromJSON(row));
                rv.rows.push_back(std::move(decoded));
            }
        }
        return rv;
    }

    Result<api::v1::GetTableByScopeResponse> get_table_by_scope(const json& params);

    Result<GetTableRowsResponse> get_table_rows_impl(json params, std::optional<bool> defaultJson);

    Result<api::v1::GetTransactionStatusResponse> get_transaction_status(const Checksum256& id);

private:
    APIClient* client_;
};

}  // namespace dwarfkit
