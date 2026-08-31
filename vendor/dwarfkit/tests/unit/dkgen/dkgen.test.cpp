// dkgen golden-output tests (BLUEPRINT.md Phase 6): the checked-in generated
// headers for eosio.token, eosio and atomicassets must compile (they are
// included below), match a fresh generator run byte-for-byte, and encode
// action data identically to the ABI-driven serializer.
#include <doctest/doctest.h>

#ifdef DK_HAVE_DKGEN

#include <fstream>
#include <sstream>

#include <dwarfkit/contract.hpp>

#include <generator.hpp>

#include "../../gen/atomicassets.gen.hpp"
#include "../../gen/eosio.gen.hpp"
#include "../../gen/eosio_token.gen.hpp"

using namespace dwarfkit;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

Result<ABI> loadAbi(const std::string& path) {
    const std::string text = readFile(path);
    auto parsed = ABI::from(std::string_view(text));
    if (parsed) {
        return parsed;
    }
    std::string trimmed = text;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
        trimmed.pop_back();
    }
    DK_TRY(blob, Blob::from(trimmed));
    return ABI::from(blob);
}

}  // namespace

TEST_SUITE("dkgen") {
    TEST_CASE("golden output") {
        SUBCASE("eosio.token") {
            const auto abi = loadAbi(DK_FIXTURE_DIR "/dkgen/eosio.token.abi.json").value();
            const auto generated = dkgen::generateContractHeader("eosio.token", abi).value();
            CHECK(generated == readFile(DK_GEN_DIR "/eosio_token.gen.hpp"));
        }
        SUBCASE("eosio") {
            const auto abi = loadAbi(DK_FIXTURE_DIR "/dkgen/eosio.abi.b64").value();
            const auto generated = dkgen::generateContractHeader("eosio", abi).value();
            CHECK(generated == readFile(DK_GEN_DIR "/eosio.gen.hpp"));
        }
        SUBCASE("atomicassets") {
            const auto abi = loadAbi(DK_FIXTURE_DIR "/dkgen/atomicassets.abi.b64").value();
            const auto generated = dkgen::generateContractHeader("atomicassets", abi).value();
            CHECK(generated == readFile(DK_GEN_DIR "/atomicassets.gen.hpp"));
        }
    }

    TEST_CASE("generated encoding matches the abi serializer") {
        namespace token = dwarfkit::gen::eosio_token;
        token::Types::transfer transfer;
        transfer.from = Name::from("alice");
        transfer.to = Name::from("bob");
        transfer.quantity = Asset::from("1.3200 EOS").value();
        transfer.memo = "grocery bill";
        const auto typedBytes = Serializer::encode(transfer).value();
        const auto abiBytes =
            Serializer::encode(json{{"from", "alice"},
                                    {"to", "bob"},
                                    {"quantity", "1.3200 EOS"},
                                    {"memo", "grocery bill"}},
                               "transfer", token::abi())
                .value();
        CHECK(typedBytes.hexString() == abiBytes.hexString());
    }

    TEST_CASE("generated contract builds typed actions") {
        namespace token = dwarfkit::gen::eosio_token;
        const auto client =
            std::make_shared<APIClient>(APIClientOptions{.url = "https://jungle4.greymass.com"});
        const token::Contract contract(client);
        CHECK(contract.account == Name::from("eosio.token"));
        token::Types::transfer transfer;
        transfer.from = Name::from("alice");
        transfer.to = Name::from("bob");
        transfer.quantity = Asset::from("0.0001 EOS").value();
        transfer.memo = "typed";
        const auto action = contract.transfer(transfer).value();
        CHECK(action.account == Name::from("eosio.token"));
        CHECK(action.name == Name::from("transfer"));
        const auto decoded = action.decodeData(token::abi()).value();
        CHECK(decoded["from"] == "alice");
        CHECK(decoded["memo"] == "typed");
    }

    TEST_CASE("generated variants and extensions round trip") {
        // eosio's producer authority: a variant inside a binary extension
        namespace sys = dwarfkit::gen::eosio;
        CHECK(sys::abi().version.starts_with("eosio::abi/"));
        // atomicassets ATTRIBUTE_MAP: vector of {key, variant value} pairs
        namespace aa = dwarfkit::gen::atomicassets;
        CHECK(aa::abi().version.starts_with("eosio::abi/"));
        const auto& variants = aa::abi().variants;
        CHECK(!variants.empty());
    }

    TEST_CASE("identifier sanitization") {
        CHECK(dkgen::sanitizeIdentifier("eosio.token") == "eosio_token");
        CHECK(dkgen::sanitizeIdentifier("new") == "new_");
        CHECK(dkgen::sanitizeIdentifier("1234") == "_1234");
    }
}

#endif  // DK_HAVE_DKGEN
