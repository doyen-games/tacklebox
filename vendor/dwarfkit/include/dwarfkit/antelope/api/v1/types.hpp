// Port of antelope src/api/v1/types.ts. Types live under dwarfkit::api::v1
// (BLUEPRINT.md section 4). TS interfaces that were never decoded through the
// serializer (PushTransactionResponse, SendTransactionResponse, the params
// bags) stay json on the wire.
#pragma once

#include <map>

#include <dwarfkit/antelope/chain/authority.hpp>
#include <dwarfkit/antelope/chain/transaction.hpp>
#include <dwarfkit/antelope/serializer.hpp>

namespace dwarfkit::api::v1 {

struct AccountLinkedAction {
    DK_STRUCT("account_linked_action")
    Name account;
    std::optional<Name> action;
    DK_FIELDS(account, action)
};

struct AccountPermission {
    DK_STRUCT("account_permission")
    Name perm_name;
    Name parent;
    Authority required_auth;
    std::optional<std::vector<AccountLinkedAction>> linked_actions;
    DK_FIELDS(perm_name, parent, required_auth, linked_actions)
};

struct AccountResourceLimit {
    DK_STRUCT("account_resource_limit")
    int64_t used = 0;
    int64_t available = 0;
    int64_t max = 0;
    std::optional<TimePoint> last_usage_update_time;
    std::optional<int64_t> current_used;
    DK_FIELDS(used, available, max, last_usage_update_time, current_used)
};

struct AccountTotalResources {
    DK_STRUCT("account_total_resources")
    Name owner;
    Asset net_weight;
    Asset cpu_weight;
    uint64_t ram_bytes = 0;
    DK_FIELDS(owner, net_weight, cpu_weight, ram_bytes)
};

struct AccountSelfDelegatedBandwidth {
    DK_STRUCT("account_self_delegated_bandwidth")
    Name from;
    Name to;
    Asset net_weight;
    Asset cpu_weight;
    DK_FIELDS(from, to, net_weight, cpu_weight)
};

struct AccountRefundRequest {
    DK_STRUCT("account_refund_request")
    Name owner;
    TimePoint request_time;
    Asset net_amount;
    Asset cpu_amount;
    DK_FIELDS(owner, request_time, net_amount, cpu_amount)
};

struct AccountVoterInfo {
    DK_STRUCT("account_voter_info")
    Name owner;
    Name proxy;
    std::vector<Name> producers;
    std::optional<int64_t> staked;
    double last_vote_weight = 0;
    double proxied_vote_weight = 0;
    bool is_proxy = false;
    std::optional<uint32_t> flags1;
    uint32_t reserved2 = 0;
    std::string reserved3;
    DK_FIELDS(owner, proxy, producers, staked, last_vote_weight, proxied_vote_weight, is_proxy,
              flags1, reserved2, reserved3)
};

struct AccountRexInfoMaturities {
    DK_STRUCT("account_rex_info_maturities")
    // Expected results from after EOSIO.Contracts v1.9.0
    std::optional<TimePoint> key;
    std::optional<int64_t> value;
    // Expected results from before EOSIO.Contracts v1.9.0
    std::optional<TimePoint> first;
    std::optional<int64_t> second;
    DK_FIELDS(key, value, first, second)
};

struct AccountRexInfo {
    DK_STRUCT("account_rex_info")
    uint32_t version = 0;
    Name owner;
    Asset vote_stake;
    Asset rex_balance;
    int64_t matured_rex = 0;
    std::vector<AccountRexInfoMaturities> rex_maturities;
    DK_FIELDS(version, owner, vote_stake, rex_balance, matured_rex, rex_maturities)
};

struct GetRawAbiResponse {
    DK_STRUCT("get_raw_abi_response")
    Name account_name;
    Checksum256 code_hash;
    Checksum256 abi_hash;
    Blob abi;
    DK_FIELDS(account_name, code_hash, abi_hash, abi)
};

// Templated on the voter info type: chains like Telos and WAX extend
// voter_info (upstream re-declares the field on a subclass).
template <class VoterInfo>
struct BasicAccountObject {
    DK_STRUCT("account_object")
    // The account name of the retrieved account
    Name account_name;
    // Highest block number on the chain
    uint32_t head_block_num = 0;
    // Highest block unix timestamp.
    TimePoint head_block_time;
    // Indicator of if this is a privileged system account
    bool privileged = false;
    // Last update to accounts contract as unix timestamp.
    TimePoint last_code_update;
    // Account created as unix timestamp.
    TimePoint created;
    // Account core token balance
    std::optional<Asset> core_liquid_balance;
    int64_t ram_quota = 0;
    int64_t net_weight = 0;
    int64_t cpu_weight = 0;
    AccountResourceLimit net_limit;
    AccountResourceLimit cpu_limit;
    std::optional<AccountResourceLimit> subjective_cpu_bill_limit;
    uint64_t ram_usage = 0;
    std::vector<AccountPermission> permissions;
    std::optional<AccountTotalResources> total_resources;
    std::optional<AccountSelfDelegatedBandwidth> self_delegated_bandwidth;
    std::optional<AccountRefundRequest> refund_request;
    std::optional<VoterInfo> voter_info;
    std::optional<AccountRexInfo> rex_info;
    DK_FIELDS(account_name, head_block_num, head_block_time, privileged, last_code_update, created,
              core_liquid_balance, ram_quota, net_weight, cpu_weight, net_limit, cpu_limit,
              subjective_cpu_bill_limit, ram_usage, permissions, total_resources,
              self_delegated_bandwidth, refund_request, voter_info, rex_info)

    Result<AccountPermission> getPermission(const Name& permission) const {
        for (const auto& entry : permissions) {
            if (entry.perm_name == permission) {
                return entry;
            }
        }
        return err(ErrorKind::NotFound, "Unknown permission " + permission.toString() +
                                            " on account " + account_name.toString() + ".");
    }
    Result<AccountPermission> getPermission(std::string_view permission) const {
        return getPermission(Name::from(permission));
    }
};

using AccountObject = BasicAccountObject<AccountVoterInfo>;

struct AccountByAuthorizersRow {
    DK_STRUCT("account_by_authorizers_row")
    Name account_name;
    Name permission_name;
    std::optional<PublicKey> authorizing_key;
    std::optional<PermissionLevel> authorizing_account;
    Weight weight;
    uint32_t threshold = 0;
    DK_FIELDS(account_name, permission_name, authorizing_key, authorizing_account, weight,
              threshold)
};

struct AccountsByAuthorizers {
    DK_STRUCT("account_by_authorizers")
    std::vector<AccountByAuthorizersRow> accounts;
    DK_FIELDS(accounts)
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

// fc "mutable variant" returned by get_block api
struct TrxVariant {
    static constexpr std::string_view abiName = "trx_variant";

    Checksum256 id;
    json extra;

    static Result<TrxVariant> from(const json& data);

    Result<std::optional<Transaction>> transaction() const;
    Result<std::optional<std::vector<Signature>>> signatures() const;

    bool equals(const TrxVariant& other) const { return id == other.id; }
    json toJSON() const { return id.toJSON(); }
};

struct GetBlockResponseTransactionReceipt : TransactionReceipt {
    DK_STRUCT_BASE("get_block_response_receipt", TransactionReceipt)
    TrxVariant trx;
    DK_FIELDS(trx)

    const Checksum256& id() const { return trx.id; }
};

struct GetBlockResponse {
    DK_STRUCT("get_block_response")
    TimePoint timestamp;
    Name producer;
    uint16_t confirmed = 0;
    BlockId previous;
    Checksum256 transaction_mroot;
    Checksum256 action_mroot;
    uint32_t schedule_version = 0;
    std::optional<NewProducers> new_producers;
    std::optional<json> header_extensions;
    std::optional<json> new_protocol_features;
    Signature producer_signature;
    std::vector<GetBlockResponseTransactionReceipt> transactions;
    std::optional<json> block_extensions;
    BlockId id;
    uint32_t block_num = 0;
    uint32_t ref_block_prefix = 0;
    DK_FIELDS(timestamp, producer, confirmed, previous, transaction_mroot, action_mroot,
              schedule_version, new_producers, header_extensions, new_protocol_features,
              producer_signature, transactions, block_extensions, id, block_num, ref_block_prefix)
};

struct GetBlockInfoResponse {
    DK_STRUCT("get_block_info_response")
    uint32_t block_num = 0;
    uint16_t ref_block_num = 0;
    BlockId id;
    TimePoint timestamp;
    Name producer;
    uint16_t confirmed = 0;
    BlockId previous;
    Checksum256 transaction_mroot;
    Checksum256 action_mroot;
    uint32_t schedule_version = 0;
    Signature producer_signature;
    uint32_t ref_block_prefix = 0;
    DK_FIELDS(block_num, ref_block_num, id, timestamp, producer, confirmed, previous,
              transaction_mroot, action_mroot, schedule_version, producer_signature,
              ref_block_prefix)
};

struct BlockStateHeader {
    DK_STRUCT("block_state_header")
    TimePoint timestamp;
    Name producer;
    uint16_t confirmed = 0;
    BlockId previous;
    Checksum256 transaction_mroot;
    Checksum256 action_mroot;
    uint32_t schedule_version = 0;
    std::optional<json> header_extensions;
    Signature producer_signature;
    DK_FIELDS(timestamp, producer, confirmed, previous, transaction_mroot, action_mroot,
              schedule_version, header_extensions, producer_signature)
};

struct GetBlockHeaderStateResponse {
    DK_STRUCT("get_block_header_state_response")
    uint32_t block_num = 0;
    uint32_t dpos_proposed_irreversible_blocknum = 0;
    uint32_t dpos_irreversible_blocknum = 0;
    BlockId id;
    BlockStateHeader header;
    // Unstructured any fields specific to header state calls
    json active_schedule;
    json blockroot_merkle;
    json producer_to_last_produced;
    json producer_to_last_implied_irb;
    json valid_block_signing_authority;
    json confirm_count;
    json pending_schedule;
    std::optional<json> activated_protocol_features;
    std::optional<json> additional_signatures;
    DK_FIELDS(block_num, dpos_proposed_irreversible_blocknum, dpos_irreversible_blocknum, id,
              header, active_schedule, blockroot_merkle, producer_to_last_produced,
              producer_to_last_implied_irb, valid_block_signing_authority, confirm_count,
              pending_schedule, activated_protocol_features, additional_signatures)
};

struct GetInfoResponse {
    DK_STRUCT("get_info_response")
    // Hash representing the last commit in the tagged release.
    std::string server_version;
    // Hash representing the ID of the chain.
    Checksum256 chain_id;
    // Highest block number on the chain
    uint32_t head_block_num = 0;
    // Highest block number on the chain that has been irreversibly applied to state.
    uint32_t last_irreversible_block_num = 0;
    // Highest block ID on the chain that has been irreversibly applied to state.
    BlockId last_irreversible_block_id;
    // Highest block ID on the chain.
    BlockId head_block_id;
    // Highest block unix timestamp.
    TimePoint head_block_time;
    // Producer that signed the highest block (head block).
    Name head_block_producer;
    // CPU limit calculated after each block is produced, approximately 1000 times blockCpuLimit.
    uint64_t virtual_block_cpu_limit = 0;
    // NET limit calculated after each block is produced, approximately 1000 times blockNetLimit.
    uint64_t virtual_block_net_limit = 0;
    // Actual maximum CPU limit.
    uint64_t block_cpu_limit = 0;
    // Actual maximum NET limit.
    uint64_t block_net_limit = 0;
    // String representation of server version.
    std::optional<std::string> server_version_string;
    // Sequential block number representing the best known head in the fork database tree.
    std::optional<uint32_t> fork_db_head_block_num;
    // Hash representing the best known head in the fork database tree.
    std::optional<BlockId> fork_db_head_block_id;
    DK_FIELDS(server_version, chain_id, head_block_num, last_irreversible_block_num,
              last_irreversible_block_id, head_block_id, head_block_time, head_block_producer,
              virtual_block_cpu_limit, virtual_block_net_limit, block_cpu_limit, block_net_limit,
              server_version_string, fork_db_head_block_num, fork_db_head_block_id)

    TransactionHeader getTransactionHeader(uint32_t secondsAhead = 120) const;
};

struct GetTableByScopeResponseRow {
    DK_STRUCT("get_table_by_scope_response_row")
    Name code;
    Name scope;
    Name table;
    Name payer;
    uint32_t count = 0;
    DK_FIELDS(code, scope, table, payer, count)
};

struct GetTableByScopeResponse {
    DK_STRUCT("get_table_by_scope_response")
    std::vector<GetTableByScopeResponseRow> rows;
    std::string more;
    DK_FIELDS(rows, more)
};

struct OrderedActionsResult {
    DK_STRUCT("ordered_action_result")
    uint64_t global_action_seq = 0;
    int64_t account_action_seq = 0;
    uint32_t block_num = 0;
    BlockTimestamp block_time;
    std::optional<json> action_trace;
    std::optional<bool> irrevirsible;
    DK_FIELDS(global_action_seq, account_action_seq, block_num, block_time, action_trace,
              irrevirsible)
};

struct GetActionsResponse {
    DK_STRUCT("get_actions_response")
    std::vector<OrderedActionsResult> actions;
    int32_t last_irreversible_block = 0;
    int32_t head_block_num = 0;
    std::optional<bool> time_limit_exceeded_error;
    DK_FIELDS(actions, last_irreversible_block, head_block_num, time_limit_exceeded_error)
};

struct GetTransactionResponse {
    DK_STRUCT("get_transaction_response")
    Checksum256 id;
    uint32_t block_num = 0;
    BlockTimestamp block_time;
    uint32_t last_irreversible_block = 0;
    std::optional<json> traces;
    json trx;
    DK_FIELDS(id, block_num, block_time, last_irreversible_block, traces, trx)
};

struct GetKeyAccountsResponse {
    DK_STRUCT("get_key_accounts_response")
    std::vector<Name> account_names;
    DK_FIELDS(account_names)
};

struct GetCodeResponse {
    DK_STRUCT("get_code_response")
    ABI abi;
    Name account_name;
    Checksum256 code_hash;
    std::string wast;
    std::string wasm;
    DK_FIELDS(abi, account_name, code_hash, wast, wasm)
};

struct GetControlledAccountsResponse {
    DK_STRUCT("get_controlled_accounts_response")
    std::vector<Name> controlled_accounts;
    DK_FIELDS(controlled_accounts)
};

struct GetCurrencyStatsItemResponse {
    DK_STRUCT("get_currency_stats_item_response")
    Asset supply;
    Asset max_supply;
    Name issuer;
    DK_FIELDS(supply, max_supply, issuer)
};

using GetCurrencyStatsResponse = std::map<std::string, GetCurrencyStatsItemResponse>;

struct GetTransactionStatusResponse {
    DK_STRUCT("get_transaction_status_response")
    std::string state;
    uint32_t head_number = 0;
    BlockId head_id;
    TimePoint head_timestamp;
    uint32_t irreversible_number = 0;
    BlockId irreversible_id;
    TimePoint irreversible_timestamp;
    BlockId earliest_tracked_block_id;
    uint32_t earliest_tracked_block_number = 0;
    DK_FIELDS(state, head_number, head_id, head_timestamp, irreversible_number, irreversible_id,
              irreversible_timestamp, earliest_tracked_block_id, earliest_tracked_block_number)
};

struct ProducerAuthority {
    DK_STRUCT("producer_authority")
    uint32_t threshold = 0;
    std::vector<KeyWeight> keys;
    DK_FIELDS(threshold, keys)
};

// authority on the wire is [number, ProducerAuthority]
struct Producer {
    DK_STRUCT("producer")
    Name producer_name;
    json authority;
    DK_FIELDS(producer_name, authority)

    Result<ProducerAuthority> producerAuthority() const;
};

struct ProducerSchedule {
    DK_STRUCT("producer_schedule")
    uint32_t version = 0;
    std::vector<Producer> producers;
    DK_FIELDS(version, producers)
};

struct GetProducerScheduleResponse {
    DK_STRUCT("get_producer_schedule_response")
    std::optional<ProducerSchedule> active;
    std::optional<ProducerSchedule> pending;
    std::optional<ProducerSchedule> proposed;
    DK_FIELDS(active, pending, proposed)
};

struct ProtocolFeature {
    DK_STRUCT("protocol_feature")
    Checksum256 feature_digest;
    uint32_t activation_ordinal = 0;
    uint32_t activation_block_num = 0;
    Checksum256 description_digest;
    std::vector<std::string> dependencies;
    std::string protocol_feature_type;
    std::vector<json> specification;
    DK_FIELDS(feature_digest, activation_ordinal, activation_block_num, description_digest,
              dependencies, protocol_feature_type, specification)
};

struct GetProtocolFeaturesResponse {
    DK_STRUCT("get_protocol_features_response")
    std::vector<ProtocolFeature> activated_protocol_features;
    std::optional<uint32_t> more;
    DK_FIELDS(activated_protocol_features, more)
};

}  // namespace dwarfkit::api::v1

namespace dwarfkit {

template <>
struct abi_traits<api::v1::TrxVariant, void> {
    static constexpr std::string_view abiName = "trx_variant";
    static Result<void> toABI(const api::v1::TrxVariant&, ABIEncoder&) {
        return err(ErrorKind::Unsupported, "trx_variant has no binary form");
    }
    static Result<api::v1::TrxVariant> fromABI(ABIDecoder&) {
        return err(ErrorKind::Unsupported, "trx_variant has no binary form");
    }
    static json toJSON(const api::v1::TrxVariant& v) { return v.toJSON(); }
    static Result<api::v1::TrxVariant> fromJSON(const json& j) {
        return api::v1::TrxVariant::from(j);
    }
    static api::v1::TrxVariant abiDefault() { return {}; }
};

}  // namespace dwarfkit
