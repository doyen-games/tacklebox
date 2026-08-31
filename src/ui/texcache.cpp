#include "ui/texcache.hpp"

#include <algorithm>
#include <cstring>

#ifdef TB_MOBILE
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

// Windows' GL headers stop at OpenGL 1.1; this token is from 1.2.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#include <stb_image.h>

#include "chain/service.hpp"
#include "core/task_runner.hpp"
#include "core/util.hpp"

namespace tb::ui {

TextureCache& textures() {
    static TextureCache cache;
    return cache;
}

const TexEntry* TextureCache::get(const std::string& url, ChainService& service,
                                  TaskRunner& runner) {
    auto it = entries_.find(url);
    if (it != entries_.end()) {
        it->second.lastUsedMs = nowMs();
        return &it->second;
    }

    TexEntry entry;
    entry.state = TexEntry::State::Loading;
    entry.lastUsedMs = nowMs();
    entries_[url] = entry;

    // Fetch + decode off-thread, upload on main.
    ChainService* svc = &service;
    TaskRunner* run = &runner;
    run->run([this, url, svc, run] {
        Pending decoded;
        decoded.url = url;
        auto bytes = svc->fetchUrl(url, kMaxFetchBytes);
        if (bytes) {
            int w = 0, h = 0, comp = 0;
            stbi_uc* pixels = stbi_load_from_memory(bytes->data(),
                                                    static_cast<int>(bytes->size()), &w, &h,
                                                    &comp, 4);
            if (pixels) {
                // Downscale very large media by simple stride sampling: the UI
                // shows thumbnails, not wall art.
                const int maxDim = 512;
                if (w > maxDim || h > maxDim) {
                    int stride = ((w > h ? w : h) + maxDim - 1) / maxDim;
                    int nw = w / stride, nh = h / stride;
                    std::vector<uint8_t> small(static_cast<size_t>(nw) * nh * 4);
                    for (int y = 0; y < nh; ++y)
                        for (int x = 0; x < nw; ++x)
                            std::memcpy(&small[(static_cast<size_t>(y) * nw + x) * 4],
                                        &pixels[((static_cast<size_t>(y) * stride) * w +
                                                 static_cast<size_t>(x) * stride) *
                                                4],
                                        4);
                    decoded.rgba = std::move(small);
                    decoded.width = nw;
                    decoded.height = nh;
                } else {
                    decoded.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
                    decoded.width = w;
                    decoded.height = h;
                }
                decoded.ok = true;
                stbi_image_free(pixels);
            }
        }
        run->postMain([this, decoded = std::move(decoded)]() mutable {
            applyDecoded(std::move(decoded));
        });
    });
    return &entries_[url];
}

void TextureCache::applyDecoded(Pending decoded) {
    auto it = entries_.find(decoded.url);
    if (it == entries_.end()) return;
    if (!decoded.ok) {
        it->second.state = TexEntry::State::Failed;
        return;
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, decoded.width, decoded.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, decoded.rgba.data());
    it->second.glId = tex;
    it->second.width = decoded.width;
    it->second.height = decoded.height;
    it->second.state = TexEntry::State::Ready;
}

void TextureCache::evict() {
    if (entries_.size() <= kMaxTextures) return;
    // Drop the least recently used ready textures.
    std::vector<std::pair<int64_t, std::string>> byAge;
    for (const auto& [url, entry] : entries_)
        if (entry.state != TexEntry::State::Loading) byAge.push_back({entry.lastUsedMs, url});
    std::sort(byAge.begin(), byAge.end());
    size_t toDrop = entries_.size() - kMaxTextures;
    for (size_t i = 0; i < byAge.size() && i < toDrop; ++i) {
        auto it = entries_.find(byAge[i].second);
        if (it != entries_.end()) {
            if (it->second.glId) glDeleteTextures(1, &it->second.glId);
            entries_.erase(it);
        }
    }
}

void TextureCache::clear() {
    for (auto& [url, entry] : entries_)
        if (entry.glId) glDeleteTextures(1, &entry.glId);
    entries_.clear();
}

}  // namespace tb::ui
