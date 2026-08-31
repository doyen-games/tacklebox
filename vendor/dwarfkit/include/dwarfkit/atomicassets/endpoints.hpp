// Port of atomicassets src/endpoints (assets/market/tools API clients over
// the eosio-contract-api). Options and responses are json; the wire format
// (paths, bodies, header) matches the upstream fixtures byte for byte.
#pragma once

#include <dwarfkit/antelope.hpp>
#include <dwarfkit/atomicassets/types.hpp>

namespace dwarfkit::atomic {

// endpoints/utils.ts
json serializeQueryParams(const json& params);
json fixPostArguments(const json& params);
json buildBodyParams(const json& params, const json& extra = {});
std::string buildQueryParams(const json& params, const json& extra = {});

class EndpointAPI {
public:
    explicit EndpointAPI(APIClient* client) : client_(client) {}

protected:
    Result<json> get(const std::string& path) const;
    Result<json> post(const std::string& path, const json& body) const;
    APIClient* client_;
};

class AssetsV1API : public EndpointAPI {
public:
    using EndpointAPI::EndpointAPI;
    Result<json> get_assets(const json& options = {}, const json& extra = {}) const;
    Result<json> get_assets_count(const json& options = {}, const json& extra = {}) const;
    Result<json> get_asset(uint64_t assetId) const;
    Result<json> get_asset_stats(uint64_t assetId) const;
    Result<json> get_asset_logs(uint64_t assetId, const json& options = {}) const;
    Result<json> get_collections(const json& options = {}) const;
    Result<json> get_collections_count(const json& options = {}) const;
    Result<json> get_collection(const Name& collectionName) const;
    Result<json> get_collection_stats(const Name& collectionName) const;
    Result<json> get_collection_schemas(const Name& collectionName) const;
    Result<json> get_collection_logs(const Name& collectionName, const json& options = {}) const;
    Result<json> get_schemas(const json& options = {}) const;
    Result<json> get_schemas_count(const json& options = {}) const;
    Result<json> get_schema(const Name& collectionName, const Name& schemaName) const;
    Result<json> get_schema_stats(const Name& collectionName, const Name& schemaName) const;
    Result<json> get_schema_logs(const Name& collectionName, const Name& schemaName,
                                 const json& options = {}) const;
    Result<json> get_templates(const json& options = {}, const json& extra = {}) const;
    Result<json> get_templates_count(const json& options = {}, const json& extra = {}) const;
    Result<json> get_template(const Name& collectionName, int32_t templateId) const;
    Result<json> get_template_stats(int32_t templateId) const;
    Result<json> get_template_stats(const Name& collectionName, int32_t templateId) const;
    Result<json> get_template_logs(const Name& collectionName, int32_t templateId,
                                   const json& options = {}) const;
    Result<json> get_offers(const json& options = {}) const;
    Result<json> get_offers_count(const json& options = {}) const;
    Result<json> get_offer(uint64_t offerId) const;
    Result<json> get_offer_logs(uint64_t offerId, const json& options = {}) const;
    Result<json> get_transfers(const json& options = {}) const;
    Result<json> get_transfers_count(const json& options = {}) const;
    Result<json> get_accounts(const json& options = {}) const;
    Result<json> get_accounts_count(const json& options = {}) const;
    Result<json> get_account(const Name& account, const json& options = {}) const;
    Result<json> get_account_template_schema_count(const Name& account,
                                                   const Name& collectionName) const;
    Result<json> get_burns(const json& options = {}) const;
    Result<json> get_account_burns(const Name& account, const json& options = {}) const;
    Result<json> get_config() const;
};

class MarketV1API : public EndpointAPI {
public:
    using EndpointAPI::EndpointAPI;
    Result<json> get_assets(const json& options = {}, const json& extra = {}) const;
    Result<json> get_assets_count(const json& options = {}, const json& extra = {}) const;
    Result<json> get_asset(uint64_t assetId) const;
    Result<json> get_asset_stats(uint64_t assetId) const;
    Result<json> get_asset_logs(uint64_t assetId, const json& options = {}) const;
    Result<json> get_asset_sales(uint64_t assetId, const json& options = {}) const;
    Result<json> get_offers(const json& options = {}) const;
    Result<json> get_offer(uint64_t offerId) const;
    Result<json> get_offer_logs(uint64_t offerId, const json& options = {}) const;
    Result<json> get_transfers(const json& options = {}) const;
    Result<json> get_sale(uint64_t saleId) const;
    Result<json> get_sale_logs(uint64_t saleId, const json& options = {}) const;
    Result<json> get_sales_by_templates(const json& options = {}, const json& extra = {}) const;
    Result<json> get_auctions(const json& options = {}, const json& extra = {}) const;
    Result<json> get_auctions_count(const json& options = {}, const json& extra = {}) const;
    Result<json> get_auction(uint64_t auctionId) const;
    Result<json> get_auction_logs(uint64_t auctionId, const json& options = {}) const;
    Result<json> get_buyoffers(const json& options = {}, const json& extra = {}) const;
    Result<json> get_buyoffers_count(const json& options = {}, const json& extra = {}) const;
    Result<json> get_buyoffer(uint64_t buyofferId) const;
    Result<json> get_buyoffer_logs(uint64_t buyofferId, const json& options = {}) const;
    Result<json> get_template_buyoffers(const json& options = {}, const json& extra = {}) const;
    Result<json> get_template_buyoffers_count(const json& options = {},
                                              const json& extra = {}) const;
    Result<json> get_template_buyoffer(uint64_t buyofferId) const;
    Result<json> get_template_buyoffer_logs(uint64_t buyofferId,
                                            const json& options = {}) const;
    Result<json> get_marketplaces() const;
    Result<json> get_marketplace(const std::string& marketplaceName) const;
    Result<json> get_sale_prices(const json& options = {}) const;
    Result<json> get_sale_prices_by_days(const json& options = {}) const;
    Result<json> get_template_prices(const json& options = {}) const;
    Result<json> get_asset_prices(const json& options = {}) const;
    Result<json> get_inventory_prices(const Name& account, const json& options = {}) const;
    Result<json> get_stats_collections(const json& options = {}) const;
    Result<json> get_stats_collection(const Name& collectionName,
                                      const json& options = {}) const;
    Result<json> get_stats_accounts(const json& options = {}) const;
    Result<json> get_stats_account(const Name& account, const json& options = {}) const;
    Result<json> get_stats_schemas(const Name& collectionName, const json& options = {}) const;
    Result<json> get_stats_templates(const json& options = {}) const;
    Result<json> get_stats_graph(const json& options = {}) const;
    Result<json> get_stats_sales(const json& options = {}) const;
    Result<json> get_config() const;
};

class MarketV2API : public EndpointAPI {
public:
    using EndpointAPI::EndpointAPI;
    Result<json> get_sales(const json& options = {}, const json& extra = {}) const;
    Result<json> get_sales_count(const json& options = {}, const json& extra = {}) const;
    Result<json> get_stats_schemas(const Name& collectionName, const json& options = {}) const;
};

class ToolsV1API : public EndpointAPI {
public:
    using EndpointAPI::EndpointAPI;
    Result<json> get_links(const json& options = {}) const;
    Result<json> get_links_count(const json& options = {}) const;
    Result<json> get_link(uint64_t linkId) const;
    Result<json> get_link_logs(uint64_t linkId, const json& options = {}) const;
    Result<json> get_config() const;
};

struct AssetsAPIClient {
    explicit AssetsAPIClient(APIClient* client) : v1(client) {}
    AssetsV1API v1;
};

struct MarketAPIClient {
    explicit MarketAPIClient(APIClient* client) : v1(client), v2(client) {}
    MarketV1API v1;
    MarketV2API v2;
};

struct ToolsAPIClient {
    explicit ToolsAPIClient(APIClient* client) : v1(client) {}
    ToolsV1API v1;
};

class AtomicAssetsAPIClient {
    // declared first: the sub-clients below are constructed from it
    std::shared_ptr<APIClient> client_;

public:
    explicit AtomicAssetsAPIClient(std::shared_ptr<APIClient> client)
        : client_(std::move(client)),
          atomicassets(client_.get()),
          atomicmarket(client_.get()),
          atomictools(client_.get()) {}

    AssetsAPIClient atomicassets;
    MarketAPIClient atomicmarket;
    ToolsAPIClient atomictools;
};

}  // namespace dwarfkit::atomic
