#include "app/update.hpp"

#include <array>
#include <cstdlib>

namespace tb::update {

namespace {

// "v1.2.3-rc1" -> {1,2,3} + "rc1"
void splitVersion(const std::string& text, std::array<long, 3>& nums,
                  std::string& suffix) {
    nums = {0, 0, 0};
    suffix.clear();
    size_t start = text.rfind('v', 0) == 0 || text.rfind('V', 0) == 0 ? 1 : 0;
    std::string core = text.substr(start);
    if (auto dash = core.find('-'); dash != std::string::npos) {
        suffix = core.substr(dash + 1);
        core = core.substr(0, dash);
    }
    size_t part = 0, pos = 0;
    while (part < 3 && pos <= core.size()) {
        size_t dot = core.find('.', pos);
        if (dot == std::string::npos) dot = core.size();
        nums[part++] = std::strtol(core.substr(pos, dot - pos).c_str(), nullptr, 10);
        pos = dot + 1;
    }
}

}  // namespace

int compareVersions(const std::string& a, const std::string& b) {
    std::array<long, 3> na{}, nb{};
    std::string sa, sb;
    splitVersion(a, na, sa);
    splitVersion(b, nb, sb);
    for (size_t i = 0; i < 3; ++i)
        if (na[i] != nb[i]) return na[i] < nb[i] ? -1 : 1;
    // Same triple: a release outranks any prerelease; two prereleases
    // compare by suffix text.
    if (sa.empty() && sb.empty()) return 0;
    if (sa.empty()) return 1;
    if (sb.empty()) return -1;
    return sa == sb ? 0 : (sa < sb ? -1 : 1);
}

std::optional<ReleaseInfo> parseLatestRelease(const json& body) {
    if (!body.is_object()) return std::nullopt;
    if (body.value("draft", false)) return std::nullopt;
    ReleaseInfo info;
    info.tag = body.value("tag_name", "");
    info.url = body.value("html_url", "");
    if (info.tag.empty() || info.url.empty()) return std::nullopt;
    info.published = body.value("published_at", "");
    if (info.published.size() > 10) info.published.resize(10);  // date part
    info.notes = body.value("body", "");
    if (info.notes.size() > 600) {
        info.notes.resize(600);
        info.notes += "...";
    }
    return info;
}

}  // namespace tb::update
