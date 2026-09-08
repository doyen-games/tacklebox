#include "core/ca_bundle.hpp"

#include <cstdlib>
#include <filesystem>

namespace tb {

std::vector<std::string> caBundleCandidates() {
    std::vector<std::string> out;
    if (const char* env = std::getenv("SSL_CERT_FILE"); env && *env) out.emplace_back(env);
    out.insert(out.end(), {
                              "/etc/ssl/certs/ca-certificates.crt",  // Debian, Ubuntu, Arch
                              "/etc/pki/tls/certs/ca-bundle.crt",    // Fedora, RHEL, CentOS
                              "/etc/ssl/ca-bundle.pem",              // openSUSE, SLES
                              "/etc/pki/tls/cacert.pem",             // OpenELEC
                              "/etc/ssl/cert.pem",                   // Alpine, FreeBSD, macOS
                              "/usr/share/ssl/certs/ca-bundle.crt",  // older Red Hat
                              "/usr/local/share/certs/ca-root-nss.crt",  // FreeBSD (nss)
                          });
    return out;
}

std::optional<std::string> firstExistingFile(const std::vector<std::string>& candidates) {
    for (const auto& path : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec)) return path;
    }
    return std::nullopt;
}

}  // namespace tb
