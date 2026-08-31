#include <dwarfkit/session/storage.hpp>

#include <fstream>
#include <sstream>

namespace dwarfkit {

FileSessionStorage::FileSessionStorage(std::filesystem::path directory, std::string keyPrefix)
    : directory_(std::move(directory)), keyPrefix_(std::move(keyPrefix)) {}

std::string FileSessionStorage::storageKey(std::string_view key) const {
    return "wharf-" + keyPrefix_ + "-" + std::string(key);
}

Result<void> FileSessionStorage::write(std::string_view key, std::string_view data) {
    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);
    std::ofstream file(directory_ / storageKey(key), std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        return err(ErrorKind::Storage, "Unable to open storage file for " + std::string(key));
    }
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!file.good()) {
        return err(ErrorKind::Storage, "Unable to write storage file for " + std::string(key));
    }
    return {};
}

Result<std::optional<std::string>> FileSessionStorage::read(std::string_view key) {
    std::ifstream file(directory_ / storageKey(key), std::ios::binary);
    if (!file.good()) {
        return std::optional<std::string>{};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return std::optional(buffer.str());
}

Result<void> FileSessionStorage::remove(std::string_view key) {
    std::error_code ec;
    std::filesystem::remove(directory_ / storageKey(key), ec);
    return {};
}

}  // namespace dwarfkit
