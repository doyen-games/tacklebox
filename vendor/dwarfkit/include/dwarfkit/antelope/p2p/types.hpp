// Port of antelope src/p2p/types.ts: the nodeos net-plugin message structs
// and the net_message variant.
#pragma once

#include <dwarfkit/antelope/chain/block_id.hpp>
#include <dwarfkit/antelope/chain/transaction.hpp>
#include <dwarfkit/antelope/serializer.hpp>

namespace dwarfkit::p2p {

struct HandshakeMessage {
    DK_STRUCT("handshake_message")
    uint16_t networkVersion = 0;
    Checksum256 chainId;
    Checksum256 nodeId;
    PublicKey key;
    int64_t time = 0;
    Checksum256 token;
    Signature sig;
    std::string p2pAddress;
    uint32_t lastIrreversibleBlockNumber = 0;
    BlockId lastIrreversibleBlockId;
    uint32_t headNum = 0;
    BlockId headId;
    std::string os;
    std::string agent;
    int16_t generation = 0;
    DK_FIELDS(networkVersion, chainId, nodeId, key, time, token, sig, p2pAddress,
              lastIrreversibleBlockNumber, lastIrreversibleBlockId, headNum, headId, os, agent,
              generation)
};

struct ChainSizeMessage {
    DK_STRUCT("chain_size_message")
    uint32_t lastIrreversibleBlockNumber = 0;
    BlockId lastIrreversibleBlockId;
    uint32_t headNum = 0;
    BlockId headId;
    DK_FIELDS(lastIrreversibleBlockNumber, lastIrreversibleBlockId, headNum, headId)
};

struct GoAwayMessage {
    DK_STRUCT("go_away_message")
    uint8_t reason = 0;
    Checksum256 nodeId;
    DK_FIELDS(reason, nodeId)
};

struct TimeMessage {
    DK_STRUCT("time_message")
    int64_t org = 0;
    int64_t rec = 0;
    int64_t xmt = 0;
    int64_t dst = 0;
    DK_FIELDS(org, rec, xmt, dst)
};

struct NoticeMessage {
    DK_STRUCT("notice_message")
    std::vector<Checksum256> knownTrx;
    std::vector<BlockId> knownBlocks;
    DK_FIELDS(knownTrx, knownBlocks)
};

struct RequestMessage {
    DK_STRUCT("request_message")
    std::vector<Checksum256> reqTrx;
    std::vector<BlockId> reqBlocks;
    DK_FIELDS(reqTrx, reqBlocks)
};

struct SyncRequestMessage {
    DK_STRUCT("sync_request_message")
    uint32_t startBlock = 0;
    uint32_t endBlock = 0;
    DK_FIELDS(startBlock, endBlock)
};

struct NewProducersEntry {
    DK_STRUCT("new_producers_entry")
    Name producer_name;
    PublicKey block_signing_key;
    DK_FIELDS(producer_name, block_signing_key)
};

struct NewProducers {
    DK_STRUCT("new_producers")
    uint32_t version = 0;
    std::vector<NewProducersEntry> producers;
    DK_FIELDS(version, producers)
};

struct BlockExtension {
    DK_STRUCT("block_extension")
    uint16_t type = 0;
    Bytes data;
    DK_FIELDS(type, data)
};

struct HeaderExtension {
    DK_STRUCT("header_extension")
    uint16_t type = 0;
    Bytes data;
    DK_FIELDS(type, data)
};

DK_VARIANT(TrxVariant, "trx_variant", Checksum256, PackedTransaction)

struct FullTransactionReceipt {
    DK_STRUCT("full_transaction_receipt")
    uint8_t status = 0;
    uint32_t cpu_usage_us = 0;
    VarUInt net_usage_words;
    TrxVariant trx;
    DK_FIELDS(status, cpu_usage_us, net_usage_words, trx)
};

struct BlockHeader {
    DK_STRUCT("block_header")
    uint32_t timeSlot = 0;
    Name producer;
    uint16_t confirmed = 0;
    BlockId previous;
    BlockId transaction_mroot;
    BlockId action_mroot;
    uint32_t schedule_version = 0;
    std::optional<NewProducers> new_producers;
    std::vector<json> header_extensions;
    DK_FIELDS(timeSlot, producer, confirmed, previous, transaction_mroot, action_mroot,
              schedule_version, new_producers, header_extensions)

    uint32_t blockNum() const { return previous.blockNum() + 1; }
    Result<BlockId> id() const;
};

struct SignedBlock : BlockHeader {
    DK_STRUCT_BASE("signed_block", BlockHeader)
    Signature producer_signature;
    std::vector<FullTransactionReceipt> transactions;
    std::vector<BlockExtension> block_extensions;
    DK_FIELDS(producer_signature, transactions, block_extensions)
};

DK_VARIANT(NetMessage, "net_message", HandshakeMessage, ChainSizeMessage, GoAwayMessage,
           TimeMessage, NoticeMessage, RequestMessage, SyncRequestMessage, SignedBlock,
           PackedTransaction)

}  // namespace dwarfkit::p2p
