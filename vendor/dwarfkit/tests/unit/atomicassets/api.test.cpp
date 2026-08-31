// Port of atomicassets test/{api,v2}.ts: every endpoint against the recorded
// eosio-contract-api fixtures (bodies must hash to the recordings).
#include <doctest/doctest.h>

#include <dwarfkit/atomicassets.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

atomic::AtomicAssetsAPIClient makeAtomic() {
    return atomic::AtomicAssetsAPIClient(
        makeClient(DK_FIXTURE_DIR "/atomicassets/data", "https://wax-atomic.alcor.exchange"));
}

void ok(const Result<json>& res) {
    const std::string error = res ? "" : res.error().message;
    REQUIRE_MESSAGE(res.has_value(), error);
    CHECK((*res)["success"] == true);
    CHECK(!(*res)["data"].empty());
}

}  // namespace

TEST_SUITE("atomicassets-api") {
    TEST_CASE("atomicassets v1") {
        const auto api = makeAtomic();
        const auto& v1 = api.atomicassets.v1;
        SUBCASE("get_config") { ok(v1.get_config()); }
        SUBCASE("get_accounts") {
            ok(v1.get_accounts(json{{"collection_name", json::array({"taco", "alien.worlds"})},
                                    {"owner", json::array({"taco"})},
                                    {"limit", 10}}));
        }
        SUBCASE("get_accounts_count") {
            ok(v1.get_accounts_count(
                json{{"collection_name", json::array({"taco", "alien.worlds"})},
                     {"owner", json::array({"taco"})}}));
        }
        SUBCASE("get_account") { ok(v1.get_account("taco"_n)); }
        SUBCASE("get_account_template_schema_count") {
            ok(v1.get_account_template_schema_count("taco"_n, "taco"_n));
        }
        SUBCASE("get_collections") {
            ok(v1.get_collections(json{{"author", json::array({"taco", "alien.worlds"})},
                                       {"limit", 10}}));
        }
        SUBCASE("get_collections_count") {
            ok(v1.get_collections_count(
                json{{"author", json::array({"taco", "alien.worlds"})}}));
        }
        SUBCASE("get_collection") { ok(v1.get_collection("taco"_n)); }
        SUBCASE("get_collection img optional") { ok(v1.get_collection("testfighters"_n)); }
        SUBCASE("get_collection_stats") { ok(v1.get_collection_stats("taco"_n)); }
        SUBCASE("get_collection_logs") {
            ok(v1.get_collection_logs("taco"_n, json{{"limit", 10}}));
        }
        SUBCASE("get_schemas") {
            ok(v1.get_schemas(json{{"collection_name", json::array({"taco", "alien.worlds"})},
                                   {"limit", 10}}));
        }
        SUBCASE("get_schemas_count") {
            ok(v1.get_schemas_count(
                json{{"collection_name", json::array({"taco", "alien.worlds"})}}));
        }
        SUBCASE("get_schema") { ok(v1.get_schema("taco"_n, "cmbz.res"_n)); }
        SUBCASE("get_schema_stats") { ok(v1.get_schema_stats("taco"_n, "cmbz.res"_n)); }
        SUBCASE("get_schema_logs") {
            ok(v1.get_schema_logs("taco"_n, "cmbz.res"_n, json{{"limit", 10}}));
        }
        SUBCASE("get_templates") {
            ok(v1.get_templates(json{{"collection_name", json::array({"taco", "alien.worlds"})},
                                     {"limit", 10}}));
        }
        SUBCASE("get_templates_count") {
            ok(v1.get_templates_count(
                json{{"collection_name", json::array({"taco", "alien.worlds"})}}));
        }
        SUBCASE("get_template") { ok(v1.get_template("taco"_n, 750150)); }
        SUBCASE("get_template_stats") { ok(v1.get_template_stats("taco"_n, 750150)); }
        SUBCASE("get_template_logs") {
            ok(v1.get_template_logs("taco"_n, 750150, json{{"limit", 10}}));
        }
        SUBCASE("get_assets") {
            ok(v1.get_assets(json{{"collection_name", json::array({"taco", "alien.worlds"})},
                                  {"owner", json::array({"taco"})},
                                  {"limit", 10}}));
        }
        SUBCASE("get_assets_count") {
            ok(v1.get_assets_count(
                json{{"collection_name", json::array({"taco", "alien.worlds"})},
                     {"owner", json::array({"taco"})}}));
        }
        SUBCASE("get_asset") { ok(v1.get_asset(1099851897196ull)); }
        SUBCASE("get_asset_stats") { ok(v1.get_asset_stats(1099851897196ull)); }
        SUBCASE("get_asset_logs") {
            ok(v1.get_asset_logs(
                1099851897196ull,
                json{{"action_whitelist",
                      json::array({"logmint", "mintasset", "logtransfer"})},
                     {"limit", 10}}));
        }
        SUBCASE("get_offers") {
            ok(v1.get_offers(json{{"collection_name", json::array({"taco", "alien.worlds"})},
                                  {"limit", 10}}));
        }
        SUBCASE("get_offer") { ok(v1.get_offer(22820296ull)); }
        SUBCASE("get_offer_logs") { ok(v1.get_offer_logs(22820296ull, json{{"limit", 10}})); }
        SUBCASE("get_transfers") {
            ok(v1.get_transfers(json{{"account", json::array({"taco"})},
                                     {"collection_name", json::array({"taco", "alien.worlds"})},
                                     {"limit", 10}}));
        }
        SUBCASE("get_transfers_count") {
            ok(v1.get_transfers_count(
                json{{"account", json::array({"taco"})},
                     {"collection_name", json::array({"taco", "alien.worlds"})}}));
        }
        SUBCASE("get_burns") {
            ok(v1.get_burns(json{{"collection_name", json::array({"thesvnthseal"})}}));
        }
        SUBCASE("get_account_burns") { ok(v1.get_account_burns("taco"_n)); }
    }

    TEST_CASE("atomictools v1") {
        const auto api = makeAtomic();
        const auto& v1 = api.atomictools.v1;
        SUBCASE("get_links") {
            ok(v1.get_links(json{{"creator", json::array({"taco", "federation"})},
                                 {"limit", 10}}));
        }
        SUBCASE("get_links_count") {
            ok(v1.get_links_count(json{{"creator", json::array({"taco", "federation"})}}));
        }
        SUBCASE("get_link") { ok(v1.get_link(1451754ull)); }
        SUBCASE("get_link_logs") { ok(v1.get_link_logs(1451754ull, json{{"limit", 10}})); }
        SUBCASE("get_config") { ok(v1.get_config()); }
    }

    TEST_CASE("atomicmarket v1") {
        const auto api = makeAtomic();
        const auto& v1 = api.atomicmarket.v1;
        SUBCASE("get_assets") {
            ok(v1.get_assets(json{{"collection_name", json::array({"taco", "alien.worlds"})},
                                  {"owner", json::array({"taco"})},
                                  {"limit", 10}}));
        }
        SUBCASE("get_assets_count") {
            ok(v1.get_assets_count(
                json{{"collection_name", json::array({"taco", "alien.worlds"})},
                     {"owner", json::array({"taco"})}}));
        }
        SUBCASE("get_asset") { ok(v1.get_asset(1099851897196ull)); }
        SUBCASE("get_asset_with_active_sale") { ok(v1.get_asset(1099513214175ull)); }
        SUBCASE("get_asset_stats") { ok(v1.get_asset_stats(1099851897196ull)); }
        SUBCASE("get_asset_logs") {
            ok(v1.get_asset_logs(
                1099851897196ull,
                json{{"action_whitelist",
                      json::array({"logmint", "mintasset", "logtransfer"})},
                     {"limit", 10}}));
        }
        SUBCASE("get_asset_sales") { ok(v1.get_asset_sales(1099922666976ull)); }
        SUBCASE("get_offers") {
            ok(v1.get_offers(json{{"collection_name", json::array({"taco", "alien.worlds"})},
                                  {"limit", 10}}));
        }
        SUBCASE("get_offer") { ok(v1.get_offer(22820296ull)); }
        SUBCASE("get_offer_logs") { ok(v1.get_offer_logs(22820296ull, json{{"limit", 10}})); }
        SUBCASE("get_transfers") {
            ok(v1.get_transfers(json{{"account", json::array({"taco"})},
                                     {"collection_name", json::array({"taco", "alien.worlds"})},
                                     {"limit", 10}}));
        }
        SUBCASE("get_sale") { ok(v1.get_sale(89024803ull)); }
        SUBCASE("get_sale_logs") { ok(v1.get_sale_logs(89024803ull, json{{"limit", 10}})); }
        SUBCASE("get_sales_by_templates") {
            ok(v1.get_sales_by_templates(json{{"symbol", "WAX"},
                                              {"collection_name", json::array({"taco"})},
                                              {"limit", 10}}));
        }
        SUBCASE("get_auctions") {
            ok(v1.get_auctions(json{{"collection_name", json::array({"taco", "alien.worlds"})},
                                    {"limit", 10}}));
        }
        SUBCASE("get_auctions_count") {
            ok(v1.get_auctions_count(
                json{{"collection_name", json::array({"taco", "bcbrawlers"})}}));
        }
        SUBCASE("get_auction") { ok(v1.get_auction(1301765ull)); }
        SUBCASE("get_auction_logs") {
            ok(v1.get_auction_logs(1301765ull, json{{"limit", 10}}));
        }
        SUBCASE("get_buyoffers") {
            ok(v1.get_buyoffers(json{{"collection_name", json::array({"taco"})},
                                     {"limit", 10}}));
        }
        SUBCASE("get_buyoffers_count") {
            ok(v1.get_buyoffers_count(json{{"collection_name", json::array({"taco"})}}));
        }
        SUBCASE("get_buyoffer") { ok(v1.get_buyoffer(2432258ull)); }
        SUBCASE("get_buyoffer_logs") {
            ok(v1.get_buyoffer_logs(2432258ull, json{{"limit", 10}}));
        }
        SUBCASE("get_template_buyoffers") {
            ok(v1.get_template_buyoffers(
                json{{"template_id", json::array({443565})}, {"limit", 10}}));
        }
        SUBCASE("get_template_buyoffers_count") {
            ok(v1.get_template_buyoffers_count(
                json{{"template_id", json::array({443565})}, {"state", json::array({1})}}));
        }
        SUBCASE("get_template_buyoffer") { ok(v1.get_template_buyoffer(284083ull)); }
        SUBCASE("get_template_buyoffer_logs") {
            ok(v1.get_template_buyoffer_logs(284083ull, json{{"limit", 10}}));
        }
        SUBCASE("get_marketplaces") { ok(v1.get_marketplaces()); }
        SUBCASE("get_marketplace") { ok(v1.get_marketplace("market.place")); }
        SUBCASE("get_sale_prices") {
            ok(v1.get_sale_prices(json{{"collection_name", json::array({"taco"})}}));
        }
        SUBCASE("get_sale_prices_by_days") {
            ok(v1.get_sale_prices_by_days(json{{"collection_name", json::array({"taco"})}}));
        }
        SUBCASE("get_template_prices") {
            ok(v1.get_template_prices(
                json{{"collection_name", json::array({"taco"})}, {"limit", 10}}));
        }
        SUBCASE("get_asset_prices") {
            ok(v1.get_asset_prices(json{{"collection_name", json::array({"taco"})},
                                        {"asset_id", json::array({1099922666976ull})},
                                        {"limit", 10}}));
        }
        SUBCASE("get_inventory_prices") {
            ok(v1.get_inventory_prices("taco"_n,
                                       json{{"collection_name", json::array({"taco"})}}));
        }
        SUBCASE("get_stats_collections") {
            ok(v1.get_stats_collections(json{{"symbol", "WAX"},
                                             {"search", "taco"},
                                             {"collection_name", json::array({"taco"})},
                                             {"limit", 10}}));
        }
        SUBCASE("get_stats_collection") {
            ok(v1.get_stats_collection("taco"_n, json{{"symbol", "WAX"}}));
        }
        SUBCASE("get_stats_accounts") {
            ok(v1.get_stats_accounts(json{{"symbol", "WAX"},
                                          {"collection_name", json::array({"taco"})},
                                          {"limit", 10}}));
        }
        SUBCASE("get_stats_account") {
            ok(v1.get_stats_account("taco"_n, json{{"symbol", "WAX"}}));
        }
        SUBCASE("get_stats_schemas") {
            ok(v1.get_stats_schemas("award.worlds"_n,
                                    json{{"symbol", "WAX"}, {"limit", 10}}));
        }
        SUBCASE("get_stats_templates") {
            ok(v1.get_stats_templates(json{{"symbol", "WAX"},
                                           {"search", "alien.worlds"},
                                           {"collection_name", json::array({"alien.worlds"})},
                                           {"limit", 10}}));
        }
        SUBCASE("get_stats_graph") {
            ok(v1.get_stats_graph(
                json{{"symbol", "WAX"}, {"collection_whitelist", json::array({"taco"})}}));
        }
        SUBCASE("get_stats_sales") {
            ok(v1.get_stats_sales(json{{"symbol", "WAX"},
                                       {"collection_whitelist", json::array({"award.worlds"})}}));
        }
        SUBCASE("get_config") { ok(v1.get_config()); }
    }

    TEST_CASE("atomicmarket v2") {
        const auto api = makeAtomic();
        const auto& v2 = api.atomicmarket.v2;
        SUBCASE("get_sales") {
            ok(v2.get_sales(json{{"collection_name", json::array({"taco", "alien.worlds"})},
                                 {"buyer", json::array({"taco"})},
                                 {"limit", 10}}));
        }
        SUBCASE("get_sales_count") {
            ok(v2.get_sales_count(
                json{{"collection_name", json::array({"taco", "alien.worlds"})},
                     {"buyer", json::array({"taco"})}}));
        }
        SUBCASE("get_stats_schemas") {
            ok(v2.get_stats_schemas("taco"_n, json{{"symbol", "WAX"}, {"limit", 10}}));
        }
    }

    TEST_CASE("query param helpers") {
        CHECK(atomic::serializeQueryParams(json{{"a", json::array({"x", "y"})},
                                                {"b", 5},
                                                {"c", true},
                                                {"d", "z"}}) ==
              json{{"a", "x,y"}, {"b", 5}, {"c", true}, {"d", "z"}});
        CHECK(atomic::fixPostArguments(json{{"limit", 10}, {"other", 3}}) ==
              json{{"limit", "10"}, {"other", 3}});
        CHECK(atomic::buildQueryParams(json{{"match", "some words"}, {"limit", 10}}) ==
              "?match=some+words&limit=10");
        CHECK(atomic::buildQueryParams(json::object()) == "");
    }
}
