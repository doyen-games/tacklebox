// Clipboard indirection: the platform shell (SDL on every OS) installs the
// real functions at startup; core code stays free of windowing dependencies
// and tests run without any.
#pragma once

#include <functional>
#include <string>

namespace tb {

void installClipboard(std::function<void(const std::string&)> setter,
                      std::function<std::string()> getter);

// Safe no-ops when no shell installed them (tests).
void clipboardSet(const std::string& text);
std::string clipboardGet();

}  // namespace tb
