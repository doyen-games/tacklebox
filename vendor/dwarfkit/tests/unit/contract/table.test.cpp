// Port of contract test/tests/{table,cursor}.ts (json rows; typed-row cases
// become field checks).
#include <doctest/doctest.h>

#include <dwarfkit/contract.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

std::shared_ptr<APIClient> eosClient() {
    return makeClient(DK_FIXTURE_DIR "/contract/data", "https://eos.greymass.com");
}
std::shared_ptr<APIClient> jungleClient() {
    return makeClient(DK_FIXTURE_DIR "/contract/data", "https://jungle4.greymass.com");
}
std::shared_ptr<APIClient> waxClient() {
    return makeClient(DK_FIXTURE_DIR "/contract/data", "https://wax.greymass.com");
}

std::vector<int64_t> rowIds(const std::vector<json>& rows) {
    std::vector<int64_t> rv;
    for (const auto& row : rows) {
        rv.push_back(row.value("id", int64_t(-1)));
    }
    return rv;
}

struct Fixture {
    ContractKit kit{{.client = eosClient()}};
    Contract eosio = kit.load(Name::from("eosio")).value();
    Contract decentiumorg = kit.load(Name::from("decentiumorg")).value();
    Table nameBidTable = eosio.table(Name::from("namebids")).value();
    Table producersTable = eosio.table(Name::from("producers")).value();
    Table decentiumTrendingTable = decentiumorg.table(Name::from("trending")).value();
};

}  // namespace

TEST_SUITE("contract-table") {
    TEST_CASE("construct") {
        Fixture f;
        SUBCASE("defaults") {
            const auto table = Table::from({.abi = f.eosio.abi,
                                            .account = Name::from("eosio"),
                                            .client = eosClient(),
                                            .name = Name::from("namebids")});
            CHECK(table.has_value());
        }
        SUBCASE("errors on a table name not in the contract") {
            const auto table = Table::from({.abi = f.eosio.abi,
                                            .account = f.eosio.account,
                                            .client = eosClient(),
                                            .name = Name::from("foo")});
            CHECK_FALSE(table.has_value());
        }
        SUBCASE("defaultScope") {
            const auto table = Table::from({.abi = f.decentiumorg.abi,
                                            .account = f.decentiumorg.account,
                                            .client = eosClient(),
                                            .name = Name::from("trending"),
                                            .defaultScope = "foo"})
                                   .value();
            CHECK(table.defaultScope == "foo");
        }
    }

    TEST_CASE("cursor basics") {
        Fixture f;
        SUBCASE("all returns every row in a table") {
            auto cursor = f.decentiumTrendingTable.query().value();
            CHECK(cursor.all().value().size() == 239);
        }
        SUBCASE("next fetches as many rows as possible with one request") {
            auto cursor = f.decentiumTrendingTable.query().value();
            CHECK(cursor.next().value().size() == 239);
        }
        SUBCASE("next fetches more rows after the first request") {
            auto cursor = f.nameBidTable.query().value();
            CHECK(cursor.next().value().size() == 1000);
            CHECK(cursor.next().value().size() == 1000);
            CHECK(cursor.next().value().size() == 1000);
        }
        SUBCASE("reset") {
            QueryParams params;
            params.from = 5;
            params.to = 6;
            auto cursor = f.decentiumTrendingTable.query(params).value();
            CHECK(rowIds(cursor.next().value()) == std::vector<int64_t>{5, 6});
            CHECK(cursor.next().value().empty());
            cursor.reset();
            CHECK(rowIds(cursor.next().value()) == std::vector<int64_t>{5, 6});
        }
    }

    TEST_CASE("query") {
        Fixture f;
        SUBCASE("filters by named index") {
            QueryParams params;
            params.from = 101511;
            params.to = 105056;
            params.index = "score";
            auto cursor = f.decentiumTrendingTable.query(params).value();
            const auto rows = cursor.all().value();
            std::vector<int64_t> scores;
            for (const auto& row : rows) {
                scores.push_back(row.value("score", int64_t(-1)));
            }
            CHECK(scores ==
                  std::vector<int64_t>{101511, 102465, 102507, 103688, 103734, 105056});
        }
        SUBCASE("filters by index_position") {
            QueryParams params;
            params.from = 101511;
            params.to = 105056;
            params.index_position = "secondary";
            auto cursor = f.decentiumTrendingTable.query(params).value();
            CHECK(cursor.all().value().size() == 6);
        }
        SUBCASE("fetches all rows with a scope") {
            ContractKit kit({.client = jungleClient()});
            const auto contract = kit.load(Name::from("eosio")).value();
            QueryParams params;
            params.scope = "wharfkittest";
            const auto rows =
                contract.table(Name::from("delband")).value().query(params).value().all().value();
            REQUIRE(rows.size() == 40);
            // decoded rows carry the table's fields
            CHECK(rows[0].contains("from"));
            CHECK(rows[0].contains("cpu_weight"));
        }
        SUBCASE("next(2) pages through bounded results") {
            QueryParams params;
            params.from = 5;
            params.to = 10;
            auto cursor = f.decentiumTrendingTable.query(params).value();
            CHECK(rowIds(cursor.next(2).value()) == std::vector<int64_t>{5, 6});
            CHECK(rowIds(cursor.next(2).value()) == std::vector<int64_t>{7, 8});
            CHECK(rowIds(cursor.next(2).value()) == std::vector<int64_t>{9, 10});
            CHECK(cursor.next(2).value().empty());
        }
        SUBCASE("rowsPerAPIRequest pages through bounded results") {
            QueryParams params;
            params.from = 5;
            params.to = 10;
            params.rowsPerAPIRequest = 2;
            auto cursor = f.decentiumTrendingTable.query(params).value();
            CHECK(rowIds(cursor.next().value()) == std::vector<int64_t>{5, 6});
            CHECK(rowIds(cursor.next().value()) == std::vector<int64_t>{7, 8});
            CHECK(rowIds(cursor.next().value()) == std::vector<int64_t>{9, 10});
            CHECK(cursor.next().value().empty());
        }
        SUBCASE("reverse") {
            QueryParams params;
            params.from = 5;
            params.to = 6;
            params.reverse = true;
            auto cursor = f.decentiumTrendingTable.query(params).value();
            CHECK(rowIds(cursor.next().value()) == std::vector<int64_t>{6, 5});
        }
    }

    TEST_CASE("numeric scopes") {
        Fixture f;
        SUBCASE("numeric and text scopes hit the same rows") {
            ContractKit kit({.client = waxClient()});
            const auto contract = kit.load(Name::from("alcordexmain")).value();
            QueryParams numeric;
            numeric.scope = 0;
            CHECK(contract.table(Name::from("buyorder"))
                      .value()
                      .query(numeric)
                      .value()
                      .all()
                      .value()
                      .size() == 144);
            CHECK(contract.table(Name::from("sellorder"))
                      .value()
                      .query(numeric)
                      .value()
                      .all()
                      .value()
                      .size() == 348);
            QueryParams text;
            text.scope = "0";
            CHECK(contract.table(Name::from("buyorder"))
                      .value()
                      .query(text)
                      .value()
                      .all()
                      .value()
                      .size() == 144);
        }
        SUBCASE("keeps a uint64 scope beyond the range of a number") {
            QueryParams params;
            params.scope = "9223372036854775808";
            const auto cursor = f.producersTable.query(params).value();
            CHECK(cursor.params["scope"] == "9223372036854775808");
        }
        SUBCASE("rejects a number scope that cannot hold the value") {
            QueryParams params;
            params.scope = 9223372036854775808.0;
            CHECK_FALSE(f.producersTable.query(params).has_value());
        }
        SUBCASE("defaults to the contract account") {
            const auto cursor = f.producersTable.query().value();
            CHECK(cursor.params["scope"] == "eosio");
        }
        SUBCASE("keeps a scope of zero from the table call") {
            const auto table = f.eosio.table(Name::from("producers"), 0).value();
            CHECK(table.query().value().params["scope"] == 0);
        }
        SUBCASE("falls back to the default scope for an absent query scope") {
            const auto table = f.eosio.table(Name::from("producers"), 42).value();
            CHECK(table.query().value().params["scope"] == 42);
            QueryParams empty;
            empty.scope = "";
            CHECK(table.query(empty).value().params["scope"] == 42);
        }
        SUBCASE("falls back to the contract account for an empty default scope") {
            const auto table = f.eosio.table(Name::from("producers"), "").value();
            CHECK(table.query().value().params["scope"] == "eosio");
        }
    }

    TEST_CASE("get") {
        Fixture f;
        SUBCASE("by primary index with key_type") {
            QueryParams params;
            params.key_type = "i64";
            const auto row = f.decentiumTrendingTable.get(5, params).value();
            REQUIRE(row.has_value());
            CHECK((*row)["id"] == 5);
            CHECK((*row)["score"] == 102465);
            CHECK((*row)["ref"]["permlink"]["author"] == "eosfilestore");
        }
        SUBCASE("by named index") {
            QueryParams params;
            params.index = "score";
            const auto row = f.decentiumTrendingTable.get(102465, params).value();
            REQUIRE(row.has_value());
            CHECK((*row)["id"] == 5);
        }
        SUBCASE("with default filtering") {
            const auto row = f.producersTable.get("teamgreymass").value();
            REQUIRE(row.has_value());
            CHECK((*row)["owner"] == "teamgreymass");
        }
        SUBCASE("first row without params") {
            const auto table = f.eosio.table(Name::from("global")).value();
            const auto row = table.get().value();
            REQUIRE(row.has_value());
            CHECK(row->contains("pervote_bucket"));
        }
        SUBCASE("scope from the table call") {
            ContractKit kit({.client = jungleClient()});
            const auto contract = kit.load(Name::from("eosio.token")).value();
            const auto row =
                contract.table(Name::from("accounts"), "wharfkittest").value().get().value();
            REQUIRE(row.has_value());
            CHECK(row->contains("balance"));
        }
        SUBCASE("returns nullopt when no entry is found") {
            const auto row = f.producersTable.get("doesnotexist").value();
            CHECK_FALSE(row.has_value());
        }
    }

    TEST_CASE("maxRows") {
        Fixture f;
        SUBCASE("bounds a query") {
            QueryParams params;
            params.maxRows = 10;
            auto cursor = f.decentiumTrendingTable.query(params).value();
            const auto rows = cursor.next().value();
            CHECK(rows.size() == 10);
            CHECK(rowIds(rows) == std::vector<int64_t>{0, 1, 2, 3, 5, 6, 7, 8, 9, 10});
            CHECK(cursor.next().value().empty());
        }
        SUBCASE("scope defaults differ between queries") {
            const auto table = f.eosio.table(Name::from("delband")).value();
            QueryParams params;
            params.maxRows = 10;
            CHECK(table.query(params).value().all().value().size() == 2);
            QueryParams scoped;
            scoped.maxRows = 10;
            scoped.scope = "teamgreymass";
            CHECK(table.query(scoped).value().all().value().size() == 3);
        }
        SUBCASE("all stops at maxRows") {
            QueryParams params;
            params.maxRows = 10000;
            auto cursor = f.nameBidTable.query(params).value();
            CHECK(cursor.all().value().size() == 10000);
        }
        SUBCASE("all stops when fewer rows exist") {
            QueryParams params;
            params.maxRows = 10000;
            auto cursor = f.decentiumTrendingTable.query(params).value();
            CHECK(cursor.all().value().size() == 239);
        }
    }

    TEST_CASE("scopes cursor") {
        Fixture f;
        ContractKit kit({.client = eosClient()});
        const auto msig = kit.load(Name::from("eosio.msig")).value();
        const auto proposalTable = msig.table(Name::from("proposal")).value();
        SUBCASE("all scopes") {
            auto cursor = proposalTable.scopes().value();
            const auto rows = cursor.next().value();
            CHECK(!rows.empty());
            CHECK(rows[0].contains("scope"));
        }
        SUBCASE("bounded scopes") {
            QueryParams params;
            params.from = "teamgreymass";
            params.to = "telosdompet1";
            auto cursor = proposalTable.scopes().value();
            (void)cursor;
            auto bounded = proposalTable.scopes(params).value();
            const auto rows = bounded.all().value();
            CHECK(!rows.empty());
            for (const auto& row : rows) {
                CHECK(row["code"] == "eosio.msig");
            }
        }
        SUBCASE("paged scopes") {
            QueryParams params;
            params.rowsPerAPIRequest = 2;
            auto cursor = proposalTable.scopes(params).value();
            const auto first = cursor.next().value();
            CHECK(first.size() == 2);
            const auto second = cursor.next().value();
            CHECK(second.size() == 2);
        }
    }
}
