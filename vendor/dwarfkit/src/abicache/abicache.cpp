#include <dwarfkit/abicache.hpp>

namespace dwarfkit {

namespace {

template <class T, class Field>
std::vector<T> mergeAndDeduplicate(const std::vector<T>& array1, const std::vector<T>& array2,
                                   Field field) {
    std::vector<T> rv = array1;
    for (const auto& current : array2) {
        const bool exists = std::any_of(rv.begin(), rv.end(), [&](const T& entry) {
            return field(entry) == field(current);
        });
        if (!exists) {
            rv.push_back(current);
        }
    }
    return rv;
}

}  // namespace

Result<ABI> ABICache::getAbi(const Name& account) {
    const std::string key = account.toString();
    {
        std::lock_guard lock(mutex_);
        const auto record = cache_.find(key);
        if (record != cache_.end() && !partial_.contains(key)) {
            return record->second;
        }
    }
    auto response = client->v1.chain.get_raw_abi(account);
    if (!response) {
        return err(std::move(response.error()));
    }
    std::lock_guard lock(mutex_);
    const auto record = cache_.find(key);
    if (!response->abi.array.empty()) {
        DK_TRY(chainAbi, ABI::from(response->abi));
        const ABI resolved = record != cache_.end()
                                 ? merge(chainAbi, record->second, chainAbi.version)
                                 : chainAbi;
        cache_[key] = resolved;
        partial_.erase(key);
        return resolved;
    }
    if (record != cache_.end()) {
        return record->second;
    }
    return err(ErrorKind::NotFound, "ABI for " + key + " could not be loaded.");
}

void ABICache::setAbi(const Name& account, const ABI& abi, bool merge_) {
    const std::string key = account.toString();
    std::lock_guard lock(mutex_);
    const auto existing = cache_.find(key);
    if (merge_ && existing != cache_.end()) {
        cache_[key] = merge(existing->second, abi, abi.version);
    } else {
        cache_[key] = abi;
        if (merge_) {
            partial_.insert(key);
        } else {
            partial_.erase(key);
        }
    }
}

size_t ABICache::cacheSize() const {
    std::lock_guard lock(mutex_);
    return cache_.size();
}

bool ABICache::cacheHas(const Name& account) const {
    std::lock_guard lock(mutex_);
    return cache_.contains(account.toString());
}

std::optional<ABI> ABICache::cacheGet(const Name& account) const {
    std::lock_guard lock(mutex_);
    const auto found = cache_.find(account.toString());
    if (found == cache_.end()) return std::nullopt;
    return found->second;
}

ABI ABICache::merge(const ABI& base, const ABI& addition, const std::string& version) {
    ABI rv;
    rv.version = version;
    rv.action_results = mergeAndDeduplicate(base.action_results, addition.action_results,
                                            [](const auto& e) { return e.name.toString(); });
    rv.types = mergeAndDeduplicate(base.types, addition.types,
                                   [](const auto& e) { return e.new_type_name; });
    rv.structs = mergeAndDeduplicate(base.structs, addition.structs,
                                     [](const auto& e) { return e.name; });
    rv.actions = mergeAndDeduplicate(base.actions, addition.actions,
                                     [](const auto& e) { return e.name.toString(); });
    rv.tables = mergeAndDeduplicate(base.tables, addition.tables,
                                    [](const auto& e) { return e.name.toString(); });
    rv.ricardian_clauses = mergeAndDeduplicate(base.ricardian_clauses, addition.ricardian_clauses,
                                               [](const auto& e) { return e.id; });
    rv.variants = mergeAndDeduplicate(base.variants, addition.variants,
                                      [](const auto& e) { return e.name; });
    return rv;
}

}  // namespace dwarfkit
