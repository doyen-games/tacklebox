// Port of signing-request src/signing-request.ts (EOSIO Signing Request, ESR).
// zlib is built in (raw deflate); the upstream ZlibProvider option becomes a
// bool. Promise-returning methods block.
#pragma once

#include <map>
#include <variant>

#include <dwarfkit/signing_request/abi.hpp>
#include <dwarfkit/signing_request/abi_provider.hpp>
#include <dwarfkit/signing_request/base64u.hpp>

namespace dwarfkit {

// Current supported protocol version, backwards compatible with version 2.
inline constexpr int ProtocolVersion = 3;

// Interface that should be implemented by signature providers.
struct SignatureProvider {
    // Sign 32-byte message and return signer name and signature.
    virtual Result<RequestSignature> sign(const Checksum256& message) = 0;
    virtual ~SignatureProvider() = default;
};

// The placeholder name: ............1 aka uint64(1). Resolves to the signer.
inline const Name PlaceholderName = Name::from("............1");
// Placeholder that will resolve to signer permission name, uint64(2).
inline const Name PlaceholderPermission = Name::from("............2");
inline const PermissionLevel PlaceholderAuth{PlaceholderName, PlaceholderPermission};

struct ResolvedAction {
    DK_STRUCT("resolved_action")
    Name account;
    Name name;
    std::vector<PermissionLevel> authorization;
    json data;  // decoded action data
    DK_FIELDS(account, name, authorization, data)
};

struct ResolvedTransaction {
    DK_STRUCT("resolved_transaction")
    TimePointSec expiration;
    uint16_t ref_block_num = 0;
    uint32_t ref_block_prefix = 0;
    VarUInt max_net_usage_words;
    uint8_t max_cpu_usage_ms = 0;
    VarUInt delay_sec;
    std::vector<ResolvedAction> context_free_actions;
    std::vector<ResolvedAction> actions;
    std::vector<TransactionExtension> transaction_extensions;
    DK_FIELDS(expiration, ref_block_num, ref_block_prefix, max_net_usage_words, max_cpu_usage_ms,
              delay_sec, context_free_actions, actions, transaction_extensions)
};

// Context used to resolve a callback; url with {{placeholders}} templated in.
struct ResolvedCallback {
    std::string url;
    bool background = false;
    json payload;  // the CallbackPayload object
};

// Context used to resolve a transaction.
struct TransactionContext {
    std::optional<TimePointSec> timestamp;
    std::optional<uint32_t> expire_seconds;
    std::optional<uint32_t> block_num;
    std::optional<uint16_t> ref_block_num;
    std::optional<uint32_t> ref_block_prefix;
    std::optional<TimePointSec> expiration;
    std::optional<ChainId> chainId;
};

struct CallbackType {
    std::string url;
    bool background = false;
};

struct SigningRequestCreateArguments {
    // Exactly one of action, actions, transaction or identity.
    std::optional<json> action;
    std::optional<json> actions;
    std::optional<json> transaction;
    // Identity request: {scope?, permission?}.
    std::optional<json> identity;
    // Chain ID to use; anyChain makes a multi-chain request (TS chainId: null).
    std::optional<ChainId> chainId;
    bool anyChain = false;
    // Chain IDs to constrain a multi-chain request to.
    std::vector<ChainId> chainIds;
    // Whether wallet should broadcast tx, defaults to true.
    std::optional<bool> broadcast;
    std::optional<CallbackType> callback;
    // Optional metadata to pass along with the request.
    std::vector<InfoPair> info;
};

struct SigningRequestCreateIdentityArguments {
    CallbackType callback;
    std::optional<Name> account;
    std::optional<Name> permission;
    std::optional<Name> scope;
    std::optional<ChainId> chainId;
    bool anyChain = false;
    std::vector<ChainId> chainIds;
    std::vector<InfoPair> info;
};

struct SigningRequestEncodingOptions {
    // Compress when encoding (upstream: only when a zlib provider is passed).
    bool zlib = true;
    // Abi provider, required if the arguments contain un-encoded actions.
    AbiProvider* abiProvider = nullptr;
    // Used to create a request signature at creation time if provided.
    SignatureProvider* signatureProvider = nullptr;
};

using AbiMap = std::map<std::string, ABI>;

class ResolvedSigningRequest;

class SigningRequest {
public:
    // Create a new signing request. Fetches any required ABIs through the
    // provider (the TS create/createSync pair collapses into one blocking call).
    static Result<SigningRequest> create(const SigningRequestCreateArguments& args,
                                         const SigningRequestEncodingOptions& options = {},
                                         const AbiMap& abis = {});

    // Creates an identity request.
    static Result<SigningRequest> identity(const SigningRequestCreateIdentityArguments& args,
                                           const SigningRequestEncodingOptions& options = {});

    // Create a request from a chain id and serialized transaction.
    static Result<SigningRequest> fromTransaction(
        const ChainId& chainId, std::span<const uint8_t> serializedTransaction,
        const SigningRequestEncodingOptions& options = {});

    // Creates a signing request from an encoded esr: uri string.
    static Result<SigningRequest> from(std::string_view uri,
                                       const SigningRequestEncodingOptions& options = {});
    static Result<SigningRequest> fromData(std::span<const uint8_t> data,
                                           const SigningRequestEncodingOptions& options = {});

    // The signing request version.
    int version = 2;
    // The raw signing request data.
    std::variant<RequestDataV2, RequestDataV3> data;
    // The request signature.
    std::optional<RequestSignature> signature;

    SigningRequest() : data(RequestDataV2{}) {}
    SigningRequest(int version, std::variant<RequestDataV2, RequestDataV3> data,
                   const SigningRequestEncodingOptions& options = {},
                   std::optional<RequestSignature> signature = std::nullopt);

    // Sign the request, mutating.
    Result<void> sign(SignatureProvider& signatureProvider);
    // The signature digest for this request.
    Checksum256 getSignatureDigest() const;
    // Set the signature data for this request, mutating.
    Result<void> setSignature(std::string_view signer, std::string_view signature);
    // Set the request callback, mutating.
    void setCallback(const std::string& url, bool background);
    // Set broadcast flag.
    void setBroadcast(bool broadcast);

    // Encode this request into an esr: uri.
    std::string encode(std::optional<bool> compress = std::nullopt, bool slashes = true,
                       std::string scheme = "esr:") const;
    // The request data without header or signature.
    Bytes getData() const;
    // Signature data; empty if the request is not signed.
    Bytes getSignatureData() const;

    // ABI definitions required to resolve request.
    std::vector<Name> getRequiredAbis() const;
    // Whether TaPoS values are required to resolve request.
    bool requiresTapos() const;
    // Resolve required ABI definitions.
    Result<AbiMap> fetchAbis(AbiProvider* abiProvider = nullptr) const;

    // Decode raw actions to object representations, resolving placeholders.
    Result<std::vector<ResolvedAction>> resolveActions(
        const AbiMap& abis,
        const std::optional<PermissionLevel>& signer = std::nullopt) const;
    Result<ResolvedTransaction> resolveTransaction(const AbiMap& abis,
                                                   const PermissionLevel& signer,
                                                   const TransactionContext& ctx = {}) const;
    Result<ResolvedSigningRequest> resolve(const AbiMap& abis, const PermissionLevel& signer,
                                           const TransactionContext& ctx = {}) const;

    // The id of the chain where this request is valid.
    Result<ChainId> getChainId() const;
    // Chain IDs this request is valid for; nullopt unless multi-chain with a
    // constrained list.
    Result<std::optional<std::vector<ChainId>>> getChainIds() const;
    // Set chain IDs this request is valid for (multi-chain requests).
    Result<void> setChainIds(const std::vector<ChainId>& ids);
    // True when the chain id is alias 0, i.e. valid for any chain.
    bool isMultiChain() const;

    // The actions in this request with action data encoded.
    Result<std::vector<Action>> getRawActions() const;
    // Unresolved transaction.
    Result<Transaction> getRawTransaction() const;

    bool isIdentity() const;
    bool shouldBroadcast() const;
    // Requested identity account/permission/scope; nullopt when unset.
    std::optional<Name> getIdentity() const;
    std::optional<Name> getIdentityPermission() const;
    std::optional<Name> getIdentityScope() const;

    // Raw info dict.
    std::map<std::string, Bytes> getInfo() const;
    std::optional<Bytes> getRawInfoKey(const std::string& key) const;
    void setRawInfoKey(const std::string& key, const Bytes& value);
    // Strings encode as raw utf8 (matching upstream); other values use the
    // typed or dynamic serializer.
    void setInfoKey(const std::string& key, std::string_view value);
    void setInfoKey(const std::string& key, bool value);
    template <class T>
    Result<void> setInfoKey(const std::string& key, const T& object) {
        DK_TRY(data_, Serializer::encode(object));
        setRawInfoKey(key, data_);
        return {};
    }
    // Utf8 string value of an info key.
    std::optional<std::string> getInfoKey(const std::string& key) const;
    template <class T>
    Result<T> getInfoKey(const std::string& key) const {
        const auto raw = getRawInfoKey(key);
        if (!raw) {
            return err(ErrorKind::NotFound, "No info value for key " + key);
        }
        return Serializer::decode<T>(*raw);
    }
    Result<json> getInfoKey(const std::string& key, std::string_view type) const;

    // Deep copy (value semantics make this trivial).
    SigningRequest clone() const { return *this; }

    std::string toString() const { return encode(); }
    json toJSON() const { return encode(); }

    // equality of the raw data (used by tests)
    bool dataEquals(const SigningRequest& other) const;

private:
    friend class ResolvedSigningRequest;
    static ABI identityAbi(int version);

    bool zlib_ = true;
    AbiProvider* abiProvider_ = nullptr;
};

class ResolvedSigningRequest {
public:
    // Recreate a resolved request from a callback payload.
    static Result<ResolvedSigningRequest> fromPayload(
        const json& payload, const SigningRequestEncodingOptions& options = {});

    ResolvedSigningRequest(SigningRequest request, PermissionLevel signer, Transaction transaction,
                           ResolvedTransaction resolvedTransaction, ChainId chainId);

    // The request that created the transaction.
    SigningRequest request;
    // Expected signer of transaction.
    PermissionLevel signer;
    // Transaction object with action data encoded.
    Transaction transaction;
    // Transaction object with action data decoded.
    ResolvedTransaction resolvedTransaction;
    // Id of chain where the request was resolved.
    ChainId chainId;

    Bytes serializedTransaction() const;
    Checksum256 signingDigest() const { return transaction.signingDigest(chainId); }
    Bytes signingData() const { return transaction.signingData(chainId); }

    Result<std::optional<ResolvedCallback>> getCallback(
        const std::vector<Signature>& signatures,
        std::optional<uint32_t> blockNum = std::nullopt) const;

    Result<struct IdentityProof> getIdentityProof(const Signature& signature) const;
};

}  // namespace dwarfkit
