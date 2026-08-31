#include "core/clipboard.hpp"

namespace tb {

namespace {
std::function<void(const std::string&)> g_set;
std::function<std::string()> g_get;
}  // namespace

void installClipboard(std::function<void(const std::string&)> setter,
                      std::function<std::string()> getter) {
    g_set = std::move(setter);
    g_get = std::move(getter);
}

void clipboardSet(const std::string& text) {
    if (g_set) g_set(text);
}

std::string clipboardGet() { return g_get ? g_get() : std::string(); }

}  // namespace tb
