#include <dwarfkit/plugins/wallet/anchor/chains.hpp>

#include <algorithm>
#include <cctype>

namespace dwarfkit::anchor {

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::optional<std::string> findUrl(const std::map<std::string, std::string>& urls,
                                   const std::string& chainId) {
    for (const auto& [key, value] : urls) {
        if (toLower(key) == chainId) {
            std::string url = value;
            while (!url.empty() && url.back() == '/') {
                url.pop_back();
            }
            return url;
        }
    }
    return std::nullopt;
}

}  // namespace

const std::map<std::string, std::string>& DEFAULT_WEB_AUTHENTICATOR_URLS() {
    static const std::map<std::string, std::string> urls = {
        // Vaulta
        {"aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906",
         "https://vaulta.anchorwallet.io"},
        // Jungle 4
        {"73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d",
         "https://jungle4.anchorwallet.io"},
    };
    return urls;
}

std::optional<std::string> resolveWebAuthenticatorUrl(
    const std::optional<std::string>& chainId,
    const std::map<std::string, std::string>& overrides) {
    if (!chainId || chainId->empty()) {
        return std::nullopt;
    }
    const std::string key = toLower(*chainId);
    if (auto url = findUrl(overrides, key)) {
        return url;
    }
    return findUrl(DEFAULT_WEB_AUTHENTICATOR_URLS(), key);
}

}  // namespace dwarfkit::anchor
