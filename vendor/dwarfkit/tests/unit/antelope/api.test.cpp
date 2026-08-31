// Port of antelope test/api.ts, replaying the recorded fixtures through the
// MockProvider. Recording-only cases (MOCK_RECORD env) are not ported.
#include <doctest/doctest.h>

#include <dwarfkit/antelope/api/client.hpp>

#include "../../util/mock_provider.hpp"

using namespace dwarfkit;
namespace v1 = dwarfkit::api::v1;

namespace {

struct ApiTransfer {
    DK_STRUCT("transfer")
    Name from;
    Name to;
    Asset quantity;
    std::string memo;
    DK_FIELDS(from, to, quantity, memo)
};

struct User {
    DK_STRUCT("user")
    Name account;
    double balance = 0;
    DK_FIELDS(account, balance)
};

struct Returnvalue {
    DK_STRUCT("returnvalue")
    Name message;
    DK_FIELDS(message)
};

std::unique_ptr<APIClient> makeClient(const std::string& api,
                                      test::MockProvider** provider = nullptr) {
    auto mock = std::make_shared<test::MockProvider>(api);
    if (provider) *provider = mock.get();
    return std::make_unique<APIClient>(APIClientOptions{.provider = mock});
}

// makeMockTransaction from test/utils/mock-transfer.ts
Result<Transaction> makeMockTransaction(const v1::GetInfoResponse& info,
                                        const std::string& memo = "eosio-core is the best <3") {
    const auto header = info.getTransactionHeader(90);
    DK_TRY(action,
           Action::from(json{{"authorization", json::array({{{"actor", "corecorecore"},
                                                             {"permission", "active"}}})},
                             {"account", "eosio.token"},
                             {"name", "transfer"}},
                        ApiTransfer{.from = "corecorecore"_n,
                                    .to = "teamgreymass"_n,
                                    .quantity = "0.0042 EOS"_asset,
                                    .memo = memo}));
    json txJson = Serializer::objectify(header);
    txJson["actions"] = json::array({Serializer::objectify(action)});
    return Transaction::from(txJson);
}

Result<SignedTransaction> signMockTransaction(const Transaction& transaction,
                                              const v1::GetInfoResponse& info) {
    DK_TRY(privateKey, PrivateKey::from("5JW71y3njNNVf9fiGaufq8Up5XiGk68jZ5tYhKpy69yyU9cr7n9"));
    DK_TRY(signature, privateKey.signDigest(transaction.signingDigest(info.chain_id)));
    json signedJson = Serializer::objectify(transaction);
    signedJson["signatures"] = json::array({signature.toString()});
    return SignedTransaction::from(signedJson);
}

}  // namespace

TEST_SUITE("api-v1") {
    TEST_CASE("FetchProvider methods") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto defaultResponse = jungle4->provider->call({.path = "/v1/chain/get_info"});
        CHECK(defaultResponse.value().status == 200);
        const auto getResponse =
            jungle4->provider->call({.path = "/v1/chain/get_info", .method = "GET"});
        CHECK(getResponse.value().status == 200);
    }

    TEST_CASE("chain get_abi") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto response = jungle4->v1.chain.get_abi("eosio.token").value();
        CHECK(response["account_name"] == "eosio.token");
        REQUIRE(response.contains("abi"));
        CHECK(response["abi"]["version"] == "eosio::abi/1.2");
    }

    TEST_CASE("ABI to blob to ABI") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto response = jungle4->v1.chain.get_abi("eosio.token").value();
        const auto originalAbi = ABI::from(response["abi"]).value();
        const auto serializedABI = Serializer::encode(originalAbi).value();
        const Blob blob(serializedABI.array);
        const auto abiFromBlob = ABI::from(Blob::from(blob.toString()).value()).value();
        CHECK(originalAbi.equals(abiFromBlob));
        CHECK(abiFromBlob.tables[0].name == originalAbi.tables[0].name);
    }

    TEST_CASE("chain get_raw_abi") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto response = jungle4->v1.chain.get_raw_abi("eosio.token"_n).value();
        CHECK(response.account_name.toString() == "eosio.token");
        CHECK(response.code_hash.hexString() ==
              "33109b3dd5d354cab5a425c1d4c404c4db056717215f1a8b7ba036a6692811df");
        CHECK(response.abi_hash.hexString() ==
              "d84356074da34a976528321472d73ac919227b9b01d9de59d8ade6d96440455c");
        const auto abi = ABI::from(response.abi).value();
        CHECK(abi.version == "eosio::abi/1.2");
    }

    TEST_CASE("chain get_code") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto response = jungle4->v1.chain.get_code("eosio.token"_n).value();
        CHECK(response.account_name.toString() == "eosio.token");
    }

    TEST_CASE("chain get_account") {
        const auto jungle = makeClient("https://jungle4.greymass.com");
        const auto account = jungle->v1.chain.get_account("teamgreymass").value();
        CHECK(account.account_name.toString() == "teamgreymass");
    }

    TEST_CASE("chain get_account (voter info)") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto account = eos->v1.chain.get_account("teamgreymass").value();
        CHECK(account.account_name.toString() == "teamgreymass");
        REQUIRE(account.voter_info.has_value());
        CHECK(account.voter_info->last_vote_weight > 0);
    }

    TEST_CASE("chain get_account (system account)") {
        const auto jungle = makeClient("https://jungle4.greymass.com");
        const auto account = jungle->v1.chain.get_account("eosio").value();
        CHECK(account.account_name.toString() == "eosio");
    }

    TEST_CASE("chain get_account (fio)") {
        const auto fio = makeClient("https://fio.greymass.com");
        const auto account = fio->v1.chain.get_account("lhp1ytjibtea").value();
        CHECK(account.account_name.toString() == "lhp1ytjibtea");
    }

    TEST_CASE("chain get_account / getPermission") {
        const auto jungle = makeClient("https://jungle4.greymass.com");
        const auto account = jungle->v1.chain.get_account("teamgreymass").value();
        const auto permission = account.getPermission("active").value();
        CHECK(permission.perm_name.toString() == "active");
        const auto byName = account.getPermission("active"_n).value();
        CHECK(byName.perm_name.toString() == "active");
        CHECK_FALSE(account.getPermission("invalid").has_value());
    }

    TEST_CASE("chain get_account (linked actions)") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto account = jungle4->v1.chain.get_account("wharfkit1115").value();
        CHECK(account.account_name.toString() == "wharfkit1115");
        const auto permission = account.getPermission("test"_n).value();
        REQUIRE(permission.linked_actions.has_value());
        REQUIRE(permission.linked_actions->size() == 1);
        CHECK((*permission.linked_actions)[0].account.toString() == "eosio.token");
        REQUIRE((*permission.linked_actions)[0].action.has_value());
        CHECK((*permission.linked_actions)[0].action->toString() == "transfer");
    }

    TEST_CASE("chain get_accounts_by_authorizers (keys)") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto response =
            jungle4->v1.chain
                .get_accounts_by_authorizers(json{
                    {"keys", json::array({"PUB_K1_6RWZ1CmDL4B6LdixuertnzxcRuUDac3NQspJEvMnebGcXY4z"
                                          "Zj"})}})
                .value();
        REQUIRE(response.accounts.size() == 13);
        CHECK(response.accounts[0].account_name.toString() == "testtestasdf");
        CHECK(response.accounts[0].permission_name.toString() == "owner");
        REQUIRE(response.accounts[0].authorizing_key.has_value());
        CHECK(response.accounts[0].authorizing_key->toString() ==
              "PUB_K1_6RWZ1CmDL4B6LdixuertnzxcRuUDac3NQspJEvMnebGcXY4zZj");
        CHECK(response.accounts[0].weight.value == 1);
        CHECK(response.accounts[0].threshold == 1);
    }

    TEST_CASE("chain get_accounts_by_authorizers (accounts)") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto response =
            jungle4->v1.chain
                .get_accounts_by_authorizers(json{{"accounts", json::array({"eosio.prods"})}})
                .value();
        REQUIRE(response.accounts.size() == 1);
        CHECK(response.accounts[0].account_name.toString() == "eosio");
        CHECK(response.accounts[0].permission_name.toString() == "active");
        REQUIRE(response.accounts[0].authorizing_account.has_value());
        CHECK(response.accounts[0].authorizing_account->actor.toString() == "eosio.prods");
        CHECK(response.accounts[0].authorizing_account->permission.toString() == "active");
    }

    TEST_CASE("chain get_activated_protocol_features") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto response = jungle4->v1.chain.get_activated_protocol_features().value();
        REQUIRE(response.activated_protocol_features.size() == 10);
        REQUIRE(response.more.has_value());
        CHECK(*response.more == 10);
        const auto& feature = response.activated_protocol_features[0];
        CHECK(feature.feature_digest.hexString() ==
              "0ec7e080177b2c02b278d5088611686b49d739925a92d9bfcacd7fc6b74053bd");
        CHECK(feature.activation_ordinal == 0);
        CHECK(feature.activation_block_num == 4);
        CHECK(feature.description_digest.hexString() ==
              "64fe7df32e9b86be2b296b3f81dfd527f84e82b98e363bc97e40bc7a83733310");
        CHECK(feature.dependencies.empty());
        CHECK(feature.protocol_feature_type == "builtin");
    }

    TEST_CASE("chain get_block (by id)") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto block =
            eos->v1.chain
                .get_block("00816d41e41f1462acb648b810b20f152d944fabd79aaff31c9f50102e4e5db9")
                .value();
        CHECK(block.block_num == 8482113);
        CHECK(block.id.hexString() ==
              "00816d41e41f1462acb648b810b20f152d944fabd79aaff31c9f50102e4e5db9");
    }

    TEST_CASE("chain get_block (by num)") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto block = eos->v1.chain.get_block(8482113).value();
        CHECK(block.block_num == 8482113);
        CHECK(block.id.hexString() ==
              "00816d41e41f1462acb648b810b20f152d944fabd79aaff31c9f50102e4e5db9");
    }

    TEST_CASE("chain get_block w/ new_producers") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto block = eos->v1.chain.get_block(92565371).value();
        CHECK(block.block_num == 92565371);
    }

    TEST_CASE("chain get_block w/ transactions") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto block = eos->v1.chain.get_block(124472078).value();
        CHECK(block.block_num == 124472078);
        REQUIRE(block.transactions.size() == 8);
        const auto tx = block.transactions[5].trx.transaction().value();
        REQUIRE(tx.has_value());
        CHECK(tx->id().hexString() == block.transactions[5].id().hexString());
        const auto sigs = block.transactions[5].trx.signatures().value();
        REQUIRE(sigs.has_value());
        CHECK((*sigs)[0].toString() ==
              "SIG_K1_KeQEThQJEk7fuQC1zLuFyXZBnVmeRJXq9SrmDJGcerq1RZbgCoH5tvt28xpM7xA1bp7tStVPw17g"
              "NMG6hFyYXuNHCU4Wpd");
    }

    TEST_CASE("chain get_block w/ compression") {
        const auto wax = makeClient("https://wax.greymass.com");
        const auto block = wax->v1.chain.get_block(258546986).value();
        CHECK(block.block_num == 258546986);
        for (const auto& tx : block.transactions) {
            const auto decoded = tx.trx.transaction().value();
            CHECK(decoded.has_value());
        }
    }

    TEST_CASE("chain get_block_header_state") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto header = eos->v1.chain.get_block_header_state(323978187).value();
        CHECK(header.block_num == 323978187);
    }

    TEST_CASE("chain get_block_header_state (header extensions)") {
        const auto eos = makeClient("https://eos.greymass.com");
        CHECK(eos->v1.chain.get_block_header_state(400838396).value().block_num == 400838396);
        const auto wax = makeClient("https://wax.greymass.com");
        CHECK(wax->v1.chain.get_block_header_state(336356138).value().block_num == 336356138);
        const auto telos = makeClient("https://telos.greymass.com");
        CHECK(telos->v1.chain.get_block_header_state(369358506).value().block_num == 369358506);
    }

    TEST_CASE("chain get_currency_balance") {
        const auto jungle = makeClient("https://jungle4.greymass.com");
        const auto balances =
            jungle->v1.chain.get_currency_balance("eosio.token", "lioninjungle").value();
        REQUIRE(balances.size() == 2);
        CHECK(balances[0].toString() == "539235868.8986 EOS");
        CHECK(balances[1].toString() == "100360.0680 JUNGLE");
    }

    TEST_CASE("chain get_currency_balance w/ symbol") {
        const auto jungle = makeClient("https://jungle4.greymass.com");
        const auto balances =
            jungle->v1.chain.get_currency_balance("eosio.token", "lioninjungle", "JUNGLE").value();
        REQUIRE(balances.size() == 1);
        CHECK(balances[0].value() == 100360.068);
    }

    TEST_CASE("chain get_currency_stats") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto stats = jungle4->v1.chain.get_currency_stats("eosio.token", "EOS").value();
        REQUIRE(stats.contains("EOS"));
        CHECK(stats.at("EOS").issuer.toString() == "eosio");
    }

    TEST_CASE("chain get_info") {
        const auto jungle = makeClient("https://jungle4.greymass.com");
        const auto info = jungle->v1.chain.get_info().value();
        CHECK(info.chain_id.hexString() ==
              "73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d");
    }

    TEST_CASE("chain get_info (beos)") {
        const auto beos = makeClient("https://api.beos.world");
        const auto info = beos->v1.chain.get_info().value();
        CHECK(info.chain_id.hexString() ==
              "cbef47b0b26d2b8407ec6a6f91284100ec32d288a39d4b4bbd49655f7c484112");
    }

    TEST_CASE("chain get_producer_schedule") {
        const auto jungle = makeClient("https://jungle4.greymass.com");
        const auto schedule = jungle->v1.chain.get_producer_schedule().value();
        REQUIRE(schedule.active.has_value());
        CHECK(schedule.active->version == 72);
        REQUIRE(schedule.active->producers.size() == 21);
        CHECK(schedule.active->producers[0].producer_name.toString() == "alohaeostest");
        const auto authority = schedule.active->producers[0].producerAuthority().value();
        CHECK(authority.threshold == 1);
        REQUIRE(authority.keys.size() == 1);
        CHECK(authority.keys[0].weight.value == 1);
        CHECK(authority.keys[0].key.toString() ==
              "PUB_K1_8QwUpioje5txP4XwwXjjufqMs7wjrxkuWhUxcVMaxqrr14Sd2v");
    }

    TEST_CASE("chain push_transaction") {
        const auto jungle = makeClient("https://jungle4.greymass.com");
        const auto info = jungle->v1.chain.get_info().value();
        const auto header = info.getTransactionHeader();
        const auto action =
            Action::from(json{{"authorization", json::array({{{"actor", "corecorecore"},
                                                              {"permission", "active"}}})},
                              {"account", "eosio.token"},
                              {"name", "transfer"}},
                         ApiTransfer{.from = "corecorecore"_n,
                                     .to = "teamgreymass"_n,
                                     .quantity = "0.0042 EOS"_asset,
                                     .memo = "eosio-core is the best <3"})
                .value();
        json txJson = Serializer::objectify(header);
        txJson["actions"] = json::array({Serializer::objectify(action)});
        const auto transaction = Transaction::from(txJson).value();
        const auto privateKey =
            PrivateKey::from("5JW71y3njNNVf9fiGaufq8Up5XiGk68jZ5tYhKpy69yyU9cr7n9").value();
        const auto signature = privateKey.signDigest(transaction.signingDigest(info.chain_id)).value();
        json signedJson = Serializer::objectify(transaction);
        signedJson["signatures"] = json::array({signature.toString()});
        const auto signedTransaction = SignedTransaction::from(signedJson).value();
        const auto result = jungle->v1.chain.push_transaction(signedTransaction).value();
        CHECK(result["transaction_id"] == transaction.id().hexString());
    }

    TEST_CASE("packed transaction compression flags") {
        const auto jungle = makeClient("https://jungle4.greymass.com");
        const auto info = jungle->v1.chain.get_info().value();
        const auto transaction = makeMockTransaction(info).value();
        const auto signedTransaction = signMockTransaction(transaction, info).value();
        CHECK(PackedTransaction::fromSigned(signedTransaction).value().compression == 1);
        CHECK(PackedTransaction::fromSigned(signedTransaction, CompressionType::none)
                  .value()
                  .compression == 0);
    }

    TEST_CASE("chain compute_transaction") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto info = jungle4->v1.chain.get_info().value();
        const auto transaction = makeMockTransaction(info).value();
        const auto signedTransaction = signMockTransaction(transaction, info).value();
        const auto result = jungle4->v1.chain.compute_transaction(signedTransaction).value();
        CHECK(result["transaction_id"] == transaction.id().hexString());
    }

    TEST_CASE("chain send_transaction") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto info = jungle4->v1.chain.get_info().value();
        const auto transaction = makeMockTransaction(info).value();
        const auto signedTransaction = signMockTransaction(transaction, info).value();
        const auto result = jungle4->v1.chain.send_transaction(signedTransaction).value();
        CHECK(result["transaction_id"] == transaction.id().hexString());
    }

    TEST_CASE("chain send_transaction2 (default)") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto info = jungle4->v1.chain.get_info().value();
        const auto transaction =
            makeMockTransaction(info, "chain send_transaction2 (default)").value();
        const auto signedTransaction = signMockTransaction(transaction, info).value();
        const auto result = jungle4->v1.chain.send_transaction2(signedTransaction).value();
        CHECK(result["transaction_id"] == transaction.id().hexString());
    }

    TEST_CASE("chain send_transaction2 (failure traces)") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto info = jungle4->v1.chain.get_info().value();
        const auto transaction =
            makeMockTransaction(info, "chain send_transaction2 (failure traces)").value();
        const auto signedTransaction = signMockTransaction(transaction, info).value();
        const auto result = jungle4->v1.chain
                                .send_transaction2(signedTransaction,
                                                   {.return_failure_trace = true})
                                .value();
        CHECK(result["transaction_id"] == transaction.id().hexString());
    }

    TEST_CASE("chain send_transaction2 (retry)") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto info = jungle4->v1.chain.get_info().value();
        const auto transaction =
            makeMockTransaction(info, "chain send_transaction2 (retry)").value();
        const auto signedTransaction = signMockTransaction(transaction, info).value();
        const auto result =
            jungle4->v1.chain
                .send_transaction2(signedTransaction,
                                   {.retry_trx = true, .retry_trx_num_blocks = 10})
                .value();
        CHECK(result["transaction_id"] == transaction.id().hexString());
    }

    TEST_CASE("chain send_transaction2 (failure detection)") {
        test::MockProvider* provider = nullptr;
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io", &provider);
        provider->setContext("chain send_transaction2 (failure detection)");
        const auto info = jungle4->v1.chain.get_info().value();
        const auto header = info.getTransactionHeader(90);
        const auto action =
            Action::from(json{{"authorization", json::array({{{"actor", "corecorecore"},
                                                              {"permission", "active"}}})},
                              {"account", "eosio.token"},
                              {"name", "transfer"}},
                         ApiTransfer{.from = "corecorecore"_n,
                                     .to = "nonexistent1"_n,
                                     .quantity = "0.0001 EOS"_asset,
                                     .memo = "this should fail"})
                .value();
        json txJson = Serializer::objectify(header);
        txJson["actions"] = json::array({Serializer::objectify(action)});
        const auto transaction = Transaction::from(txJson).value();
        const auto signedTransaction = signMockTransaction(transaction, info).value();
        const auto result = jungle4->v1.chain.send_transaction2(signedTransaction);
        REQUIRE_FALSE(result.has_value());
        const Error& error = result.error();
        CHECK(error.kind == ErrorKind::Api);
        CHECK(apierror::name(error) == "eosio_assert_message_exception");
        CHECK(apierror::code(error) == 3050003);
        CHECK(apierror::details(error).size() > 0);
        CHECK(apierror::response(error)["json"]["processed"].contains("except"));
    }

    TEST_CASE("chain get_table_rows (untyped)") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto res = eos->v1.chain
                             .get_table_rows(json{{"code", "eosio.token"},
                                                  {"table", "stat"},
                                                  {"scope", "5459781"},
                                                  {"key_type", "i64"}})
                             .value();
        REQUIRE(res.rows.size() == 1);
        CHECK(res.rows[0]["max_supply"] == "10000000000.0000 EOS");
    }

    TEST_CASE("chain get_table_rows (typed)") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto res1 = eos->v1.chain
                              .get_table_rows<User>(json{{"code", "fuel.gm"},
                                                         {"table", "users"},
                                                         {"limit", 1}})
                              .value();
        REQUIRE(res1.rows.size() == 1);
        CHECK(res1.more == true);
        CHECK(res1.rows[0].account.toString() == "aaaa");
        const auto res2 = eos->v1.chain
                              .get_table_rows<User>(json{{"code", "fuel.gm"},
                                                         {"table", "users"},
                                                         {"limit", 2},
                                                         {"lower_bound", res1.next_key}})
                              .value();
        CHECK(res2.rows[0].account.toString() == "atomichub");
        CHECK(res2.next_key.get<std::string>() == "boidservices");
        CHECK(res2.rows[1].balance == doctest::Approx(0.02566).epsilon(0.000001));
    }

    TEST_CASE("chain get_table_rows (empty scope)") {
        const auto jungle = makeClient("https://jungle4.greymass.com");
        const auto res = jungle->v1.chain
                             .get_table_rows(json{{"code", "eosio"},
                                                  {"table", "powup.state"},
                                                  {"scope", ""}})
                             .value();
        CHECK(res.rows.size() == 1);
    }

    TEST_CASE("chain get_table_rows (ram payer)") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto res = eos->v1.chain
                             .get_table_rows(json{{"code", "eosio.token"},
                                                  {"table", "stat"},
                                                  {"scope", 5459781},
                                                  {"show_payer", true}})
                             .value();
        REQUIRE(res.rows.size() == 1);
        REQUIRE(res.ram_payers.has_value());
        CHECK((*res.ram_payers)[0].toString() == "eosio.token");
    }

    TEST_CASE("chain get_table_by_scope") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto res = eos->v1.chain
                             .get_table_by_scope(json{{"code", "eosio.token"},
                                                      {"table", "accounts"},
                                                      {"limit", 1}})
                             .value();
        REQUIRE(res.rows.size() == 1);
        const auto res2 = eos->v1.chain
                              .get_table_by_scope(json{{"code", "eosio.token"},
                                                       {"table", "accounts"},
                                                       {"lower_bound", res.more},
                                                       {"upper_bound", res.more},
                                                       {"limit", 1}})
                              .value();
        CHECK(res2.rows.size() >= 0);
    }

    TEST_CASE("chain send_read_only_transaction") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto info = jungle4->v1.chain.get_info().value();
        const auto header = info.getTransactionHeader(90);
        const auto action = Action::from(json{{"authorization", json::array()},
                                              {"account", "abcabcabc333"},
                                              {"name", "returnvalue"}},
                                         Returnvalue{.message = "hello"_n})
                                .value();
        json txJson = Serializer::objectify(header);
        txJson["actions"] = json::array({Serializer::objectify(action)});
        const auto transaction = Transaction::from(txJson).value();
        json signedJson = Serializer::objectify(transaction);
        const auto signedTransaction = SignedTransaction::from(signedJson).value();
        const auto res = jungle4->v1.chain.send_read_only_transaction(signedTransaction).value();
        REQUIRE(res["processed"]["action_traces"].size() == 1);
        CHECK(res["processed"]["action_traces"][0]["return_value_data"] ==
              "Validation has passed.");
        CHECK(res["processed"]["action_traces"][0]["return_value_hex_data"] ==
              "1656616c69646174696f6e20686173207061737365642e");
        const auto decoded =
            Serializer::decode<std::string>(
                Bytes::from(res["processed"]["action_traces"][0]["return_value_hex_data"]
                                .get<std::string>())
                    .value())
                .value();
        CHECK(decoded == "Validation has passed.");
    }

    TEST_CASE("api errors") {
        const auto jungle = makeClient("https://jungle4.greymass.com");
        const auto result = jungle->call(
            {.path = "/v1/chain/get_account", .params = json{{"account_name", "nani1"}}});
        REQUIRE_FALSE(result.has_value());
        const Error& error = result.error();
        CHECK(error.kind == ErrorKind::Api);
        CHECK(error.message == "Account not found at /v1/chain/get_account");
        CHECK(apierror::name(error) == "exception");
        CHECK(apierror::code(error) == 0);
        const json response = apierror::response(error);
        CHECK(response["headers"]["access-control-allow-origin"] == "*");
        CHECK(response["headers"]["date"] == "Fri, 04 Aug 2023 18:50:00 GMT");
        const json details = apierror::details(error);
        REQUIRE(details.size() == 1);
        CHECK(details[0]["file"] == "http_plugin.cpp");
        CHECK(details[0]["line_number"] == 954);
        CHECK(details[0]["method"] == "handle_exception");
    }

    TEST_CASE("history get_actions") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto res = eos->v1.history.get_actions("teamgreymass", 1, 1).value();
        CHECK(res.actions.size() == 1);
    }

    TEST_CASE("history get_transaction") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto id = Checksum256::from(
                            "03ef96a276a252b66595d91006ad0ff38ed999816f078bc5d87f88368a9354e7")
                            .value();
        const auto res = eos->v1.history.get_transaction(id).value();
        REQUIRE(res.traces.has_value());
        CHECK(res.traces->is_array());
        CHECK(res.id.hexString() ==
              "03ef96a276a252b66595d91006ad0ff38ed999816f078bc5d87f88368a9354e7");
        CHECK(res.block_num == 199068081);
    }

    TEST_CASE("history get_transaction (no traces)") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto id = Checksum256::from(
                            "03ef96a276a252b66595d91006ad0ff38ed999816f078bc5d87f88368a9354e7")
                            .value();
        const auto res = eos->v1.history.get_transaction(id, {.excludeTraces = true}).value();
        CHECK_FALSE(res.traces.has_value());
        CHECK(res.block_num == 199068081);
    }

    TEST_CASE("history get_key_accounts") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto key =
            PublicKey::from("PUB_K1_6gqJ7sdPgjHLFLtks9cRPs5qYHa9U3CwK4P2JasTLWKQBdT2GF").value();
        const auto res = eos->v1.history.get_key_accounts(key).value();
        CHECK(res.account_names.size() == 2);
    }

    TEST_CASE("history get_controlled_accounts") {
        const auto eos = makeClient("https://eos.greymass.com");
        const auto res = eos->v1.history.get_controlled_accounts("teamgreymass").value();
        CHECK(res.controlled_accounts.size() == 2);
    }

    TEST_CASE("chain get_transaction_status") {
        const auto jungle4 = makeClient("https://jungle4.api.eosnation.io");
        const auto id = Checksum256::from(
                            "153207ae7b30621421b968fa3c327db0d89f70975cf2bee7f8118c336094019a")
                            .value();
        const auto res = jungle4->v1.chain.get_transaction_status(id).value();
        CHECK(res.state == "UNKNOWN");
    }
}
