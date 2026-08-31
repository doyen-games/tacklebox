// Port of session src/storage.ts. BrowserLocalStorage becomes
// FileSessionStorage and MemorySessionStorage (BLUEPRINT.md 2).
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

// Interface storage adapters implement (BLUEPRINT.md 5.3). Storage adapters
// persist Sessions and can be passed to the SessionKit constructor to
// auto-persist sessions.
struct SessionStorage {
    // Write string to storage at key, overwriting existing values.
    virtual Result<void> write(std::string_view key, std::string_view data) = 0;
    // Read key from storage; nullopt if the key can not be found.
    virtual Result<std::optional<std::string>> read(std::string_view key) = 0;
    // Delete key from storage; not an error for a non-existing key.
    virtual Result<void> remove(std::string_view key) = 0;
    virtual ~SessionStorage() = default;
};

// Stores each key as a file named wharf-{keyPrefix}-{key} in a directory.
class FileSessionStorage final : public SessionStorage {
public:
    explicit FileSessionStorage(std::filesystem::path directory, std::string keyPrefix = "");

    Result<void> write(std::string_view key, std::string_view data) override;
    Result<std::optional<std::string>> read(std::string_view key) override;
    Result<void> remove(std::string_view key) override;

    std::string storageKey(std::string_view key) const;

private:
    std::filesystem::path directory_;
    std::string keyPrefix_;
};

// In-memory storage, mainly for tests and ephemeral sessions.
class MemorySessionStorage final : public SessionStorage {
public:
    Result<void> write(std::string_view key, std::string_view data) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        data_[std::string(key)] = std::string(data);
        return {};
    }
    Result<std::optional<std::string>> read(std::string_view key) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto found = data_.find(std::string(key));
        if (found == data_.end()) {
            return std::optional<std::string>{};
        }
        return std::optional(found->second);
    }
    Result<void> remove(std::string_view key) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        data_.erase(std::string(key));
        return {};
    }

private:
    std::mutex mutex_;
    std::map<std::string, std::string> data_;
};

}  // namespace dwarfkit
