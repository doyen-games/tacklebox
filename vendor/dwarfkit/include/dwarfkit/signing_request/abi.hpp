// Port of signing-request src/abi.ts: the ESR ABI structs and typedefs.
#pragma once

#include <dwarfkit/antelope/chain/transaction.hpp>
#include <dwarfkit/signing_request/chain_id.hpp>

namespace dwarfkit {

struct IdentityV2 {
    DK_STRUCT("identity")
    std::optional<PermissionLevel> permission;
    DK_FIELDS(permission)
};

struct IdentityV3 {
    DK_STRUCT("identity")
    Name scope;
    std::optional<PermissionLevel> permission;
    DK_FIELDS(scope, permission)
};

DK_VARIANT(RequestVariantV2, "variant_req", Action, std::vector<Action>, Transaction, IdentityV2)
DK_VARIANT(RequestVariantV3, "variant_req", Action, std::vector<Action>, Transaction, IdentityV3)

struct RequestFlags {
    using DkAliasTag = void;
    using DkAliased = uint8_t;
    static constexpr std::string_view abiName = "request_flags";

    static constexpr uint8_t broadcastFlag = 1 << 0;
    static constexpr uint8_t backgroundFlag = 1 << 1;

    uint8_t value = 0;

    constexpr RequestFlags() = default;
    constexpr RequestFlags(uint8_t value) : value(value) {}  // NOLINT(runtime/explicit)

    constexpr bool broadcast() const { return (value & broadcastFlag) != 0; }
    constexpr void setBroadcast(bool enabled) { setFlag(broadcastFlag, enabled); }
    constexpr bool background() const { return (value & backgroundFlag) != 0; }
    constexpr void setBackground(bool enabled) { setFlag(backgroundFlag, enabled); }

    constexpr bool operator==(const RequestFlags&) const = default;

private:
    constexpr void setFlag(uint8_t flag, bool enabled) {
        value = enabled ? static_cast<uint8_t>(value | flag) : static_cast<uint8_t>(value & ~flag);
    }
};

struct InfoPair {
    DK_STRUCT("info_pair")
    std::string key;
    Bytes value;
    DK_FIELDS(key, value)
};

struct RequestDataV2 {
    DK_STRUCT("signing_request")
    ChainIdVariant chain_id;
    RequestVariantV2 req;
    RequestFlags flags;
    std::string callback;
    std::vector<InfoPair> info;
    DK_FIELDS(chain_id, req, flags, callback, info)
};

struct RequestDataV3 {
    DK_STRUCT("signing_request")
    ChainIdVariant chain_id;
    RequestVariantV3 req;
    RequestFlags flags;
    std::string callback;
    std::vector<InfoPair> info;
    DK_FIELDS(chain_id, req, flags, callback, info)
};

struct RequestSignature {
    DK_STRUCT("request_signature")
    Name signer;
    Signature signature;
    DK_FIELDS(signer, signature)
};

}  // namespace dwarfkit
