#include <dwarfkit/antelope/chain/transaction.hpp>

#include <dwarfkit/core/zlib.hpp>

namespace dwarfkit {

namespace {

json withHeaderDefaults(const json& object) {
    json merged = {{"max_net_usage_words", 0}, {"max_cpu_usage_ms", 0}, {"delay_sec", 0}};
    merged.update(object);
    return merged;
}

// resolve untyped actions, looking up an ABI per contract when provided
Result<std::vector<Action>> resolveActions(const json& list,
                                           const std::vector<AbiProviderEntry>* abis,
                                           const ABI* singleAbi) {
    std::vector<Action> rv;
    if (list.is_null()) return rv;
    for (const auto& entry : list) {
        const ABI* abi = singleAbi;
        if (abis) {
            abi = nullptr;
            const Name contract =
                Name::from(entry.contains("account") ? entry.at("account").get<std::string>() : "");
            for (const auto& candidate : *abis) {
                if (candidate.contract == contract) {
                    abi = &candidate.abi;
                    break;
                }
            }
        }
        if (abi) {
            DK_TRY(action, Action::from(entry, *abi));
            rv.push_back(std::move(action));
        } else {
            DK_TRY(action, Action::from(entry));
            rv.push_back(std::move(action));
        }
    }
    return rv;
}

Result<Transaction> transactionFrom(const json& object, const std::vector<AbiProviderEntry>* abis,
                                    const ABI* singleAbi) {
    DK_TRY(header, structFrom<TransactionHeader>(withHeaderDefaults(object)));
    Transaction tx;
    static_cast<TransactionHeader&>(tx) = std::move(header);
    DK_TRY(actions, resolveActions(object.value("actions", json(nullptr)), abis, singleAbi));
    tx.actions = std::move(actions);
    DK_TRY(cfActions,
           resolveActions(object.value("context_free_actions", json(nullptr)), abis, singleAbi));
    tx.context_free_actions = std::move(cfActions);
    if (object.contains("transaction_extensions") &&
        !object.at("transaction_extensions").is_null()) {
        DK_TRY(extensions, abi_traits<std::vector<TransactionExtension>>::fromJSON(
                               object.at("transaction_extensions")));
        tx.transaction_extensions = std::move(extensions);
    }
    return tx;
}

}  // namespace

Result<TransactionHeader> TransactionHeader::from(const json& object) {
    return structFrom<TransactionHeader>(withHeaderDefaults(object));
}

Result<Transaction> Transaction::from(const json& object) {
    return transactionFrom(object, nullptr, nullptr);
}

Result<Transaction> Transaction::from(const json& object, const ABI& abi) {
    return transactionFrom(object, nullptr, &abi);
}

Result<Transaction> Transaction::from(const json& object,
                                      const std::vector<AbiProviderEntry>& abis) {
    return transactionFrom(object, &abis, nullptr);
}

Checksum256 Transaction::id() const {
    // encoding a well-formed transaction cannot fail
    ABIEncoder encoder;
    (void)abi_traits<Transaction>::toABI(*this, encoder);
    return Checksum256::hash(encoder.getData());
}

Bytes Transaction::signingData(const Checksum256& chainId) const {
    Bytes data(std::vector<uint8_t>(chainId.array.begin(), chainId.array.end()));
    ABIEncoder encoder;
    (void)abi_traits<Transaction>::toABI(*this, encoder);
    data.append(encoder.getData());
    data.append(std::vector<uint8_t>(32, 0));
    return data;
}

Checksum256 Transaction::signingDigest(const Checksum256& chainId) const {
    return Checksum256::hash(signingData(chainId));
}

Result<SignedTransaction> SignedTransaction::from(const json& object) {
    DK_TRY(tx, Transaction::from(object));
    SignedTransaction rv;
    static_cast<Transaction&>(rv) = std::move(tx);
    if (object.contains("signatures") && !object.at("signatures").is_null()) {
        DK_TRY(signatures,
               abi_traits<std::vector<Signature>>::fromJSON(object.at("signatures")));
        rv.signatures = std::move(signatures);
    }
    if (object.contains("context_free_data") && !object.at("context_free_data").is_null()) {
        DK_TRY(cfd, abi_traits<std::vector<Bytes>>::fromJSON(object.at("context_free_data")));
        rv.context_free_data = std::move(cfd);
    }
    return rv;
}

Result<PackedTransaction> PackedTransaction::from(const json& object) {
    json merged = {{"signatures", json::array()},
                   {"packed_context_free_data", ""},
                   {"compression", 0}};
    merged.update(object);
    return structFrom<PackedTransaction>(merged);
}

Result<PackedTransaction> PackedTransaction::fromSigned(const SignedTransaction& signed_,
                                                        CompressionType compression) {
    // Encode data
    DK_TRY(packedTrx, Serializer::encode(Transaction::from(signed_)));
    DK_TRY(packedCfd, Serializer::encode(signed_.context_free_data));
    if (compression == CompressionType::zlib) {
        // compress data
        DK_TRY(compressedTrx, zlibCompress(packedTrx.array));
        packedTrx = Bytes(std::move(compressedTrx));
        DK_TRY(compressedCfd, zlibCompress(packedCfd.array));
        packedCfd = Bytes(std::move(compressedCfd));
    }
    PackedTransaction rv;
    rv.signatures = signed_.signatures;
    rv.compression = static_cast<uint8_t>(compression);
    rv.packed_context_free_data = std::move(packedCfd);
    rv.packed_trx = std::move(packedTrx);
    return rv;
}

Result<Transaction> PackedTransaction::getTransaction() const {
    switch (compression) {
        case 0:
            return Serializer::decode<Transaction>(packed_trx);
        case 1: {
            DK_TRY(inflated, zlibUncompress(packed_trx.array));
            return Serializer::decode<Transaction>(std::span<const uint8_t>(inflated));
        }
        default:
            return err(ErrorKind::Invalid,
                       "Unknown transaction compression " + std::to_string(compression));
    }
}

Result<SignedTransaction> PackedTransaction::getSignedTransaction() const {
    DK_TRY(transaction, getTransaction());
    // TODO: decode context free data
    SignedTransaction rv;
    static_cast<Transaction&>(rv) = std::move(transaction);
    rv.signatures = signatures;
    return rv;
}

}  // namespace dwarfkit
