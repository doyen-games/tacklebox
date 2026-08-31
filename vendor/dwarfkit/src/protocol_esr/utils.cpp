#include <dwarfkit/protocol_esr/utils.hpp>

#include <cctype>
#include <iostream>

#include <dwarfkit/antelope/utils.hpp>

namespace dwarfkit {

std::string snakeToPascal(const std::string& name) {
    std::string rv;
    rv.reserve(name.size());
    bool upperNext = true;
    for (const char c : name) {
        if (c == '_') {
            upperNext = true;
            continue;
        }
        rv += upperNext ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        upperNext = false;
    }
    return rv;
}

std::string snakeToCamel(const std::string& name) {
    std::string rv = snakeToPascal(name);
    if (!rv.empty()) {
        rv[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(rv[0])));
    }
    return rv;
}

std::function<void(const std::string&)>& logWarnSink() {
    static std::function<void(const std::string&)> sink = [](const std::string& message) {
        std::cerr << message << "\n";
    };
    return sink;
}

void logWarn(const std::string& message) { logWarnSink()("[anchor-link] " + message); }

std::string uuid() {
    // same layout as upstream: version nibble 4, variant nibble 8..b, but
    // fed from the CSPRNG instead of Math.random
    static constexpr char chars[] = "0123456789abcdef";
    const auto random = secureRandom(36);
    std::string rv;
    rv.reserve(36);
    for (int i = 0; i < 36; i++) {
        const uint8_t byte = random ? (*random)[static_cast<size_t>(i)] : uint8_t(0);
        switch (i) {
            case 8:
            case 13:
            case 18:
            case 23:
                rv += '-';
                break;
            case 14:
                rv += '4';
                break;
            case 19:
                rv += chars[8 + byte % 4];
                break;
            default:
                rv += chars[byte % 16];
        }
    }
    return rv;
}

std::optional<std::string> generateReturnUrl() { return std::nullopt; }

}  // namespace dwarfkit
