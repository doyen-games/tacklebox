// Port of account src/permission.ts. The unused TS type aliases
// (PermissionData, ActionData, AddKeyActionParam) are dropped; LinkedAction is
// api::v1::AccountLinkedAction.
#pragma once

#include <dwarfkit/antelope/api/v1/types.hpp>

namespace dwarfkit {

class Permission : public api::v1::AccountPermission {
public:
    Permission() = default;
    static Permission from(const api::v1::AccountPermission& permission);
    // {perm_name, parent, required_auth[, linked_actions]} json form
    static Result<Permission> from(const json& value);

    const Name& name() const { return perm_name; }

    Result<void> addKey(const PublicKey& key, uint16_t weight = 1);
    Result<void> addKey(std::string_view key, uint16_t weight = 1);
    Result<void> removeKey(const PublicKey& key);
    Result<void> removeKey(std::string_view key);

    Result<void> addAccount(const PermissionLevel& permissionLevel, uint16_t weight = 1);
    // "actor@permission" form
    Result<void> addAccount(std::string_view permissionLevel, uint16_t weight = 1);
    Result<void> removeAccount(const PermissionLevel& permissionLevel);

    void addWait(const WaitWeight& wait);
    void removeWait(const WaitWeight& wait);
};

}  // namespace dwarfkit
