// Port of antelope test/authority-sort.ts, replaying the recorded fixtures.
#include <doctest/doctest.h>

#include <dwarfkit/antelope/api/client.hpp>

#include "../../util/mock_provider.hpp"

using namespace dwarfkit;
namespace v1 = dwarfkit::api::v1;

namespace {

struct UpdateAuth {
    DK_STRUCT("updateauth")
    Name account;
    Name permission;
    Name parent;
    Authority auth;
    DK_FIELDS(account, permission, parent, auth)
};

// Chosen so localeCompare order is the reverse of bytewise order: base58 Z < w.
constexpr const char* KEY_A = "PUB_K1_5ZexUstSEjwgcZWfziD6zC6xqvDzYkoMH4bf2MjuyJdnejdjWy";
constexpr const char* KEY_B = "PUB_K1_5wBCjBSLvTm44r6cFBvHVYDcq9syfS7oTh6X8YSPst6msRH7um";

// Passkeys for rpid jungle4.anchorwallet.io and jungle4-account.unicove.com.
constexpr const char* WA_A =
    "PUB_WA_9EgZ4NdyTxraccNd6SHSqfGNdqDGPC2Der6AvnzBJQtTHRgRxf8DZZjf2V4azLnC2YUnWgYDizuLTAWt7bHB";
constexpr const char* WA_B =
    "PUB_WA_2CSuysB2uR6ewLaqXNxzT2ugL3TqnKUTELtyDB9SiFxnRj3FF4H9KyPmRVBQuwSfejbDe2jGfdqnPta4v3VR3G"
    "1maz";

constexpr const char* ACCOUNT = "corecorecore";
constexpr const char* PERMISSION = "sortproof";
constexpr const char* PRIVATE_KEY = "5JW71y3njNNVf9fiGaufq8Up5XiGk68jZ5tYhKpy69yyU9cr7n9";

// Built by hand rather than via Authority::from, which would re-sort the keys.
Authority authorityWithKeys(const std::vector<std::string>& keys) {
    Authority auth;
    auth.threshold = 1;
    for (const auto& key : keys) {
        KeyWeight kw;
        kw.key = PublicKey::from(key).value();
        kw.weight = uint16_t(1);
        auth.keys.push_back(std::move(kw));
    }
    return auth;
}

Result<json> pushUpdateAuth(APIClient& jungle, test::MockProvider& provider, const Authority& auth,
                            const std::string& context) {
    provider.setContext(context);
    DK_TRY(info, jungle.v1.chain.get_info());
    DK_TRY(action, Action::from(json{{"authorization", json::array({{{"actor", ACCOUNT},
                                                                     {"permission", "active"}}})},
                                     {"account", "eosio"},
                                     {"name", "updateauth"}},
                                UpdateAuth{.account = Name::from(ACCOUNT),
                                           .permission = Name::from(PERMISSION),
                                           .parent = "active"_n,
                                           .auth = auth}));
    json txJson = Serializer::objectify(info.getTransactionHeader());
    txJson["actions"] = json::array({Serializer::objectify(action)});
    DK_TRY(transaction, Transaction::from(txJson));
    DK_TRY(privateKey, PrivateKey::from(PRIVATE_KEY));
    DK_TRY(signature, privateKey.signDigest(transaction.signingDigest(info.chain_id)));
    json signedJson = Serializer::objectify(transaction);
    signedJson["signatures"] = json::array({signature.toString()});
    DK_TRY(signedTx, SignedTransaction::from(signedJson));
    return jungle.v1.chain.push_transaction(signedTx);
}

}  // namespace

TEST_SUITE("authority-key-ordering") {
    TEST_CASE("nodeos rejects K1 keys in reverse bytewise order") {
        auto mock = std::make_shared<test::MockProvider>();
        APIClient jungle(APIClientOptions{.provider = mock});
        const auto result =
            pushUpdateAuth(jungle, *mock, authorityWithKeys({KEY_B, KEY_A}), "k1-reversed");
        REQUIRE_FALSE(result.has_value());
        CHECK(apierror::name(result.error()) == "action_validate_exception");
        CHECK(apierror::details(result.error())[0]["message"].get<std::string>().starts_with(
            "Invalid authority"));
    }

    TEST_CASE("Authority.from() orders K1 keys the way nodeos accepts") {
        auto mock = std::make_shared<test::MockProvider>();
        APIClient jungle(APIClientOptions{.provider = mock});
        const auto auth =
            Authority::from(json{{"threshold", 1},
                                 {"keys", json::array({{{"key", KEY_B}, {"weight", 1}},
                                                       {{"key", KEY_A}, {"weight", 1}}})}})
                .value();
        CHECK(auth.keys[0].key.toString() == KEY_A);
        const auto result = pushUpdateAuth(jungle, *mock, auth, "k1-sorted");
        REQUIRE(result.has_value());
        CHECK((*result)["transaction_id"].is_string());
    }

    TEST_CASE("nodeos rejects WA keys in reverse bytewise order") {
        auto mock = std::make_shared<test::MockProvider>();
        APIClient jungle(APIClientOptions{.provider = mock});
        const auto result =
            pushUpdateAuth(jungle, *mock, authorityWithKeys({WA_B, WA_A}), "wa-reversed");
        REQUIRE_FALSE(result.has_value());
        CHECK(apierror::name(result.error()) == "action_validate_exception");
        CHECK(apierror::details(result.error())[0]["message"].get<std::string>().starts_with(
            "Invalid authority"));
    }

    TEST_CASE("Authority.from() orders WA keys the way nodeos accepts") {
        auto mock = std::make_shared<test::MockProvider>();
        APIClient jungle(APIClientOptions{.provider = mock});
        const auto auth =
            Authority::from(json{{"threshold", 1},
                                 {"keys", json::array({{{"key", WA_B}, {"weight", 1}},
                                                       {{"key", WA_A}, {"weight", 1}}})}})
                .value();
        CHECK(auth.keys[0].key.toString() == WA_A);
        const auto result = pushUpdateAuth(jungle, *mock, auth, "wa-sorted");
        REQUIRE(result.has_value());
        CHECK((*result)["transaction_id"].is_string());
    }
}
