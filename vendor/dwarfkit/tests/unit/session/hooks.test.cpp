// Port of session test/tests/plugins/hooks/beforeSign.ts
#include <doctest/doctest.h>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

TransactContext makeContext() {
    const auto client = makeClient(DK_FIXTURE_DIR "/session/data");
    const auto abiCache = std::make_shared<ABICache>(client);
    const auto session =
        std::make_shared<Session>(mockSessionArgs(), mockSessionOptions());
    TransactContextOptions options;
    options.abiCache = abiCache;
    options.chain = mockChainDefinition();
    options.client = client;
    options.createRequest = [session, abiCache](const TransactArgs& args) {
        return session->createRequest(args, abiCache);
    };
    options.fetch = makeMockFetch();
    options.permissionLevel = PermissionLevel::from("wharfkit1125@test").value();
    return TransactContext(options);
}

}  // namespace

TEST_SUITE("session-hooks") {
    TEST_CASE("beforeSign: prepend action on action") {
        auto context = makeContext();
        SigningRequestCreateArguments args;
        args.action = Serializer::objectify(makeMockAction());
        const auto request = SigningRequest::create(args, {.zlib = true}).value();
        const auto response =
            mockTransactResourceProviderPresignHook(request, context).value();
        REQUIRE(response.has_value());
        const auto actions = response->request.getRawActions().value();
        REQUIRE(actions.size() == 2);
        CHECK(actions[0].account == Name::from("greymassnoop"));
        CHECK(actions[0].authorization[0].actor == Name::from("greymassfuel"));
        CHECK(actions[1].account == Name::from("eosio.token"));
        CHECK(actions[1].authorization[0].actor == Name::from(mockAccountName));
    }

    TEST_CASE("beforeSign: prepend action on actions") {
        auto context = makeContext();
        const json action = Serializer::objectify(makeMockAction());
        SigningRequestCreateArguments args;
        args.actions = json::array({action, action});
        args.chainId = ChainId::from(mockChainDefinition().id);
        const auto request = SigningRequest::create(args, {.zlib = true}).value();
        const auto response =
            mockTransactResourceProviderPresignHook(request, context).value();
        REQUIRE(response.has_value());
        const auto actions = response->request.getRawActions().value();
        REQUIRE(actions.size() == 3);
        CHECK(actions[0].account == Name::from("greymassnoop"));
        CHECK(actions[0].authorization[0].actor == Name::from("greymassfuel"));
        CHECK(actions[1].account == Name::from("eosio.token"));
        CHECK(actions[1].authorization[0].actor == Name::from(mockAccountName));
    }

    TEST_CASE("beforeSign: prepend action on transaction") {
        auto context = makeContext();
        const auto client = makeClient(DK_FIXTURE_DIR "/session/data");
        const auto info = client->v1.chain.get_info().value();
        SigningRequestCreateArguments args;
        args.transaction = Serializer::objectify(makeMockTransaction(info));
        args.chainId = ChainId::from(mockChainDefinition().id);
        const auto request = SigningRequest::create(args, {.zlib = true}).value();
        const auto response =
            mockTransactResourceProviderPresignHook(request, context).value();
        REQUIRE(response.has_value());
        const auto actions = response->request.getRawActions().value();
        REQUIRE(actions.size() == 2);
        CHECK(actions[0].account == Name::from("greymassnoop"));
        CHECK(actions[0].authorization[0].actor == Name::from("greymassfuel"));
        CHECK(actions[1].account == Name::from("eosio.token"));
        CHECK(actions[1].authorization[0].actor == Name::from(mockAccountName));
    }
}
