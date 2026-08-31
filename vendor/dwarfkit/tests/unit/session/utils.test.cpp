// Port of session test/tests/utils.ts
#include <doctest/doctest.h>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

// Ensure no data besides the actions has changed
void commonAsserts(const Transaction& original, const Transaction& modified,
                   size_t oldActions = 1, size_t newActions = 2) {
    REQUIRE(original.context_free_actions.size() == modified.context_free_actions.size());
    CHECK(original.delay_sec.value == modified.delay_sec.value);
    CHECK(original.expiration.value == modified.expiration.value);
    CHECK_FALSE(original.id() == modified.id());
    CHECK(original.max_cpu_usage_ms == modified.max_cpu_usage_ms);
    CHECK(original.max_net_usage_words.value == modified.max_net_usage_words.value);
    CHECK(original.ref_block_num == modified.ref_block_num);
    CHECK(original.ref_block_prefix == modified.ref_block_prefix);
    REQUIRE(original.transaction_extensions.size() == modified.transaction_extensions.size());
    CHECK(original.actions.size() == oldActions);
    CHECK(modified.actions.size() == newActions);
}

bool sameAction(const Action& a, const Action& b) {
    return a.account == b.account && a.name == b.name && a.data == b.data;
}

SigningRequest makeRequest(const SigningRequestCreateArguments& args) {
    return SigningRequest::create(args, {.zlib = true}).value();
}

}  // namespace

TEST_SUITE("session-utils") {
    TEST_CASE("chainDefinition returns name") {
        ChainDefinition definition;
        definition.id = Checksum256::from(std::string_view(mockChainId)).value();
        definition.url = "https://jungle4.greymass.com";
        CHECK(definition.name() == "Jungle 4 (Testnet)");
    }

    TEST_CASE("chainDefinition returns unknown") {
        ChainDefinition definition;
        definition.id =
            Checksum256::from(std::string_view(
                                  "3d2e128872f1e1f7dacbb3b624d21fe5875193619376c2e5e4843bbdd5deeae3"))
                .value();
        definition.url = "https://randochain.greymass.com";
        CHECK(definition.name() == "Unknown blockchain");
    }

    TEST_CASE("appendAction") {
        const Action newAction = makeMockAction("new action");
        SUBCASE("payload w/ action") {
            SigningRequestCreateArguments args;
            args.action = Serializer::objectify(makeMockAction("old action"));
            args.chainId = ChainId::from(mockChainDefinition().id);
            const auto request = makeRequest(args);
            const auto originalTransaction = request.getRawTransaction().value();
            const auto modifiedRequest = appendAction(request, newAction).value();
            const auto modifiedTransaction = modifiedRequest.getRawTransaction().value();
            commonAsserts(originalTransaction, modifiedTransaction);
            CHECK(sameAction(originalTransaction.actions[0], modifiedTransaction.actions[0]));
            CHECK(sameAction(newAction, modifiedTransaction.actions[1]));
        }
        SUBCASE("payload w/ actions") {
            const json action = Serializer::objectify(makeMockAction("old action"));
            SigningRequestCreateArguments args;
            args.actions = json::array({action, action});
            args.chainId = ChainId::from(mockChainDefinition().id);
            const auto request = makeRequest(args);
            const auto originalTransaction = request.getRawTransaction().value();
            const auto modifiedRequest = appendAction(request, newAction).value();
            const auto modifiedTransaction = modifiedRequest.getRawTransaction().value();
            commonAsserts(originalTransaction, modifiedTransaction, 2, 3);
            CHECK(sameAction(originalTransaction.actions[0], modifiedTransaction.actions[0]));
            CHECK(sameAction(originalTransaction.actions[1], modifiedTransaction.actions[1]));
            CHECK(sameAction(newAction, modifiedTransaction.actions[2]));
        }
        SUBCASE("payload w/ transaction") {
            const auto client = makeClient(DK_FIXTURE_DIR "/session/data");
            const auto info = client->v1.chain.get_info().value();
            SigningRequestCreateArguments args;
            args.transaction =
                Serializer::objectify(makeMockTransaction(info, "old action"));
            args.chainId = ChainId::from(mockChainDefinition().id);
            const auto request = makeRequest(args);
            const auto originalTransaction = request.getRawTransaction().value();
            const auto modifiedRequest = appendAction(request, newAction).value();
            const auto modifiedTransaction = modifiedRequest.getRawTransaction().value();
            commonAsserts(originalTransaction, modifiedTransaction);
            CHECK(sameAction(originalTransaction.actions[0], modifiedTransaction.actions[0]));
            CHECK(sameAction(newAction, modifiedTransaction.actions[1]));
        }
    }

    TEST_CASE("prependAction") {
        const Action newAction = makeMockAction("new action");
        SUBCASE("payload w/ action") {
            SigningRequestCreateArguments args;
            args.action = Serializer::objectify(makeMockAction("old action"));
            args.chainId = ChainId::from(mockChainDefinition().id);
            const auto request = makeRequest(args);
            const auto originalTransaction = request.getRawTransaction().value();
            const auto modifiedRequest = prependAction(request, newAction).value();
            const auto modifiedTransaction = modifiedRequest.getRawTransaction().value();
            commonAsserts(originalTransaction, modifiedTransaction);
            CHECK(sameAction(newAction, modifiedTransaction.actions[0]));
            CHECK(sameAction(originalTransaction.actions[0], modifiedTransaction.actions[1]));
        }
        SUBCASE("payload w/ actions") {
            const json action = Serializer::objectify(makeMockAction("old action"));
            SigningRequestCreateArguments args;
            args.actions = json::array({action, action});
            args.chainId = ChainId::from(mockChainDefinition().id);
            const auto request = makeRequest(args);
            const auto originalTransaction = request.getRawTransaction().value();
            const auto modifiedRequest = prependAction(request, newAction).value();
            const auto modifiedTransaction = modifiedRequest.getRawTransaction().value();
            commonAsserts(originalTransaction, modifiedTransaction, 2, 3);
            CHECK(sameAction(newAction, modifiedTransaction.actions[0]));
            CHECK(sameAction(originalTransaction.actions[0], modifiedTransaction.actions[1]));
            CHECK(sameAction(originalTransaction.actions[1], modifiedTransaction.actions[2]));
        }
        SUBCASE("payload w/ transaction") {
            const auto client = makeClient(DK_FIXTURE_DIR "/session/data");
            const auto info = client->v1.chain.get_info().value();
            SigningRequestCreateArguments args;
            args.transaction =
                Serializer::objectify(makeMockTransaction(info, "old action"));
            args.chainId = ChainId::from(mockChainDefinition().id);
            const auto request = makeRequest(args);
            const auto originalTransaction = request.getRawTransaction().value();
            const auto modifiedRequest = prependAction(request, newAction).value();
            const auto modifiedTransaction = modifiedRequest.getRawTransaction().value();
            commonAsserts(originalTransaction, modifiedTransaction);
            CHECK(sameAction(newAction, modifiedTransaction.actions[0]));
            CHECK(sameAction(originalTransaction.actions[0], modifiedTransaction.actions[1]));
        }
    }

    TEST_CASE("logo") {
        SUBCASE("returns light when stringified") {
            const auto logo = Logo::from(std::string_view("foo"));
            CHECK(logo.toString() == "foo");
        }
        SUBCASE("variants always available") {
            const auto logo = Logo::from(std::string_view("foo"));
            CHECK(logo.getVariant("dark") == "foo");
            CHECK(logo.getVariant("light") == "foo");
        }
        SUBCASE("variants are correctly returned") {
            const auto logo = Logo::from(json{{"dark", "foo"}, {"light", "bar"}}).value();
            CHECK(logo.toString() == "bar");
            CHECK(logo.getVariant("dark") == "foo");
            CHECK(logo.getVariant("light") == "bar");
        }
    }
}
