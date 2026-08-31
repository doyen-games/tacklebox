#include <dwarfkit/atomicassets/endpoints.hpp>

namespace dwarfkit::atomic {

namespace {

// JS String(value): arrays join with commas, everything else stringifies.
std::string jsString(const json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_array()) {
        std::string rv;
        for (size_t i = 0; i < value.size(); i++) {
            if (i > 0) {
                rv += ",";
            }
            rv += jsString(value[i]);
        }
        return rv;
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    if (value.is_null()) {
        return "null";
    }
    return value.dump();
}

// application/x-www-form-urlencoded component (URLSearchParams)
std::string formEncode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string rv;
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '*' || c == '-' || c == '.' || c == '_') {
            rv += static_cast<char>(c);
        } else if (c == ' ') {
            rv += '+';
        } else {
            rv += '%';
            rv += hex[c >> 4];
            rv += hex[c & 0xf];
        }
    }
    return rv;
}

}  // namespace

json serializeQueryParams(const json& params) {
    json result = json::object();
    if (!params.is_object()) {
        return result;
    }
    for (const auto& [key, value] : params.items()) {
        if (value.is_boolean() || value.is_number()) {
            result[key] = value;
        } else {
            result[key] = jsString(value);
        }
    }
    return result;
}

json fixPostArguments(const json& params) {
    // https://github.com/pinknetworkx/eosio-contract-api/issues/131
    json options = params;
    for (const char* key : {"page", "limit", "before", "after", "burned"}) {
        if (options.contains(key)) {
            options[key] = jsString(options[key]);
        }
    }
    return options;
}

json buildBodyParams(const json& params, const json& extra) {
    json options = fixPostArguments(serializeQueryParams(params));
    // Object.assign(extra, options): options overwrite extra
    json rv = extra.is_object() ? extra : json::object();
    for (const auto& [key, value] : options.items()) {
        rv[key] = value;
    }
    return rv;
}

std::string buildQueryParams(const json& params, const json& extra) {
    const json queryParts = buildBodyParams(params, extra);
    if (queryParts.empty()) {
        return "";
    }
    std::string rv = "?";
    bool first = true;
    for (const auto& [key, value] : queryParts.items()) {
        if (!first) {
            rv += "&";
        }
        first = false;
        rv += formEncode(key) + "=" + formEncode(jsString(value));
    }
    return rv;
}

Result<json> EndpointAPI::get(const std::string& path) const {
    return client_->call<json>({.path = path, .params = std::nullopt, .method = "GET"});
}

Result<json> EndpointAPI::post(const std::string& path, const json& body) const {
    return client_->call<json>({.path = path,
                                .params = body,
                                .method = "POST",
                                .headers = {{"Content-Type", "application/json"}}});
}

// ---- assets v1 -------------------------------------------------------------

Result<json> AssetsV1API::get_assets(const json& options, const json& extra) const {
    return post("/atomicassets/v1/assets", buildBodyParams(options, extra));
}
Result<json> AssetsV1API::get_assets_count(const json& options, const json& extra) const {
    return post("/atomicassets/v1/assets/_count", buildBodyParams(options, extra));
}
Result<json> AssetsV1API::get_asset(uint64_t assetId) const {
    return get("/atomicassets/v1/assets/" + std::to_string(assetId));
}
Result<json> AssetsV1API::get_asset_stats(uint64_t assetId) const {
    return get("/atomicassets/v1/assets/" + std::to_string(assetId) + "/stats");
}
Result<json> AssetsV1API::get_asset_logs(uint64_t assetId, const json& options) const {
    return post("/atomicassets/v1/assets/" + std::to_string(assetId) + "/logs",
                buildBodyParams(options));
}
Result<json> AssetsV1API::get_collections(const json& options) const {
    return post("/atomicassets/v1/collections", buildBodyParams(options));
}
Result<json> AssetsV1API::get_collections_count(const json& options) const {
    return post("/atomicassets/v1/collections/_count", buildBodyParams(options));
}
Result<json> AssetsV1API::get_collection(const Name& collectionName) const {
    return get("/atomicassets/v1/collections/" + collectionName.toString());
}
Result<json> AssetsV1API::get_collection_stats(const Name& collectionName) const {
    return get("/atomicassets/v1/collections/" + collectionName.toString() + "/stats");
}
Result<json> AssetsV1API::get_collection_schemas(const Name& collectionName) const {
    return get("/atomicassets/v1/collections/" + collectionName.toString() + "/schemas");
}
Result<json> AssetsV1API::get_collection_logs(const Name& collectionName,
                                              const json& options) const {
    return post("/atomicassets/v1/collections/" + collectionName.toString() + "/logs",
                buildBodyParams(options));
}
Result<json> AssetsV1API::get_schemas(const json& options) const {
    return post("/atomicassets/v1/schemas", buildBodyParams(options));
}
Result<json> AssetsV1API::get_schemas_count(const json& options) const {
    return post("/atomicassets/v1/schemas/_count", buildBodyParams(options));
}
Result<json> AssetsV1API::get_schema(const Name& collectionName, const Name& schemaName) const {
    return get("/atomicassets/v1/schemas/" + collectionName.toString() + "/" +
               schemaName.toString());
}
Result<json> AssetsV1API::get_schema_stats(const Name& collectionName,
                                           const Name& schemaName) const {
    return get("/atomicassets/v1/schemas/" + collectionName.toString() + "/" +
               schemaName.toString() + "/stats");
}
Result<json> AssetsV1API::get_schema_logs(const Name& collectionName, const Name& schemaName,
                                          const json& options) const {
    return post("/atomicassets/v1/schemas/" + collectionName.toString() + "/" +
                    schemaName.toString() + "/logs",
                buildBodyParams(options));
}
Result<json> AssetsV1API::get_templates(const json& options, const json& extra) const {
    return post("/atomicassets/v1/templates", buildBodyParams(options, extra));
}
Result<json> AssetsV1API::get_templates_count(const json& options, const json& extra) const {
    return post("/atomicassets/v1/templates/_count", buildBodyParams(options, extra));
}
Result<json> AssetsV1API::get_template(const Name& collectionName, int32_t templateId) const {
    return get("/atomicassets/v1/templates/" + collectionName.toString() + "/" +
               std::to_string(templateId));
}
Result<json> AssetsV1API::get_template_stats(int32_t templateId) const {
    return get("/atomicassets/v1/templates/" + std::to_string(templateId) + "/stats");
}
Result<json> AssetsV1API::get_template_stats(const Name& collectionName,
                                             int32_t templateId) const {
    return get("/atomicassets/v1/templates/" + collectionName.toString() + "/" +
               std::to_string(templateId) + "/stats");
}
Result<json> AssetsV1API::get_template_logs(const Name& collectionName, int32_t templateId,
                                            const json& options) const {
    return post("/atomicassets/v1/templates/" + collectionName.toString() + "/" +
                    std::to_string(templateId) + "/logs",
                buildBodyParams(options));
}
Result<json> AssetsV1API::get_offers(const json& options) const {
    return post("/atomicassets/v1/offers", buildBodyParams(options));
}
Result<json> AssetsV1API::get_offers_count(const json& options) const {
    return post("/atomicassets/v1/offers/_count", buildBodyParams(options));
}
Result<json> AssetsV1API::get_offer(uint64_t offerId) const {
    return get("/atomicassets/v1/offers/" + std::to_string(offerId));
}
Result<json> AssetsV1API::get_offer_logs(uint64_t offerId, const json& options) const {
    return post("/atomicassets/v1/offers/" + std::to_string(offerId) + "/logs",
                buildBodyParams(options));
}
Result<json> AssetsV1API::get_transfers(const json& options) const {
    return post("/atomicassets/v1/transfers", buildBodyParams(options));
}
Result<json> AssetsV1API::get_transfers_count(const json& options) const {
    return post("/atomicassets/v1/transfers/_count", buildBodyParams(options));
}
Result<json> AssetsV1API::get_accounts(const json& options) const {
    return post("/atomicassets/v1/accounts", buildBodyParams(options));
}
Result<json> AssetsV1API::get_accounts_count(const json& options) const {
    return post("/atomicassets/v1/accounts/_count", buildBodyParams(options));
}
Result<json> AssetsV1API::get_account(const Name& account, const json& options) const {
    return post("/atomicassets/v1/accounts/" + account.toString(), buildBodyParams(options));
}
Result<json> AssetsV1API::get_account_template_schema_count(const Name& account,
                                                            const Name& collectionName) const {
    return get("/atomicassets/v1/accounts/" + account.toString() + "/" +
               collectionName.toString());
}
Result<json> AssetsV1API::get_burns(const json& options) const {
    return post("/atomicassets/v1/burns", buildBodyParams(options));
}
Result<json> AssetsV1API::get_account_burns(const Name& account, const json& options) const {
    return post("/atomicassets/v1/burns/" + account.toString(), buildBodyParams(options));
}
Result<json> AssetsV1API::get_config() const {
    return get("/atomicassets/v1/config");
}

// ---- market v1 -------------------------------------------------------------

Result<json> MarketV1API::get_assets(const json& options, const json& extra) const {
    return post("/atomicmarket/v1/assets", buildBodyParams(options, extra));
}
Result<json> MarketV1API::get_assets_count(const json& options, const json& extra) const {
    return post("/atomicmarket/v1/assets/_count", buildBodyParams(options, extra));
}
Result<json> MarketV1API::get_asset(uint64_t assetId) const {
    return get("/atomicmarket/v1/assets/" + std::to_string(assetId));
}
Result<json> MarketV1API::get_asset_stats(uint64_t assetId) const {
    return get("/atomicmarket/v1/assets/" + std::to_string(assetId) + "/stats");
}
Result<json> MarketV1API::get_asset_logs(uint64_t assetId, const json& options) const {
    return post("/atomicmarket/v1/assets/" + std::to_string(assetId) + "/logs",
                buildBodyParams(options));
}
Result<json> MarketV1API::get_asset_sales(uint64_t assetId, const json& options) const {
    return post("/atomicmarket/v1/assets/" + std::to_string(assetId) + "/sales",
                buildBodyParams(options));
}
Result<json> MarketV1API::get_offers(const json& options) const {
    return post("/atomicmarket/v1/offers", buildBodyParams(options));
}
Result<json> MarketV1API::get_offer(uint64_t offerId) const {
    return get("/atomicmarket/v1/offers/" + std::to_string(offerId));
}
Result<json> MarketV1API::get_offer_logs(uint64_t offerId, const json& options) const {
    return post("/atomicmarket/v1/offers/" + std::to_string(offerId) + "/logs",
                buildBodyParams(options));
}
Result<json> MarketV1API::get_transfers(const json& options) const {
    return post("/atomicmarket/v1/transfers", buildBodyParams(options));
}
Result<json> MarketV1API::get_sale(uint64_t saleId) const {
    return get("/atomicmarket/v1/sales/" + std::to_string(saleId));
}
Result<json> MarketV1API::get_sale_logs(uint64_t saleId, const json& options) const {
    return post("/atomicmarket/v1/sales/" + std::to_string(saleId) + "/logs",
                buildBodyParams(options));
}
Result<json> MarketV1API::get_sales_by_templates(const json& options, const json& extra) const {
    return post("/atomicmarket/v1/sales/templates", buildBodyParams(options, extra));
}
Result<json> MarketV1API::get_auctions(const json& options, const json& extra) const {
    return post("/atomicmarket/v1/auctions", buildBodyParams(options, extra));
}
Result<json> MarketV1API::get_auctions_count(const json& options, const json& extra) const {
    return post("/atomicmarket/v1/auctions/_count", buildBodyParams(options, extra));
}
Result<json> MarketV1API::get_auction(uint64_t auctionId) const {
    return get("/atomicmarket/v1/auctions/" + std::to_string(auctionId));
}
Result<json> MarketV1API::get_auction_logs(uint64_t auctionId, const json& options) const {
    return post("/atomicmarket/v1/auctions/" + std::to_string(auctionId) + "/logs",
                buildBodyParams(options));
}
Result<json> MarketV1API::get_buyoffers(const json& options, const json& extra) const {
    return post("/atomicmarket/v1/buyoffers", buildBodyParams(options, extra));
}
Result<json> MarketV1API::get_buyoffers_count(const json& options, const json& extra) const {
    return post("/atomicmarket/v1/buyoffers/_count", buildBodyParams(options, extra));
}
Result<json> MarketV1API::get_buyoffer(uint64_t buyofferId) const {
    return get("/atomicmarket/v1/buyoffers/" + std::to_string(buyofferId));
}
Result<json> MarketV1API::get_buyoffer_logs(uint64_t buyofferId, const json& options) const {
    return post("/atomicmarket/v1/buyoffers/" + std::to_string(buyofferId) + "/logs",
                buildBodyParams(options));
}
Result<json> MarketV1API::get_template_buyoffers(const json& options,
                                                 const json& extra) const {
    return post("/atomicmarket/v1/template_buyoffers", buildBodyParams(options, extra));
}
Result<json> MarketV1API::get_template_buyoffers_count(const json& options,
                                                       const json& extra) const {
    return post("/atomicmarket/v1/template_buyoffers/_count", buildBodyParams(options, extra));
}
Result<json> MarketV1API::get_template_buyoffer(uint64_t buyofferId) const {
    return get("/atomicmarket/v1/template_buyoffers/" + std::to_string(buyofferId));
}
Result<json> MarketV1API::get_template_buyoffer_logs(uint64_t buyofferId,
                                                     const json& options) const {
    return post("/atomicmarket/v1/template_buyoffers/" + std::to_string(buyofferId) + "/logs",
                buildBodyParams(options));
}
Result<json> MarketV1API::get_marketplaces() const {
    return get("/atomicmarket/v1/marketplaces");
}
Result<json> MarketV1API::get_marketplace(const std::string& marketplaceName) const {
    return get("/atomicmarket/v1/marketplaces/" + marketplaceName);
}
Result<json> MarketV1API::get_sale_prices(const json& options) const {
    return post("/atomicmarket/v1/prices/sales", buildBodyParams(options));
}
Result<json> MarketV1API::get_sale_prices_by_days(const json& options) const {
    return post("/atomicmarket/v1/prices/sales/days", buildBodyParams(options));
}
Result<json> MarketV1API::get_template_prices(const json& options) const {
    return post("/atomicmarket/v1/prices/templates", buildBodyParams(options));
}
Result<json> MarketV1API::get_asset_prices(const json& options) const {
    return post("/atomicmarket/v1/prices/assets", buildBodyParams(options));
}
Result<json> MarketV1API::get_inventory_prices(const Name& account,
                                               const json& options) const {
    return post("/atomicmarket/v1/prices/inventory/" + account.toString(),
                buildBodyParams(options));
}
Result<json> MarketV1API::get_stats_collections(const json& options) const {
    return post("/atomicmarket/v1/stats/collections", buildBodyParams(options));
}
Result<json> MarketV1API::get_stats_collection(const Name& collectionName,
                                               const json& options) const {
    return post("/atomicmarket/v1/stats/collections/" + collectionName.toString(),
                buildBodyParams(options));
}
Result<json> MarketV1API::get_stats_accounts(const json& options) const {
    return post("/atomicmarket/v1/stats/accounts", buildBodyParams(options));
}
Result<json> MarketV1API::get_stats_account(const Name& account, const json& options) const {
    return post("/atomicmarket/v1/stats/accounts/" + account.toString(),
                buildBodyParams(options));
}
Result<json> MarketV1API::get_stats_schemas(const Name& collectionName,
                                            const json& options) const {
    return post("/atomicmarket/v1/stats/schemas/" + collectionName.toString(),
                buildBodyParams(options));
}
Result<json> MarketV1API::get_stats_templates(const json& options) const {
    return post("/atomicmarket/v1/stats/templates", buildBodyParams(options));
}
Result<json> MarketV1API::get_stats_graph(const json& options) const {
    return post("/atomicmarket/v1/stats/graph", buildBodyParams(options));
}
Result<json> MarketV1API::get_stats_sales(const json& options) const {
    return post("/atomicmarket/v1/stats/sales", buildBodyParams(options));
}
Result<json> MarketV1API::get_config() const {
    return get("/atomicmarket/v1/config");
}

// ---- market v2 -------------------------------------------------------------

Result<json> MarketV2API::get_sales(const json& options, const json& extra) const {
    return post("/atomicmarket/v2/sales", buildBodyParams(options, extra));
}
Result<json> MarketV2API::get_sales_count(const json& options, const json& extra) const {
    return post("/atomicmarket/v2/sales/_count", buildBodyParams(options, extra));
}
Result<json> MarketV2API::get_stats_schemas(const Name& collectionName,
                                            const json& options) const {
    return post("/atomicmarket/v2/stats/schemas/" + collectionName.toString(),
                buildBodyParams(options));
}

// ---- tools v1 --------------------------------------------------------------

Result<json> ToolsV1API::get_links(const json& options) const {
    return post("/atomictools/v1/links", buildBodyParams(options));
}
Result<json> ToolsV1API::get_links_count(const json& options) const {
    return post("/atomictools/v1/links/_count", buildBodyParams(options));
}
Result<json> ToolsV1API::get_link(uint64_t linkId) const {
    return get("/atomictools/v1/links/" + std::to_string(linkId));
}
Result<json> ToolsV1API::get_link_logs(uint64_t linkId, const json& options) const {
    return post("/atomictools/v1/links/" + std::to_string(linkId) + "/logs",
                buildBodyParams(options));
}
Result<json> ToolsV1API::get_config() const {
    return get("/atomictools/v1/config");
}

}  // namespace dwarfkit::atomic
