// Update check against the project's GitHub Releases feed. Notify-only by
// design: a wallet must never download or execute code on its own, so the
// check compares versions and hands the user the release page URL.
#pragma once

#include <optional>
#include <string>

#include <dwarfkit/core/json.hpp>

namespace tb::update {

using dwarfkit::json;

// Where releases live; the checker calls
//   https://api.github.com/repos/<kRepo>/releases/latest
inline constexpr const char* kRepo = "doyen-games/tacklebox";

struct ReleaseInfo {
    std::string tag;       // "v0.3.0"
    std::string url;       // human release page (html_url)
    std::string notes;     // release body, truncated for display
    std::string published; // ISO date
};

// Semver-ish compare of "v1.2.3" / "1.2.3-rc1" style tags: negative when a
// is older than b, 0 when equal, positive when a is newer. Prereleases sort
// below their release ("1.2.3-rc1" < "1.2.3").
int compareVersions(const std::string& a, const std::string& b);

// Parse the /releases/latest response; nullopt when the shape is wrong or
// the release is a draft.
std::optional<ReleaseInfo> parseLatestRelease(const json& body);

}  // namespace tb::update
