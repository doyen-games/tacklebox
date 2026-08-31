// Port of signing-request test/request.ts
#include <doctest/doctest.h>

#include <fstream>
#include <sstream>

#include <dwarfkit/signing_request.hpp>

using namespace dwarfkit;

namespace {

json parse(std::string_view text) { return json::parse(text); }

bool deepEqual(const json& a, const json& b) { return nlohmann::json(a) == nlohmann::json(b); }

class MockAbiProvider final : public AbiProvider {
public:
    Result<ABI> getAbi(const Name& account) override {
        std::ifstream file(std::string(DK_FIXTURE_DIR "/signing_request/abis/") +
                           account.toString() + ".json");
        if (!file.good()) {
            return err(ErrorKind::NotFound, "No ABI for: " + account.toString());
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return ABI::from(json::parse(buffer.str()));
    }
};

MockAbiProvider& abiProvider() {
    static MockAbiProvider provider;
    return provider;
}

SigningRequestEncodingOptions options() {
    return {.zlib = true, .abiProvider = &abiProvider()};
}

Result<AbiMap> testAbis() {
    AbiMap abis;
    auto abi = abiProvider().getAbi(Name::from("eosio.token"));
    if (!abi) return err(std::move(abi.error()));
    abis.emplace("eosio.token", std::move(*abi));
    return abis;
}

json requestDataJson(const SigningRequest& request) {
    return std::visit([](const auto& d) { return Serializer::objectify(d); }, request.data);
}

const char* timestamp = "2018-02-15T00:00:00";

}  // namespace

TEST_SUITE("signing-request") {
    TEST_CASE("should create from action") {
        const auto request =
            SigningRequest::create(
                {.action = parse(R"({
                    "account": "eosio.token",
                    "name": "transfer",
                    "authorization": [{"actor": "foo", "permission": "active"}],
                    "data": {"from": "foo", "to": "bar", "quantity": "1.000 EOS", "memo": "hello there"}
                })")},
                options())
                .value();
        CHECK(deepEqual(requestDataJson(request), parse(R"({
            "chain_id": ["chain_alias", 1],
            "req": ["action", {
                "account": "eosio.token",
                "name": "transfer",
                "authorization": [{"actor": "foo", "permission": "active"}],
                "data": "000000000000285d000000000000ae39e80300000000000003454f53000000000b68656c6c6f207468657265"
            }],
            "callback": "",
            "flags": 1,
            "info": []
        })")));
    }

    TEST_CASE("should create from actions") {
        const auto request =
            SigningRequest::create(
                {.actions = parse(R"([
                    {"account": "eosio.token", "name": "transfer",
                     "authorization": [{"actor": "foo", "permission": "active"}],
                     "data": {"from": "foo", "to": "bar", "quantity": "1.000 EOS", "memo": "hello there"}},
                    {"account": "eosio.token", "name": "transfer",
                     "authorization": [{"actor": "baz", "permission": "active"}],
                     "data": {"from": "baz", "to": "bar", "quantity": "1.000 EOS", "memo": "hello there"}}
                ])"),
                 .callback = CallbackType{"https://example.com/?tx={{tx}}", true}},
                options())
                .value();
        CHECK(deepEqual(requestDataJson(request), parse(R"({
            "chain_id": ["chain_alias", 1],
            "req": ["action[]", [
                {"account": "eosio.token", "name": "transfer",
                 "authorization": [{"actor": "foo", "permission": "active"}],
                 "data": "000000000000285d000000000000ae39e80300000000000003454f53000000000b68656c6c6f207468657265"},
                {"account": "eosio.token", "name": "transfer",
                 "authorization": [{"actor": "baz", "permission": "active"}],
                 "data": "000000000000be39000000000000ae39e80300000000000003454f53000000000b68656c6c6f207468657265"}
            ]],
            "callback": "https://example.com/?tx={{tx}}",
            "flags": 3,
            "info": []
        })")));
    }

    TEST_CASE("should create from transaction") {
        const auto request =
            SigningRequest::create(
                {.transaction = parse(R"({
                    "delay_sec": 123,
                    "expiration": "2018-02-15T00:00:00",
                    "max_cpu_usage_ms": 99,
                    "actions": [{
                        "account": "eosio.token", "name": "transfer",
                        "authorization": [{"actor": "foo", "permission": "active"}],
                        "data": "000000000000285D000000000000AE39E80300000000000003454F53000000000B68656C6C6F207468657265"
                    }]
                })"),
                 .broadcast = false,
                 .callback = CallbackType{"https://example.com/?tx={{tx}}", false}},
                options())
                .value();
        CHECK(deepEqual(requestDataJson(request), parse(R"({
            "chain_id": ["chain_alias", 1],
            "req": ["transaction", {
                "actions": [{
                    "account": "eosio.token", "name": "transfer",
                    "authorization": [{"actor": "foo", "permission": "active"}],
                    "data": "000000000000285d000000000000ae39e80300000000000003454f53000000000b68656c6c6f207468657265"
                }],
                "context_free_actions": [],
                "delay_sec": 123,
                "expiration": "2018-02-15T00:00:00",
                "max_cpu_usage_ms": 99,
                "max_net_usage_words": 0,
                "ref_block_num": 0,
                "ref_block_prefix": 0,
                "transaction_extensions": []
            }],
            "callback": "https://example.com/?tx={{tx}}",
            "flags": 0,
            "info": []
        })")));
    }

    TEST_CASE("should create from uri") {
        const auto request =
            SigningRequest::from(
                "esr://gmNgZGBY1mTC_MoglIGBIVzX5uxZRqAQGMBoExgDAjRi4fwAVz93ICUckpGYl12skJZfpFCSka"
                "qQllmcwczAAAA",
                options())
                .value();
        CHECK(deepEqual(requestDataJson(request), parse(R"({
            "chain_id": ["chain_alias", 1],
            "req": ["action", {
                "account": "eosio.token",
                "name": "transfer",
                "authorization": [{"actor": "............1", "permission": "............1"}],
                "data": "0100000000000000000000000000285d01000000000000000050454e47000000135468616e6b7320666f72207468652066697368"
            }],
            "callback": "",
            "flags": 3,
            "info": []
        })")));
    }

    TEST_CASE("should resolve to transaction") {
        const auto request =
            SigningRequest::create(
                {.action = parse(R"({
                    "account": "eosio.token", "name": "transfer",
                    "authorization": [{"actor": "foo", "permission": "active"}],
                    "data": {"from": "foo", "to": "bar", "quantity": "1.000 EOS", "memo": "hello there"}
                })")},
                options())
                .value();
        const auto abis = request.fetchAbis().value();
        const auto tx =
            request
                .resolveTransaction(abis, PermissionLevel{"foo"_n, "bar"_n},
                                    {.timestamp = TimePointSec::from(timestamp).value(),
                                     .expire_seconds = 0,
                                     .block_num = 1234,
                                     .ref_block_prefix = 56789})
                .value();
        CHECK(deepEqual(Serializer::objectify(tx), parse(R"({
            "actions": [{
                "account": "eosio.token", "name": "transfer",
                "authorization": [{"actor": "foo", "permission": "active"}],
                "data": {"from": "foo", "to": "bar", "quantity": "1.000 EOS", "memo": "hello there"}
            }],
            "context_free_actions": [],
            "transaction_extensions": [],
            "expiration": "2018-02-15T00:00:00",
            "ref_block_num": 1234,
            "ref_block_prefix": 56789,
            "max_cpu_usage_ms": 0,
            "max_net_usage_words": 0,
            "delay_sec": 0
        })")));
    }

    TEST_CASE("should resolve to transaction with a higher height") {
        const auto request =
            SigningRequest::create(
                {.action = parse(R"({
                    "account": "eosio.token", "name": "transfer",
                    "authorization": [{"actor": "foo", "permission": "active"}],
                    "data": {"from": "foo", "to": "bar", "quantity": "1.000 EOS", "memo": "hello there"}
                })")},
                options())
                .value();
        const auto abis = request.fetchAbis().value();
        const auto tx =
            request
                .resolveTransaction(
                    abis, PermissionLevel{"foo"_n, "bar"_n},
                    {.timestamp = TimePointSec::from("2022-11-01T00:19:33.500").value(),
                     .expire_seconds = 60,
                     .block_num = 211598529,
                     .ref_block_prefix = 3524347598u})
                .value();
        CHECK(deepEqual(Serializer::objectify(tx), parse(R"({
            "actions": [{
                "account": "eosio.token", "name": "transfer",
                "authorization": [{"actor": "foo", "permission": "active"}],
                "data": {"from": "foo", "to": "bar", "quantity": "1.000 EOS", "memo": "hello there"}
            }],
            "context_free_actions": [],
            "transaction_extensions": [],
            "expiration": "2022-11-01T00:20:34",
            "ref_block_num": 48321,
            "ref_block_prefix": 3524347598,
            "max_cpu_usage_ms": 0,
            "max_net_usage_words": 0,
            "delay_sec": 0
        })")));
    }

    TEST_CASE("should resolve with placeholder name") {
        const auto request =
            SigningRequest::create(
                {.action = parse(R"({
                    "account": "eosio.token", "name": "transfer",
                    "authorization": [{"actor": "............1", "permission": "............2"}],
                    "data": {"from": "............1", "to": "............2", "quantity": "1.000 EOS", "memo": "hello there"}
                })")},
                options())
                .value();
        const auto abis = request.fetchAbis().value();
        const auto tx =
            request
                .resolveTransaction(abis, PermissionLevel{"foo"_n, Name::from("mractive")},
                                    {.timestamp = TimePointSec::from(timestamp).value(),
                                     .expire_seconds = 0,
                                     .block_num = 1234,
                                     .ref_block_prefix = 56789})
                .value();
        CHECK(deepEqual(Serializer::objectify(tx), parse(R"({
            "actions": [{
                "account": "eosio.token", "name": "transfer",
                "authorization": [{"actor": "foo", "permission": "mractive"}],
                "data": {"from": "foo", "to": "mractive", "quantity": "1.000 EOS", "memo": "hello there"}
            }],
            "context_free_actions": [],
            "transaction_extensions": [],
            "expiration": "2018-02-15T00:00:00",
            "ref_block_num": 1234,
            "ref_block_prefix": 56789,
            "max_cpu_usage_ms": 0,
            "max_net_usage_words": 0,
            "delay_sec": 0
        })")));
    }

    TEST_CASE("should encode and decode requests") {
        const auto req1 =
            SigningRequest::create(
                {.action = parse(R"({
                    "account": "eosio.token", "name": "transfer",
                    "authorization": [{"actor": "............1", "permission": "............1"}],
                    "data": {"from": "............1", "to": "foo", "quantity": "1. PENG", "memo": "Thanks for the fish"}
                })"),
                 .callback = CallbackType{"", true}},
                options())
                .value();
        const auto encoded = req1.encode();
        CHECK(encoded ==
              "esr://gmNgZGBY1mTC_MoglIGBIVzX5uxZRqAQGMBoExgDAjRi4fwAVz93ICUckpGYl12skJZfpFCSkaqQ"
              "llmcwczAAAA");
        const auto req2 = SigningRequest::from(encoded, options()).value();
        CHECK(deepEqual(requestDataJson(req2), requestDataJson(req1)));
    }

    TEST_CASE("should create identity tx") {
        const auto req = SigningRequest::identity(
                             {.callback = CallbackType{"https://example.com", true}}, options())
                             .value();
        const auto tx =
            req.resolveTransaction(testAbis().value(), PermissionLevel{"foo"_n, "bar"_n}).value();
        CHECK(deepEqual(Serializer::objectify(tx), parse(R"({
            "actions": [{
                "account": "", "name": "identity",
                "authorization": [{"actor": "foo", "permission": "bar"}],
                "data": {"permission": {"actor": "foo", "permission": "bar"}}
            }],
            "context_free_actions": [],
            "transaction_extensions": [],
            "expiration": "1970-01-01T00:00:00",
            "ref_block_num": 0,
            "ref_block_prefix": 0,
            "max_cpu_usage_ms": 0,
            "max_net_usage_words": 0,
            "delay_sec": 0
        })")));
        const auto tx2 =
            req.resolveTransaction(testAbis().value(),
                                   PermissionLevel{Name::from("other"), "active"_n})
                .value();
        CHECK_FALSE(deepEqual(Serializer::objectify(tx2.actions[0]).at("data"),
                              Serializer::objectify(tx.actions[0]).at("data")));
    }

    TEST_CASE("should encode and decode signed requests") {
        struct MockSigner final : SignatureProvider {
            Result<RequestSignature> sign(const Checksum256&) override {
                RequestSignature sig;
                sig.signer = "foo"_n;
                sig.signature = Signature::from(
                                    "SIG_K1_K8Wm5AXSQdKYVyYFPCYbMZurcJQXZaSgXoqXAKE6uxR6Jot7otVzS"
                                    "55JGRhixCwNGxaGezrVckDgh88xTsiu4wzzZuP9JE")
                                    .value();
                return sig;
            }
        };
        MockSigner signer;
        auto opts = options();
        opts.signatureProvider = &signer;
        const auto req1 = SigningRequest::create(
                              {.action = parse(R"({
                                  "account": "eosio.token", "name": "transfer",
                                  "authorization": [{"actor": "foo", "permission": "active"}],
                                  "data": {"from": "foo", "to": "bar", "quantity": "1.000 EOS", "memo": "hello there"}
                              })")},
                              opts)
                              .value();
        REQUIRE(req1.signature.has_value());
        CHECK(req1.signature->signer.toString() == "foo");
        const auto encoded = req1.encode();
        CHECK(encoded ==
              "esr://gmNgZGBY1mTC_MoglIGBIVzX5uxZoAgIaMSCyBVvjYx0kAUYGNZZvmCGsJhd_YNBNHdGak5OvkJJ"
              "RmpRKlQ3WLl8anjWFNWd23XWfvzTcy_qmtRx5mtMXlkSC23ZXle6K_NJFJ4SVTb4O026Wb1G5Wx0u1A3-_"
              "G4rAPsBp78z9lN7nddAQA");
        const auto req2 = SigningRequest::from(encoded, options()).value();
        CHECK(deepEqual(requestDataJson(req2), requestDataJson(req1)));
        REQUIRE(req2.signature.has_value());
        CHECK(req2.signature->signer.toString() == "foo");
        CHECK(req2.signature->signature.toString() ==
              "SIG_K1_K8Wm5AXSQdKYVyYFPCYbMZurcJQXZaSgXoqXAKE6uxR6Jot7otVzS55JGRhixCwNGxaGezrVck"
              "Dgh88xTsiu4wzzZuP9JE");
    }

    TEST_CASE("should encode and decode test requests") {
        const std::string req1uri =
            "esr://gmNgZGBY1mTC_MoglIGBIVzX5uxZRqAQGMBoExgDAjRi4fwAVz93ICUckpGYl12skJZfpFCSkaqQ"
            "llmcwczAAAA";
        const std::string req2uri =
            "esr://gmNgZGBY1mTC_MoglIGBIVzX5uxZRqAQGMBoExgDAjRi4fwAVz93ICUckpGYl12skJZfpFCSkaqQ"
            "llmcwQxREVOsEcsgX-9-jqsy1EhNQM_GM_FkQMIziUU1VU4PsmOn_3r5hUMumeN3PXvdSuWMm1o9u6-FmC"
            "wtPvR0haqt12fNKtlWzTuiNwA";
        const auto req1 = SigningRequest::from(req1uri, options()).value();
        const auto req2 = SigningRequest::from(req2uri, options()).value();
        const auto abis = testAbis().value();
        const auto resolved1 = req1.resolveActions(abis).value();
        const auto resolved2 = req2.resolveActions(abis).value();
        CHECK(deepEqual(Serializer::objectify(resolved1), Serializer::objectify(resolved2)));
        CHECK_FALSE(req1.signature.has_value());
        REQUIRE(req2.signature.has_value());
        CHECK(req2.signature->signer.toString() == "foobar");
        CHECK(req2.signature->signature.toString() ==
              "SIG_K1_KBub1qmdiPpWA2XKKEZEG3EfKJBf38GETHzbd4t3CBdWLgdvFRLCqbcUsBbbYga6jmxfdSFfod"
              "MdhMYraKLhEzjSCsiuMs");
        CHECK(req1.encode() == req1uri);
        CHECK(req2.encode() == req2uri);
    }

    TEST_CASE("should generate correct identity requests") {
        const std::string reqUri =
            "esr://AgABAwACJWh0dHBzOi8vY2guYW5jaG9yLmxpbmsvMTIzNC00NTY3LTg5MDAA";
        const auto req = SigningRequest::from(reqUri, options()).value();
        CHECK(req.isIdentity() == true);
        CHECK_FALSE(req.getIdentity().has_value());
        CHECK_FALSE(req.getIdentityPermission().has_value());
        CHECK(req.encode() == reqUri);
        const auto resolved = req.resolve({}, PermissionLevel{"foo"_n, "bar"_n}).value();
        CHECK(deepEqual(Serializer::objectify(resolved.resolvedTransaction), parse(R"({
            "actions": [{
                "account": "", "name": "identity",
                "authorization": [{"actor": "foo", "permission": "bar"}],
                "data": {"permission": {"actor": "foo", "permission": "bar"}}
            }],
            "context_free_actions": [],
            "delay_sec": 0,
            "expiration": "1970-01-01T00:00:00",
            "max_cpu_usage_ms": 0,
            "max_net_usage_words": 0,
            "ref_block_num": 0,
            "ref_block_prefix": 0,
            "transaction_extensions": []
        })")));
    }

    TEST_CASE("should encode and decode with metadata") {
        SigningRequestCreateIdentityArguments args;
        args.callback = CallbackType{"https://example.com", false};
        args.info.push_back({"foo", Bytes(std::vector<uint8_t>{'b', 'a', 'r'})});
        args.info.push_back({"baz", Serializer::encode(std::string("hello")).value()});
        auto req = SigningRequest::identity(args, options()).value();
        const auto sig = Signature::from(
                             "SIG_K1_K4nkCupUx3hDXSHq4rhGPpDMPPPjJyvmF3M6j7ppYUzkR3L93endwnxf3Y"
                             "hJSG4SSvxxU1ytD8hj39kukTeYxjwy5H3XNJ")
                             .value();
        REQUIRE(req.setInfoKey("extra_sig", sig).has_value());
        const auto decoded = SigningRequest::from(req.encode(), options()).value();
        CHECK(decoded.getRawInfoKey("foo")->hexString() == req.getRawInfoKey("foo")->hexString());
        CHECK(decoded.getInfoKey("foo") == "bar");
        CHECK(decoded.getInfoKey("baz", "string").value() == "hello");
        CHECK(decoded.getInfoKey<Signature>("extra_sig").value().toString() ==
              "SIG_K1_K4nkCupUx3hDXSHq4rhGPpDMPPPjJyvmF3M6j7ppYUzkR3L93endwnxf3YhJSG4SSvxxU1ytD8"
              "hj39kukTeYxjwy5H3XNJ");
    }

    TEST_CASE("should template callback url") {
        const std::string mockSig =
            "SIG_K1_K8Wm5AXSQdKYVyYFPCYbMZurcJQXZaSgXoqXAKE6uxR6Jot7otVzS55JGRhixCwNGxaGezrVckD"
            "gh88xTsiu4wzzZuP9JE";
        const std::string mockTx =
            "308d206c51c5dd6c02e0417e44560cdc2e76db7765cea19dfa8f9f94922f928a";
        const auto request =
            SigningRequest::create(
                {.action = parse(R"({
                    "account": "eosio.token", "name": "transfer",
                    "authorization": [{"actor": "foo", "permission": "active"}],
                    "data": {"from": "foo", "to": "bar", "quantity": "1.000 EOS", "memo": "hello there"}
                })"),
                 .callback = CallbackType{"https://example.com/?sig={{sig}}&tx={{tx}}", false}},
                options())
                .value();
        const auto abis = request.fetchAbis().value();
        const auto resolved =
            request
                .resolve(abis, PermissionLevel{"foo"_n, "bar"_n},
                         {.timestamp = TimePointSec::from(timestamp).value(),
                          .expire_seconds = 0,
                          .block_num = 1234,
                          .ref_block_prefix = 56789})
                .value();
        const auto callback =
            resolved.getCallback({Signature::from(mockSig).value()}).value();
        REQUIRE(callback.has_value());
        CHECK(callback->url == "https://example.com/?sig=" + mockSig + "&tx=" + mockTx);
    }

    TEST_CASE("should deep clone") {
        const auto request =
            SigningRequest::create(
                {.action = parse(R"({
                    "account": "eosio.token", "name": "transfer",
                    "authorization": [{"actor": "foo", "permission": "active"}],
                    "data": {"from": "foo", "to": "bar", "quantity": "1.000 EOS", "memo": ""}
                })")},
                options())
                .value();
        auto copy = request.clone();
        CHECK(deepEqual(requestDataJson(request), requestDataJson(copy)));
        CHECK(request.encode() == copy.encode());
        copy.setInfoKey("foo", true);
        CHECK_FALSE(deepEqual(requestDataJson(request), requestDataJson(copy)));
        CHECK(request.encode() != copy.encode());
    }

    TEST_CASE("should resolve templated callback urls") {
        const std::string req1uri =
            "esr://gmNgZGBY1mTC_MoglIGBIVzX5uxZRqAQGDBBaUWYAARoxMIkGAJDIyAM9YySkoJiK3391IrE3IKc"
            "VL3k_Fz7kgrb6uqSitpataQ8ICspr7aWAQA";
        const auto req1 = SigningRequest::from(req1uri, options()).value();
        const auto abis = req1.fetchAbis().value();
        const auto resolved =
            req1.resolve(abis, PermissionLevel{"foo"_n, "bar"_n},
                         {.timestamp = TimePointSec::from(timestamp).value(),
                          .expire_seconds = 0,
                          .block_num = 1234,
                          .ref_block_prefix = 56789})
                .value();
        const auto callback =
            resolved
                .getCallback({Signature::from(
                                  "SIG_K1_KBub1qmdiPpWA2XKKEZEG3EfKJBf38GETHzbd4t3CBdWLgdvFRLCqb"
                                  "cUsBbbYga6jmxfdSFfodMdhMYraKLhEzjSCsiuMs")
                                  .value()},
                             1234)
                .value();
        REQUIRE(callback.has_value());
        CHECK(callback->url ==
              "https://example.com?tx=6aff5c203810ff6b40469fe20318856354889ff037f4cf5b89a157514a"
              "43e825&bn=1234");
    }

    TEST_CASE("should handle scoped id requests") {
        const Name scope = Name::from(18446744073709551615ull);
        const auto req =
            SigningRequest::create({.identity = json{{"scope", scope.toString()}},
                                    .callback = CallbackType{"https://example.com", true}},
                                   options())
                .value();
        CHECK(req.encode() == "esr://g2NgZP4PBQxMwhklJQXFVvr6qRWJuQU5qXrJ-bkMAA");
        const auto decoded =
            SigningRequest::from("esr://g2NgZP4PBQxMwhklJQXFVvr6qRWJuQU5qXrJ-bkMAA", options())
                .value();
        CHECK(decoded.dataEquals(req));
        CHECK(decoded.getIdentityScope()->toString() == scope.toString());
        const auto resolved =
            req.resolve({}, PermissionLevel{"foo"_n, "active"_n},
                        {.expiration = TimePointSec::from("2020-07-10T08:40:20").value()})
                .value();
        CHECK(resolved.transaction.expiration.toString() == "2020-07-10T08:40:20");
        CHECK(resolved.transaction.actions[0].data.hexString() ==
              "ffffffffffffffff01000000000000285d00000000a8ed3232");
        CHECK(resolved.signingDigest().hexString() ==
              "70d1fd5bda1998135ed44cbf26bd1cc2ed976219b2b6913ac13f41d4dd013307");
    }

    TEST_CASE("should handle multi-chain id requests") {
        SigningRequestCreateIdentityArguments args;
        args.anyChain = true;
        args.chainIds = {ChainId::from(ChainName::EOS).value(),
                         ChainId::from(ChainName::WAX).value()};
        args.scope = "foo"_n;
        args.callback = CallbackType{"myapp://login={{cid}}", false};
        const auto req = SigningRequest::identity(args, {.zlib = false}).value();
        CHECK(req.isMultiChain() == true);
        const auto ids = req.getChainIds().value();
        REQUIRE(ids.has_value());
        REQUIRE(ids->size() == 2);
        CHECK((*ids)[0].hexString() ==
              "aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906");
        CHECK((*ids)[1].hexString() ==
              "1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4");
        const auto resolved =
            req.resolve({}, PermissionLevel{"foo"_n, "active"_n},
                        {.expiration = TimePointSec::from("2020-07-10T08:40:20").value(),
                         .chainId = ChainId::from("1064487b3cd1a897ce03ae5b6a865651747e2e152090f9"
                                                  "9c1d19d44e01aea5a4")
                                        .value()})
                .value();
        const auto key =
            PrivateKey::from("PVT_K1_2wFL8Ne8JoGrxz6GdnfB7d4yhUYpqNgubHeKUC64qT3XE6Ro84").value();
        const auto sig = key.signDigest(resolved.signingDigest()).value();
        const auto callback = resolved.getCallback({sig}).value();
        REQUIRE(callback.has_value());
        CHECK(callback->background == false);
        CHECK(callback->url ==
              "myapp://login=1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4");
        CHECK(deepEqual(callback->payload, parse(R"({
            "sig": "SIG_K1_K4nkCupUx3hDXSHq4rhGPpDMPPPjJyvmF3M6j7ppYUzkR3L93endwnxf3YhJSG4SSvxxU1ytD8hj39kukTeYxjwy5H3XNJ",
            "tx": "b8e921a7b68d7309847e633d74963f25eb5a7d0b15b1aceb143723c234686a8d",
            "rbn": "0",
            "rid": "0",
            "ex": "2020-07-10T08:40:20",
            "req": "esr://AwAAAwAAAAAAAChdAAAVbXlhcHA6Ly9sb2dpbj17e2NpZH19AQljaGFpbl9pZHMFAgABAAo",
            "sa": "foo",
            "sp": "active",
            "cid": "1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4"
        })")));
        const auto recreated =
            ResolvedSigningRequest::fromPayload(callback->payload, {.zlib = false}).value();
        CHECK(recreated.request.encode() == req.encode());
        CHECK(recreated.chainId.hexString() ==
              "1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4");
        const auto proof = recreated.getIdentityProof(sig).value();
        CHECK(proof.toString() ==
              "EOSIO EGRIezzRqJfOA65baoZWUXR+LhUgkPmcHRnUTgGupaQAAAAAAAAoXXQpCF8AAAAAAAAoXQAAAACo"
              "7TIyAB9I36p6NdMCKzksNwp4nFbiEhq8/sVAeji/4JMzk/CHAwc5ipaF8G/SNuXkJ9XWaDSu98DWzbXuva"
              "VcimXUvGDQ");
        const auto recreatedProof = IdentityProof::fromString(proof.toString()).value();
        Authority auth;
        auth.threshold = 4;
        KeyWeight kw;
        kw.key = key.toPublic().value();
        kw.weight = uint16_t(4);
        auth.keys.push_back(kw);
        CHECK(recreatedProof.verify(auth, TimePointSec::from("2020-07-10T08:00:00").value())
                  .value());
        CHECK_FALSE(
            recreatedProof.verify(auth, TimePointSec::from("2020-07-10T09:00:00").value())
                .value());
    }
}
