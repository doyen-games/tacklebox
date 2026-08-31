#include <dwarfkit/antelope/api/v1/chain.hpp>

#include <dwarfkit/antelope/api/client.hpp>

namespace dwarfkit {

using namespace api::v1;

namespace {

// ${key} placeholders in nodeos exception formats
std::string resolveStackFormat(const json& entry) {
    const std::string format = entry.value("format", "");
    if (format.empty()) return "";
    if (!entry.contains("data") || !entry.at("data").is_object()) return format;
    const json& data = entry.at("data");
    std::string rv;
    size_t pos = 0;
    while (pos < format.size()) {
        const size_t open = format.find("${", pos);
        if (open == std::string::npos) {
            rv += format.substr(pos);
            break;
        }
        const size_t close = format.find('}', open);
        if (close == std::string::npos) {
            rv += format.substr(pos);
            break;
        }
        rv += format.substr(pos, open - pos);
        const std::string key = format.substr(open + 2, close - open - 2);
        if (data.contains(key)) {
            const json& value = data.at(key);
            rv += value.is_string() ? value.get<std::string>() : value.dump();
        } else {
            rv += format.substr(open, close - open + 1);
        }
        pos = close + 1;
    }
    return rv;
}

Result<PackedTransaction> pack(const SignedTransaction& tx) {
    return PackedTransaction::fromSigned(tx);
}

}  // namespace

Result<json> ChainAPI::get_abi(const Name& accountName) {
    return client_->call({.path = "/v1/chain/get_abi",
                          .params = json{{"account_name", accountName.toString()}}});
}

Result<GetCodeResponse> ChainAPI::get_code(const Name& accountName) {
    return client_->call<GetCodeResponse>(
        {.path = "/v1/chain/get_code",
         .params = json{{"account_name", accountName.toString()}}});
}

Result<GetRawAbiResponse> ChainAPI::get_raw_abi(const Name& accountName) {
    return client_->call<GetRawAbiResponse>(
        {.path = "/v1/chain/get_raw_abi",
         .params = json{{"account_name", accountName.toString()}}});
}

Result<AccountObject> ChainAPI::get_account(const Name& accountName) {
    return client_->call<AccountObject>(
        {.path = "/v1/chain/get_account",
         .params = json{{"account_name", accountName.toString()}}});
}

Result<AccountsByAuthorizers> ChainAPI::get_accounts_by_authorizers(const json& params) {
    return client_->call<AccountsByAuthorizers>(
        {.path = "/v1/chain/get_accounts_by_authorizers", .params = params});
}

Result<GetProtocolFeaturesResponse> ChainAPI::get_activated_protocol_features(
    const std::optional<json>& params) {
    return client_->call<GetProtocolFeaturesResponse>(
        {.path = "/v1/chain/get_activated_protocol_features", .params = params});
}

Result<GetBlockResponse> ChainAPI::get_block(uint32_t blockNum) {
    return client_->call<GetBlockResponse>(
        {.path = "/v1/chain/get_block", .params = json{{"block_num_or_id", blockNum}}});
}

Result<GetBlockResponse> ChainAPI::get_block(const BlockId& blockId) {
    return get_block(std::string_view(blockId.hexString()));
}

Result<GetBlockResponse> ChainAPI::get_block(std::string_view blockId) {
    return client_->call<GetBlockResponse>(
        {.path = "/v1/chain/get_block", .params = json{{"block_num_or_id", blockId}}});
}

Result<GetBlockHeaderStateResponse> ChainAPI::get_block_header_state(uint32_t blockNum) {
    return client_->call<GetBlockHeaderStateResponse>(
        {.path = "/v1/chain/get_block_header_state",
         .params = json{{"block_num_or_id", blockNum}}});
}

Result<GetBlockInfoResponse> ChainAPI::get_block_info(uint32_t blockNum) {
    return client_->call<GetBlockInfoResponse>(
        {.path = "/v1/chain/get_block_info", .params = json{{"block_num", blockNum}}});
}

Result<std::vector<Asset>> ChainAPI::get_currency_balance(const Name& contract,
                                                          const Name& accountName,
                                                          std::optional<std::string> symbol) {
    json params = {{"account", accountName.toString()}, {"code", contract.toString()}};
    if (symbol) {
        params["symbol"] = *symbol;
    }
    return client_->call<std::vector<Asset>>(
        {.path = "/v1/chain/get_currency_balance", .params = std::move(params)});
}

Result<GetCurrencyStatsResponse> ChainAPI::get_currency_stats(const Name& contract,
                                                              const std::string& symbol) {
    DK_TRY(response, client_->call({.path = "/v1/chain/get_currency_stats",
                                    .params = json{{"code", contract.toString()},
                                                   {"symbol", symbol}}}));
    GetCurrencyStatsResponse result;
    for (const auto& [key, value] : response.items()) {
        DK_TRY(item, abi_traits<GetCurrencyStatsItemResponse>::fromJSON(value));
        result.emplace(key, std::move(item));
    }
    return result;
}

Result<GetInfoResponse> ChainAPI::get_info() {
    return client_->call<GetInfoResponse>({.path = "/v1/chain/get_info", .method = "GET"});
}

Result<GetProducerScheduleResponse> ChainAPI::get_producer_schedule() {
    return client_->call<GetProducerScheduleResponse>(
        {.path = "/v1/chain/get_producer_schedule"});
}

Result<json> ChainAPI::compute_transaction(const SignedTransaction& tx) {
    DK_TRY(packed, pack(tx));
    return compute_transaction(packed);
}

Result<json> ChainAPI::compute_transaction(const PackedTransaction& tx) {
    return client_->call({.path = "/v1/chain/compute_transaction",
                          .params = json{{"transaction", Serializer::objectify(tx)}}});
}

Result<json> ChainAPI::send_read_only_transaction(const SignedTransaction& tx) {
    DK_TRY(packed, pack(tx));
    return send_read_only_transaction(packed);
}

Result<json> ChainAPI::send_read_only_transaction(const PackedTransaction& tx) {
    return client_->call({.path = "/v1/chain/send_read_only_transaction",
                          .params = json{{"transaction", Serializer::objectify(tx)}}});
}

Result<json> ChainAPI::push_transaction(const SignedTransaction& tx) {
    DK_TRY(packed, pack(tx));
    return push_transaction(packed);
}

Result<json> ChainAPI::push_transaction(const PackedTransaction& tx) {
    return client_->call({.path = "/v1/chain/push_transaction",
                          .params = Serializer::objectify(tx)});
}

Result<json> ChainAPI::send_transaction(const SignedTransaction& tx) {
    DK_TRY(packed, pack(tx));
    return send_transaction(packed);
}

Result<json> ChainAPI::send_transaction(const PackedTransaction& tx) {
    return client_->call({.path = "/v1/chain/send_transaction",
                          .params = Serializer::objectify(tx)});
}

Result<json> ChainAPI::send_transaction2(const SignedTransaction& tx,
                                         const SendTransaction2Options& options) {
    DK_TRY(packed, pack(tx));
    return send_transaction2(packed, options);
}

Result<json> ChainAPI::send_transaction2(const PackedTransaction& tx,
                                         const SendTransaction2Options& options) {
    json params = {{"return_failure_trace", true},
                   {"retry_trx", false},
                   {"retry_trx_num_blocks", 0},
                   {"transaction", Serializer::objectify(tx)}};
    if (options.return_failure_trace) params["return_failure_trace"] = *options.return_failure_trace;
    if (options.retry_trx) params["retry_trx"] = *options.retry_trx;
    if (options.retry_trx_num_blocks) params["retry_trx_num_blocks"] = *options.retry_trx_num_blocks;
    DK_TRY(response, client_->call({.path = "/v1/chain/send_transaction2",
                                    .params = std::move(params)}));
    if (response.is_object() && response.contains("processed") &&
        response["processed"].is_object() && response["processed"].contains("except") &&
        !response["processed"]["except"].is_null()) {
        const json& except = response["processed"]["except"];
        json errorDetails = json::array();
        for (const auto& entry : except.value("stack", json::array())) {
            const json context = entry.value("context", json::object());
            errorDetails.push_back({{"message", resolveStackFormat(entry)},
                                    {"file", context.value("file", "")},
                                    {"line_number", context.value("line", 0)},
                                    {"method", context.value("method", "")}});
        }
        json errorJson = response;
        errorJson["error"] = {{"code", except.value("code", 0)},
                              {"name", except.value("name", "transaction_exception")},
                              {"what", except.value("message", "Transaction failed")},
                              {"details", std::move(errorDetails)}};
        APIResponse synthesized;
        synthesized.status = 202;
        synthesized.text = response.dump();
        synthesized.json = std::move(errorJson);
        return err(apierror::make("/v1/chain/send_transaction2", synthesized));
    }
    return response;
}

Result<GetTableRowsResponse> ChainAPI::get_table_rows_impl(json params,
                                                           std::optional<bool> defaultJson) {
    std::string key_type = params.value("key_type", "");
    if (key_type.empty()) {
        key_type = "name";
    }
    const bool jsonRows = params.contains("json") ? params.at("json").get<bool>()
                                                  : defaultJson.value_or(true);

    // scope defaults to code; non-strings stringify
    std::string scope;
    if (!params.contains("scope")) {
        scope = Name::from(params.value("code", "")).toString();
    } else if (params.at("scope").is_string()) {
        scope = params.at("scope").get<std::string>();
    } else {
        scope = params.at("scope").dump();
    }

    json requestParams = std::move(params);
    requestParams["code"] = Name::from(requestParams.value("code", "")).toString();
    requestParams["table"] = Name::from(requestParams.value("table", "")).toString();
    if (requestParams.contains("limit") && requestParams.at("limit").is_string()) {
        requestParams["limit"] = std::stoul(requestParams.at("limit").get<std::string>());
    }
    requestParams["scope"] = scope;
    requestParams["key_type"] = key_type;
    requestParams["json"] = jsonRows;
    for (const char* bound : {"upper_bound", "lower_bound"}) {
        if (requestParams.contains(bound) && !requestParams.at(bound).is_string() &&
            !requestParams.at(bound).is_null()) {
            requestParams[bound] = requestParams.at(bound).dump();
        }
    }
    const bool showPayer = requestParams.value("show_payer", false);

    DK_TRY(response, client_->call({.path = "/v1/chain/get_table_rows",
                                    .params = std::move(requestParams)}));

    GetTableRowsResponse rv;
    rv.more = response.value("more", false);
    json rows = response.value("rows", json::array());
    if (showPayer) {
        rv.ram_payers.emplace();
        json unwrapped = json::array();
        for (const auto& row : rows) {
            rv.ram_payers->push_back(Name::from(row.value("payer", "")));
            unwrapped.push_back(row.value("data", json(nullptr)));
        }
        rows = std::move(unwrapped);
    }
    rv.rows = std::move(rows);

    json next_key = response.value("next_key", json(nullptr));
    if (next_key.is_string() && !next_key.get<std::string>().empty()) {
        const std::string raw = next_key.get<std::string>();
        if (key_type == "name") {
            // names are sent back as an uint64 string instead of a name string..
            try {
                rv.next_key = Name(std::stoull(raw)).toString();
            } catch (...) {
                return err(ErrorKind::Invalid, "Invalid next_key");
            }
        } else if (key_type == "i64" || key_type == "i128" || key_type == "float64" ||
                   key_type == "float128" || key_type == "sha256" || key_type == "ripemd160") {
            rv.next_key = raw;
        } else {
            return err(ErrorKind::Invalid, "Unsupported key type: " + key_type);
        }
    }
    return rv;
}

Result<GetTableByScopeResponse> ChainAPI::get_table_by_scope(const json& params) {
    return client_->call<GetTableByScopeResponse>(
        {.path = "/v1/chain/get_table_by_scope", .params = params});
}

Result<GetTransactionStatusResponse> ChainAPI::get_transaction_status(const Checksum256& id) {
    return client_->call<GetTransactionStatusResponse>(
        {.path = "/v1/chain/get_transaction_status", .params = json{{"id", id.hexString()}}});
}

}  // namespace dwarfkit
