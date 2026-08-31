#include <dwarfkit/antelope/chain/authority.hpp>

#include <algorithm>

#include <dwarfkit/antelope/serializer/traits.hpp>

namespace dwarfkit {

Result<Authority> Authority::from(const json& value) {
    json merged = {{"keys", json::array()},
                   {"accounts", json::array()},
                   {"waits", json::array()}};
    merged.update(value);
    DK_TRY(rv, structFrom<Authority>(merged));
    rv.sort();
    return rv;
}

uint32_t Authority::waitThreshold() const {
    uint32_t total = 0;
    for (const auto& wait : waits) {
        total += wait.weight.value;
    }
    return total;
}

uint16_t Authority::keyWeight(const PublicKey& publicKey) const {
    for (const auto& kw : keys) {
        if (kw.key.equals(publicKey)) {
            return kw.weight.value;
        }
    }
    return 0;
}

bool Authority::hasPermission(const PublicKey& publicKey, bool includePartial) const {
    const uint32_t needed = includePartial ? 1 : keyThreshold();
    return keyWeight(publicKey) >= needed;
}

Result<bool> Authority::hasPermission(std::string_view publicKey, bool includePartial) const {
    DK_TRY(key, PublicKey::from(publicKey));
    return hasPermission(key, includePartial);
}

void Authority::sort() {
    std::stable_sort(keys.begin(), keys.end(),
                     [](const KeyWeight& a, const KeyWeight& b) { return a.key.compare(b.key) < 0; });
    std::stable_sort(accounts.begin(), accounts.end(),
                     [](const PermissionLevelWeight& a, const PermissionLevelWeight& b) {
                         return a.permission.compare(b.permission) < 0;
                     });
    std::stable_sort(waits.begin(), waits.end(),
                     [](const WaitWeight& a, const WaitWeight& b) { return a.wait_sec < b.wait_sec; });
}

}  // namespace dwarfkit
