// Port of antelope src/chain/authority.ts
#pragma once

#include <dwarfkit/antelope/chain/permission_level.hpp>
#include <dwarfkit/antelope/chain/public_key.hpp>
#include <dwarfkit/antelope/serializer/serializable.hpp>

namespace dwarfkit {

DK_TYPE_ALIAS(Weight, "weight_type", uint16_t)

struct KeyWeight {
    DK_STRUCT("key_weight")
    PublicKey key;
    Weight weight;
    DK_FIELDS(key, weight)
};

struct PermissionLevelWeight {
    DK_STRUCT("permission_level_weight")
    PermissionLevel permission;
    Weight weight;
    DK_FIELDS(permission, weight)
};

struct WaitWeight {
    DK_STRUCT("wait_weight")
    uint32_t wait_sec = 0;
    Weight weight;
    DK_FIELDS(wait_sec, weight)
};

struct Authority {
    DK_STRUCT("authority")
    uint32_t threshold = 0;
    std::vector<KeyWeight> keys;
    std::vector<PermissionLevelWeight> accounts;
    std::vector<WaitWeight> waits;
    DK_FIELDS(threshold, keys, accounts, waits)

    // Builds from json (missing keys/accounts/waits default empty) and sorts.
    static Result<Authority> from(const json& value);
    static Authority from(Authority value) {
        return value;
    }

    // Total weight of all waits.
    uint32_t waitThreshold() const;

    // Weight a key needs to sign for this authority.
    uint32_t keyThreshold() const { return threshold - waitThreshold(); }

    // The weight for given public key, or zero if it is not included.
    uint16_t keyWeight(const PublicKey& publicKey) const;

    // Check if given public key has permission in this authority. Does not take
    // indirect permissions for the key via account weights into account.
    // includePartial considers auths where the key is included but can't be
    // reached alone (e.g. multisig).
    bool hasPermission(const PublicKey& publicKey, bool includePartial = false) const;
    Result<bool> hasPermission(std::string_view publicKey, bool includePartial = false) const;

    // Sorts the authority weights in place; should be called before including
    // the authority in an updateauth action or it might be rejected.
    void sort();
};

}  // namespace dwarfkit
