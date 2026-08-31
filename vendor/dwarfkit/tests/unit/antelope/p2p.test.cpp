// Port of antelope test/p2p.ts: every net_message alternative round-trips
// through the client raw and through the envelope framing, plus the heartbeat
// trigger and the block header getters.
#include <doctest/doctest.h>

#include <dwarfkit/antelope.hpp>

using namespace dwarfkit;
using namespace dwarfkit::p2p;

namespace {

// test/utils/mock-p2p-provider.ts
class MockP2PProvider final : public P2PProvider {
public:
    std::function<void(std::span<const uint8_t>)> writeImpl;

    void write(std::span<const uint8_t> data, P2PHandler done) override {
        REQUIRE_MESSAGE(static_cast<bool>(writeImpl), "Unexpected call to P2PProvider.write");
        writeImpl(data);
        if (done) {
            done();
        }
    }
    void end(P2PHandler) override { FAIL("Unexpected call to P2PProvider.end"); }
    void destroy(const std::optional<Error>&) override {
        FAIL("Unexpected call to P2PProvider.destroy");
    }
    void onData(P2PDataHandler handler) override { dataHandlers.push_back(std::move(handler)); }
    void onError(P2PErrorHandler handler) override {
        errorHandlers.push_back(std::move(handler));
    }
    void onClose(P2PHandler handler) override { closeHandlers.push_back(std::move(handler)); }

    void emitData(std::span<const uint8_t> data) {
        REQUIRE(!dataHandlers.empty());
        for (const auto& handler : dataHandlers) {
            handler(data);
        }
    }

    std::vector<P2PDataHandler> dataHandlers;
    std::vector<P2PErrorHandler> errorHandlers;
    std::vector<P2PHandler> closeHandlers;
};

struct MockClient {
    MockP2PProvider mock;
    std::unique_ptr<SimpleEnvelopeP2PProvider> envelope;
    std::unique_ptr<P2PClient> client;
};

std::unique_ptr<MockClient> makeMockClient(bool enveloped,
                                           const P2PClientOptions& extra = {}) {
    auto rv = std::make_unique<MockClient>();
    P2PClientOptions options = extra;
    if (enveloped) {
        rv->envelope = std::make_unique<SimpleEnvelopeP2PProvider>(&rv->mock);
        options.provider = rv->envelope.get();
    } else {
        options.provider = &rv->mock;
    }
    rv->client = std::make_unique<P2PClient>(options);
    rv->client->onError([](const Error& error) { FAIL(error.message); });
    return rv;
}

struct MessageVector {
    NetMessage message;
    std::string dataHex;
};

// the raw payload hex from test/p2p.ts, one literal per line
// clang-format off
const char* handshakeHex = "fe0001ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff0102ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff02000223e0ae8aacb41b06dc74af1a56b2eb69133f07f7f75bd1d5e53316bff195edf4010000000000000003ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff0300205150a67288c3b393fdba9061b05019c54b12bdac295fc83bebad7cd63c7bb67d5cb8cc220564da006240a58419f64d06a5c6e1fc62889816a6c3dfdd231ed38903666f6f0200000004ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff040300000005ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff05036261720362617a0400";
const char* chainSizeHex = "0200000004ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff040300000005ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff05";
const char* goAwayHex = "0901ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff01";
const char* timeHex = "0100000000000000020000000000000003000000000000000400000000000000";
const char* noticeHex = "0101ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff010102ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff02";
const char* syncHex = "0600000000040000";
const char* signedBlockHex = "e1f95c450000000000ea3055000000000006cdcc27dd5599c2bf11d6086315f7f3e20dab443b28b7a3a7b3e9b4980000000000000000000000000000000000000000000000000000000000000000c92b7fe28da371253c0764688c82cd4c3755c88580e5f3587243f3a98934554e000000000000001f490bd9651e56b29585221deb072e4f13b3cd201e7167cd48952b872070d71b380596305b55bbd2c1029f3f29b7534ed56447cc7b4d07e5a52890a8d32c449e0c0000";
const char* packedHex = "0100205150a67288c3b393fdba9061b05019c54b12bdac295fc83bebad7cd63c7bb67d5cb8cc220564da006240a58419f64d06a5c6e1fc62889816a6c3dfdd231ed389000050408c395b796efe6596160000000001a09861f648958566000000000080694a01a0986af74a94be6400000000a8ed32321e1d766f74652067753274656d6271676167652c207765206c6f766520424d00";
// clang-format on

json signedBlockJson() {
    return json{{"timeSlot", 1163721185},
                {"producer", "eosio"},
                {"confirmed", 0},
                {"previous",
                 "00000006cdcc27dd5599c2bf11d6086315f7f3e20dab443b28b7a3a7b3e9b498"},
                {"transaction_mroot",
                 "0000000000000000000000000000000000000000000000000000000000000000"},
                {"action_mroot",
                 "c92b7fe28da371253c0764688c82cd4c3755c88580e5f3587243f3a98934554e"},
                {"schedule_version", 0},
                {"producer_signature",
                 "SIG_K1_K4p4AW5xFwKbxRRUQjGMVfS1x5vSfbaLHMiCQpPgRESfQ6S3iXbcdzBYrhqup3sLgF1qNWGN"
                 "P5Jio1uS2iKoPquUvifw8G"},
                {"transactions", json::array()},
                {"header_extensions", json::array()},
                {"block_extensions", json::array()}};
}

std::vector<MessageVector> testMessages() {
    std::vector<MessageVector> rv;
    rv.push_back(
        {structFrom<HandshakeMessage>(
             json{{"networkVersion", 0xfe},
                  {"chainId", "01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff01"},
                  {"nodeId", "02ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff02"},
                  {"key", "PUB_K1_5AHoNnWetuDhKWSDx3WUf8W7Dg5xjHCMc4yHmmSiaJCFvvAgnB"},
                  {"time", 1},
                  {"token", "03ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff03"},
                  {"sig",
                   "SIG_K1_KfPLgpw35iX8nfDzhbcmSBCr7nEGNEYXgmmempQspDJYBCKuAEs5rm3s4ZuLJY428Ca8Zh"
                   "vR2Dkwu118y3NAoMDxhicRj9"},
                  {"p2pAddress", "foo"},
                  {"lastIrreversibleBlockNumber", 2},
                  {"lastIrreversibleBlockId", "04ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff04"},
                  {"headNum", 3},
                  {"headId", "05ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff05"},
                  {"os", "bar"},
                  {"agent", "baz"},
                  {"generation", 4}})
             .value(),
         handshakeHex});
    rv.push_back({structFrom<ChainSizeMessage>(
                      json{{"lastIrreversibleBlockNumber", 2},
                           {"lastIrreversibleBlockId", "04ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff04"},
                           {"headNum", 3},
                           {"headId", "05ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff05"}})
                      .value(),
                  chainSizeHex});
    rv.push_back({structFrom<GoAwayMessage>(json{{"reason", 9}, {"nodeId", "01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff01"}}).value(),
                  goAwayHex});
    rv.push_back(
        {structFrom<TimeMessage>(json{{"org", 1}, {"rec", 2}, {"xmt", 3}, {"dst", 4}}).value(),
         timeHex});
    rv.push_back({structFrom<NoticeMessage>(
                      json{{"knownTrx", json::array({"01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff01"})},
                           {"knownBlocks", json::array({"02ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff02"})}})
                      .value(),
                  noticeHex});
    rv.push_back({structFrom<RequestMessage>(
                      json{{"reqTrx", json::array({"01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff01"})},
                           {"reqBlocks", json::array({"02ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff02"})}})
                      .value(),
                  noticeHex});
    rv.push_back(
        {structFrom<SyncRequestMessage>(json{{"startBlock", 6}, {"endBlock", 1024}}).value(),
         syncHex});
    rv.push_back({structFrom<SignedBlock>(signedBlockJson()).value(), signedBlockHex});
    rv.push_back(
        {PackedTransaction::from(
             json{{"compression", 0},
                  {"packed_context_free_data", ""},
                  {"packed_trx",
                   "408c395b796efe6596160000000001a09861f648958566000000000080694a01a0986af74a94be"
                   "6400000000a8ed32321e1d766f74652067753274656d6271676167652c207765206c6f76652042"
                   "4d00"},
                  {"signatures",
                   json::array({"SIG_K1_KfPLgpw35iX8nfDzhbcmSBCr7nEGNEYXgmmempQspDJYBCKuAEs5rm"
                                "3s4ZuLJY428Ca8ZhvR2Dkwu118y3NAoMDxhicRj9"})}})
             .value(),
         packedHex});
    return rv;
}

std::vector<uint8_t> variantBytes(const MessageVector& vector) {
    const auto payload = Bytes::from(vector.dataHex).value();
    std::vector<uint8_t> rv;
    rv.push_back(static_cast<uint8_t>(vector.message.variantIdx()));
    rv.insert(rv.end(), payload.array.begin(), payload.array.end());
    return rv;
}

std::vector<uint8_t> socketBytes(const std::vector<uint8_t>& vdata) {
    std::vector<uint8_t> rv(4 + vdata.size());
    const uint32_t length = static_cast<uint32_t>(vdata.size());
    std::memcpy(rv.data(), &length, 4);
    std::memcpy(rv.data() + 4, vdata.data(), vdata.size());
    return rv;
}

}  // namespace

TEST_SUITE("p2p") {
    TEST_CASE("message round trips") {
        for (const auto& vector : testMessages()) {
            CAPTURE(vector.message.variantName());
            const auto vdata = variantBytes(vector);
            const auto framed = socketBytes(vdata);

            // receive
            {
                auto setup = makeMockClient(false);
                bool called = false;
                setup->client->onMessage(
                    [&](const NetMessage& message)
                    {
                        CHECK(Serializer::objectify(message) ==
                              Serializer::objectify(vector.message));
                        called = true;
                    });
                setup->mock.emitData(vdata);
                CHECK(called);
            }
            // receive through the envelope, split across two chunks
            {
                auto setup = makeMockClient(true);
                bool called = false;
                setup->client->onMessage(
                    [&](const NetMessage& message)
                    {
                        CHECK(Serializer::objectify(message) ==
                              Serializer::objectify(vector.message));
                        called = true;
                    });
                const size_t pivot = framed.size() / 2;
                setup->mock.emitData(std::span<const uint8_t>(framed.data(), pivot));
                CHECK_FALSE(called);
                setup->mock.emitData(
                    std::span<const uint8_t>(framed.data() + pivot, framed.size() - pivot));
                CHECK(called);
            }
            // send
            {
                auto setup = makeMockClient(false);
                std::vector<uint8_t> sent;
                setup->mock.writeImpl = [&](std::span<const uint8_t> data)
                { sent.assign(data.begin(), data.end()); };
                CHECK(setup->client->send(vector.message).has_value());
                CHECK(sent == vdata);
            }
            // send through the envelope
            {
                auto setup = makeMockClient(true);
                std::vector<uint8_t> sent;
                setup->mock.writeImpl = [&](std::span<const uint8_t> data)
                { sent.assign(data.begin(), data.end()); };
                CHECK(setup->client->send(vector.message).has_value());
                CHECK(sent == framed);
            }
        }
    }

    TEST_CASE("heartbeat trigger") {
        bool setTimeoutCalled = false;
        std::function<void()> fireTimeout;
        P2PClientOptions extra;
        extra.heartbeatTimoutMs = 6789;
        extra.setTimeoutImpl = [&](std::function<void()> handler, int timeoutMs)
        {
            CHECK(timeoutMs == 6789);
            setTimeoutCalled = true;
            fireTimeout = std::move(handler);
        };
        auto setup = makeMockClient(false, extra);
        CHECK(setTimeoutCalled);

        std::vector<uint8_t> sent;
        setup->mock.writeImpl = [&](std::span<const uint8_t> data)
        { sent.assign(data.begin(), data.end()); };
        fireTimeout();
        REQUIRE(!sent.empty());
        const auto message = Serializer::decode<NetMessage>(sent).value();
        CHECK(message.variantName() == "time_message");
    }

    TEST_CASE("block header getters") {
        const auto block = structFrom<SignedBlock>(signedBlockJson()).value();
        CHECK(block.id().value().hexString() ==
              "000000075fbe6bbad86e424962a190e8309394b7bff4bf3e16b0a2a71e5a617c");
        CHECK(block.blockNum() == 7);
    }
}
