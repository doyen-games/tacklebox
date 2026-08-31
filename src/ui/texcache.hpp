// Remote image pipeline for NFT media: worker fetch through the chain
// service's transport, stb_image decode, GL texture upload on the UI thread.
// LRU-bounded; failures cached so a dead URL is fetched once.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tb {
class ChainService;
class TaskRunner;
}  // namespace tb

namespace tb::ui {

struct TexEntry {
    enum class State { Loading, Ready, Failed };
    State state = State::Loading;
    unsigned glId = 0;  // valid when Ready
    int width = 0;
    int height = 0;
    int64_t lastUsedMs = 0;
};

class TextureCache {
public:
    // Query-or-start-load. UI thread only. Returns the entry (never null);
    // render a placeholder while state == Loading.
    const TexEntry* get(const std::string& url, ChainService& service, TaskRunner& runner);

    // Drop textures unused for a while / beyond the cap. Call once per frame.
    void evict();

    // Free everything (GL context still current).
    void clear();

private:
    struct Pending {
        std::string url;
        std::vector<uint8_t> rgba;
        int width = 0, height = 0;
        bool ok = false;
    };
    void applyDecoded(Pending decoded);

    std::map<std::string, TexEntry> entries_;
    static constexpr size_t kMaxTextures = 96;
    static constexpr size_t kMaxFetchBytes = 6 * 1024 * 1024;
};

TextureCache& textures();

}  // namespace tb::ui
