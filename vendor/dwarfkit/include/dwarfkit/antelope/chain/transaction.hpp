// Port of antelope src/chain/transaction.ts
#pragma once

#include <dwarfkit/antelope/chain/action.hpp>
#include <dwarfkit/antelope/chain/checksum.hpp>
#include <dwarfkit/antelope/chain/signature.hpp>
#include <dwarfkit/antelope/chain/time.hpp>

namespace dwarfkit {

struct TransactionExtension {
    DK_STRUCT("transaction_extension")
    uint16_t type = 0;
    Bytes data;
    DK_FIELDS(type, data)
};

struct TransactionHeader {
    DK_STRUCT("transaction_header")
    // The time at which a transaction expires.
    TimePointSec expiration;
    // Specifies a block num in the last 2^16 blocks.
    uint16_t ref_block_num = 0;
    // Specifies the lower 32 bits of the block id.
    uint32_t ref_block_prefix = 0;
    // Upper limit on total network bandwidth (in 8 byte words) billed for this transaction.
    VarUInt max_net_usage_words;
    // Upper limit on the total CPU time billed for this transaction.
    uint8_t max_cpu_usage_ms = 0;
    // Number of seconds to delay this transaction for during which it may be canceled.
    VarUInt delay_sec;
    DK_FIELDS(expiration, ref_block_num, ref_block_prefix, max_net_usage_words, max_cpu_usage_ms,
              delay_sec)

    static Result<TransactionHeader> from(const json& object);
};

// {contract, abi} pair for resolving the actions of a transaction.
struct AbiProviderEntry {
    Name contract;
    ABI abi;
};

struct Transaction : TransactionHeader {
    DK_STRUCT_BASE("transaction", TransactionHeader)
    // The context free actions in the transaction.
    std::vector<Action> context_free_actions;
    // The actions in the transaction.
    std::vector<Action> actions;
    // Transaction extensions.
    std::vector<TransactionExtension> transaction_extensions;
    DK_FIELDS(context_free_actions, actions, transaction_extensions)

    static Result<Transaction> from(const json& object);
    static Result<Transaction> from(const json& object, const ABI& abi);
    static Result<Transaction> from(const json& object, const std::vector<AbiProviderEntry>& abis);
    // Also accepts a SignedTransaction, slicing off the signatures (upstream
    // Transaction.from does the same in PackedTransaction.fromSigned).
    static Transaction from(const Transaction& object) { return object; }

    // Return true if this transaction is equal to given transaction.
    bool equals(const Transaction& other) const { return id() == other.id(); }

    Checksum256 id() const;
    Checksum256 signingDigest(const Checksum256& chainId) const;
    Bytes signingData(const Checksum256& chainId) const;
};

struct SignedTransaction : Transaction {
    DK_STRUCT_BASE("signed_transaction", Transaction)
    // List of signatures.
    std::vector<Signature> signatures;
    // Context-free action data, for each context-free action, there is an entry here.
    std::vector<Bytes> context_free_data;
    DK_FIELDS(signatures, context_free_data)

    static Result<SignedTransaction> from(const json& object);
    static SignedTransaction from(const SignedTransaction& object) { return object; }

    // The transaction without the signatures.
    Transaction transaction() const { return static_cast<const Transaction&>(*this); }

    Checksum256 id() const { return transaction().id(); }
};

// https://github.com/AntelopeIO/leap: none = 0, zlib = 1
enum class CompressionType : uint8_t { none = 0, zlib = 1 };

struct PackedTransaction {
    DK_STRUCT("packed_transaction")
    std::vector<Signature> signatures;
    uint8_t compression = 0;
    Bytes packed_context_free_data;
    Bytes packed_trx;
    DK_FIELDS(signatures, compression, packed_context_free_data, packed_trx)

    static Result<PackedTransaction> from(const json& object);
    static PackedTransaction from(const PackedTransaction& object) { return object; }
    static Result<PackedTransaction> fromSigned(const SignedTransaction& signed_,
                                                CompressionType compression = CompressionType::zlib);

    Result<Transaction> getTransaction() const;
    Result<SignedTransaction> getSignedTransaction() const;
};

struct TransactionReceipt {
    DK_STRUCT("transaction_receipt")
    std::string status;
    uint32_t cpu_usage_us = 0;
    uint32_t net_usage_words = 0;
    DK_FIELDS(status, cpu_usage_us, net_usage_words)
};

}  // namespace dwarfkit
