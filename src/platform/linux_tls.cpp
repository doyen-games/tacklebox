// Linux only: every libcurl handle the process creates gets a CA bundle that
// exists on *this* machine (see core/ca_bundle.hpp). dwarfkit's transport
// owns the handles and offers no hook, and the vendored code is not patched,
// so the link step routes curl_easy_init through here instead
// (-Wl,--wrap=curl_easy_init in CMakeLists.txt). Nothing else changes: when
// libcurl's own compiled-in bundle exists we leave the handle alone.
#include <curl/curl.h>

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

#include "core/ca_bundle.hpp"
#include "core/log.hpp"

extern "C" CURL* __real_curl_easy_init();

namespace {

// Resolved once: empty when libcurl's default is fine.
const std::string& overrideBundle() {
    static std::string bundle;
    static std::once_flag once;
    std::call_once(once, [] {
        const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
        std::error_code ec;
        if (info && info->cainfo && std::filesystem::is_regular_file(info->cainfo, ec)) return;
        if (auto found = tb::firstExistingFile(tb::caBundleCandidates())) {
            bundle = *found;
            tb::Log::info("tls: libcurl's CA bundle (%s) is missing here; using %s",
                          info && info->cainfo ? info->cainfo : "unset", bundle.c_str());
        } else {
            tb::Log::warn("tls: no CA bundle found on this system; https will fail");
        }
    });
    return bundle;
}

}  // namespace

extern "C" CURL* __wrap_curl_easy_init() {
    CURL* handle = __real_curl_easy_init();
    if (handle) {
        const std::string& bundle = overrideBundle();
        if (!bundle.empty()) curl_easy_setopt(handle, CURLOPT_CAINFO, bundle.c_str());
    }
    return handle;
}
