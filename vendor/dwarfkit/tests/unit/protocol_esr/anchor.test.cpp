// Port of protocol-esr test/tests/anchor.ts
#include <doctest/doctest.h>

#include <dwarfkit/protocol_esr.hpp>

using namespace dwarfkit;

namespace {

// test/utils/mock-config.ts
const char* mockChainIdHex = "73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d";
const char* mockChainId2Hex = "34593b65376aee3c9b06ea8a8595122b39333aaab4c76ad52587831fcc096590";
const char* mockPrivateKey = "5Jtoxgny5tT7NiNFp1MLogviuPJ9NniWjnU4wKzaX4t7pL4kJ8s";
const char* mockSignature1 =
    "SIG_K1_KA8Pk3FprCgnRJiwuagttm6Bg6zZWc6uuNMcy3dgMKPUeRHxFRPq7ePuRriU4uVq5FgHxF9yWBJKm1kVRE4VwY"
    "FxoZ2e7s";

ChainDefinition mockChainDefinition() {
    ChainDefinition rv;
    rv.id = Checksum256::from(std::string_view(mockChainIdHex)).value();
    rv.url = "https://jungle4.greymass.com";
    return rv;
}

EsrLoginContext mockLoginContext() {
    EsrLoginContext context;
    context.appName = "mock";
    context.chains = {mockChainDefinition()};
    return context;
}

json mockCallbackPayload() {
    return json{
        {"sig",
         "SIG_K1_K4nkCupUx3hDXSHq4rhGPpDMPPPjJyvmF3M6j7ppYUzkR3L93endwnxf3YhJSG4SSvxxU1ytD8hj39ku"
         "kTeYxjwy5H3XNJ"},
        {"tx", "b8e921a7b68d7309847e633d74963f25eb5a7d0b15b1aceb143723c234686a8d"},
        {"rbn", "0"},
        {"rid", "0"},
        {"ex", "2020-07-10T08:40:20"},
        {"req", "esr://AgABAwACE2h0dHBzOi8vZXhhbXBsZS5jb20A"},
        {"sa", "wharfkit1115"},
        {"sp", "test"},
        {"cid", mockChainIdHex}};
}

const char* mockBuoyUrl = "https://mock-buoy-url.com";

}  // namespace

TEST_SUITE("pesr-anchor") {
    TEST_CASE("createIdentityRequest returns request, callback, request key and private key") {
        const auto result = createIdentityRequest(mockLoginContext(), mockBuoyUrl).value();
        CHECK(result.callback.service == mockBuoyUrl);
        CHECK(result.callback.channel.size() == 36);
        const auto expectedKey = result.privateKey.toPublic().value();
        CHECK(result.requestKey.toString() == expectedKey.toString());
        CHECK(result.sameDeviceRequest.dataEquals(result.request));
    }

    TEST_CASE("createIdentityRequest returns a SigningRequest with the correct values") {
        const auto result = createIdentityRequest(mockLoginContext(), mockBuoyUrl).value();
        const SigningRequest& request = result.request;
        CHECK(request.getIdentityScope()->toString() == "mock");
        CHECK(request.getRawInfoKey("link").has_value());
        // no chain picked and one chain configured: the TS mock context yields
        // a null chainId, an any-chain request
        CHECK(request.isMultiChain());
        // the link info key decodes back to the BuoySession that was created
        const auto link = Serializer::decode<BuoySession>(request.getRawInfoKey("link")->array)
                              .value();
        CHECK(link.session_name.toString() == "mock");
        CHECK(link.request_key.toString() == result.requestKey.toString());
        REQUIRE(link.user_agent.hasValue());
        CHECK((*link.user_agent).starts_with("@wharfkit/protocol-esr"));
        CHECK(request.getInfoKey("scope") == "mock");
        // the callback points at the buoy channel and runs in the background
        const auto data = std::get<RequestDataV3>(request.data);
        CHECK(data.callback == std::string(mockBuoyUrl) + "/" + result.callback.channel);
        CHECK(data.flags.background());
    }

    TEST_CASE("setTransactionCallback sets the callback and returns the callback data") {
        auto resolved =
            ResolvedSigningRequest::fromPayload(mockCallbackPayload(), {.zlib = true}).value();
        const std::string buoyUrl = "https://example.com/buoy";
        const auto callback = setTransactionCallback(resolved.request, buoyUrl);
        const auto resolvedCallback =
            resolved.getCallback({Signature::from(mockSignature1).value()}).value();
        CHECK(callback.service == "https://example.com/buoy");
        CHECK(callback.channel.size() == 36);
        REQUIRE(resolvedCallback.has_value());
        CHECK(resolvedCallback->url.starts_with("https://example.com/buoy"));
    }

    TEST_CASE("sealMessage seals the given message with the given keys") {
        const auto privateKey = PrivateKey::from(mockPrivateKey).value();
        const auto publicKey = privateKey.toPublic().value();
        const auto result = sealMessage("hello world", privateKey, publicKey, 1234).value();
        CHECK(result.from.toString() == publicKey.toString());
        CHECK(result.nonce == 1234);
        // exact bytes from @wharfkit/sealed-messages 1.0.1 run under node
        // with this key and nonce (scratchpad/sealtest)
        CHECK(result.from.toString() ==
              "PUB_K1_6RMS3nvoN9StPzZizve6WdovaDkE5KkEcCDXW7LbepyAhzQE4R");
        CHECK(result.ciphertext.hexString() == "240c4e1c8ba7063466912f28b41caaed");
        CHECK(result.checksum == 4101051806u);
        CHECK(Serializer::encode(result).value().hexString() ==
              "0002c9c679952fe122a7a2982e104bb4ced99e165226acb76318f367c0dd992a0d55d2040000000"
              "0000010240c4e1c8ba7063466912f28b41caaed9e1571f4");
        // and unseals back to the message through the shared secret
        const auto unsealed =
            unsealMessage(result.ciphertext, privateKey, publicKey, result.nonce).value();
        CHECK(unsealed == "hello world");
    }

    TEST_CASE("verifyLoginCallbackResponse errors if there are no signatures") {
        const auto result = verifyLoginCallbackResponse(json::object(), mockLoginContext());
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message == "Invalid response, must have at least one signature");
    }

    TEST_CASE("verifyLoginCallbackResponse errors on the wrong chain id") {
        json payload = mockCallbackPayload();
        payload["cid"] = mockChainId2Hex;
        const auto result = verifyLoginCallbackResponse(payload, mockLoginContext());
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message == "Got response for wrong chain id");
    }

    TEST_CASE("verifyLoginCallbackResponse accepts the right chain id") {
        CHECK(verifyLoginCallbackResponse(mockCallbackPayload(), mockLoginContext()).has_value());
    }

    TEST_CASE("verifyLoginCallbackResponse requires cid for multi chain contexts") {
        EsrLoginContext context = mockLoginContext();
        ChainDefinition second;
        second.id = Checksum256::from(std::string_view(mockChainId2Hex)).value();
        second.url = "https://jungle4.greymass.com";
        context.chains.push_back(second);
        json payload = mockCallbackPayload();
        payload.erase("cid");
        const auto result = verifyLoginCallbackResponse(payload, context);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message ==
              "Multi chain response payload must specify resolved chain id (cid)");
    }
}
