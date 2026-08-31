// Port of session test/tests/context.ts (mock-data makeContext)
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

TEST_SUITE("session-context") {
    TEST_CASE("abiCache has default") {
        auto context = makeContext();
        CHECK(context.abiCache != nullptr);
    }

    TEST_CASE("getters") {
        auto context = makeContext();
        CHECK(context.accountName() == Name::from("wharfkit1125"));
        CHECK(context.permissionName() == Name::from("test"));
        CHECK(context.esrOptions().abiProvider == context.abiCache.get());
        CHECK(context.esrOptions().zlib == true);
    }

    TEST_CASE("resolve request") {
        auto context = makeContext();
        SigningRequestCreateArguments args;
        args.action = Serializer::objectify(makeMockAction());
        args.chainId = ChainId::from(mockChainDefinition().id);
        const auto request = SigningRequest::create(args, {.zlib = true}).value();
        const auto resolved = context.resolve(request).value();
        CHECK(resolved.chainId.hexString() == mockChainId);
        CHECK(resolved.signer == PermissionLevel::from("wharfkit1125@test").value());
        CHECK(!resolved.transaction.actions.empty());
    }
}
