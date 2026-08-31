#include <dwarfkit/account/permission.hpp>

#include <algorithm>

namespace dwarfkit {

Permission Permission::from(const api::v1::AccountPermission& permission) {
    Permission rv;
    static_cast<api::v1::AccountPermission&>(rv) = permission;
    return rv;
}

Result<Permission> Permission::from(const json& value) {
    DK_TRY(parsed, abi_traits<api::v1::AccountPermission>::fromJSON(value));
    return from(parsed);
}

Result<void> Permission::addKey(const PublicKey& key, uint16_t weight) {
    const auto exists = std::find_if(required_auth.keys.begin(), required_auth.keys.end(),
                                     [&](const KeyWeight& k) { return key.equals(k.key); });
    if (exists != required_auth.keys.end()) {
        return err(ErrorKind::Invalid, "The provided key (" + key.toString() +
                                           ") already exists on the \"" + perm_name.toString() +
                                           "\" permission.");
    }
    required_auth.keys.push_back(KeyWeight{key, weight});
    // Always sort authorities, required by antelopeio/leap
    required_auth.sort();
    return {};
}

Result<void> Permission::addKey(std::string_view key, uint16_t weight) {
    DK_TRY(parsed, PublicKey::from(key));
    return addKey(parsed, weight);
}

Result<void> Permission::removeKey(const PublicKey& key) {
    const auto it = std::find_if(required_auth.keys.begin(), required_auth.keys.end(),
                                 [&](const KeyWeight& k) { return key.equals(k.key); });
    if (it == required_auth.keys.end()) {
        return err(ErrorKind::Invalid, "The provided key (" + key.toString() +
                                           ") does not exist on the \"" + perm_name.toString() +
                                           "\" permission.");
    }
    required_auth.keys.erase(it);
    return {};
}

Result<void> Permission::removeKey(std::string_view key) {
    DK_TRY(parsed, PublicKey::from(key));
    return removeKey(parsed);
}

Result<void> Permission::addAccount(const PermissionLevel& permissionLevel, uint16_t weight) {
    const auto exists =
        std::find_if(required_auth.accounts.begin(), required_auth.accounts.end(),
                     [&](const PermissionLevelWeight& a) {
                         return permissionLevel == a.permission;
                     });
    if (exists != required_auth.accounts.end()) {
        return err(ErrorKind::Invalid, "The provided account (" + permissionLevel.toString() +
                                           ") already exists on the \"" + perm_name.toString() +
                                           "\" permission.");
    }
    required_auth.accounts.push_back(PermissionLevelWeight{permissionLevel, weight});
    // Always sort authorities, required by antelopeio/leap
    required_auth.sort();
    return {};
}

Result<void> Permission::addAccount(std::string_view permissionLevel, uint16_t weight) {
    DK_TRY(parsed, PermissionLevel::from(permissionLevel));
    return addAccount(parsed, weight);
}

Result<void> Permission::removeAccount(const PermissionLevel& permissionLevel) {
    const auto it = std::find_if(required_auth.accounts.begin(), required_auth.accounts.end(),
                                 [&](const PermissionLevelWeight& a) {
                                     return permissionLevel == a.permission;
                                 });
    if (it == required_auth.accounts.end()) {
        return err(ErrorKind::Invalid, "The provided permission (" + permissionLevel.toString() +
                                           ") does not exist on the \"" + perm_name.toString() +
                                           "\" permission.");
    }
    required_auth.accounts.erase(it);
    return {};
}

void Permission::addWait(const WaitWeight& wait) {
    required_auth.waits.push_back(wait);
    // Always sort authorities, required by antelopeio/leap
    required_auth.sort();
}

void Permission::removeWait(const WaitWeight& wait) {
    required_auth.waits.erase(std::remove_if(required_auth.waits.begin(),
                                             required_auth.waits.end(),
                                             [&](const WaitWeight& w) { return wait == w; }),
                              required_auth.waits.end());
}

}  // namespace dwarfkit
