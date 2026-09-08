// Where the system keeps its trusted CA certificates. A Linux build carries
// the libcurl of the distro it was built on, and that library has the CA
// bundle path of *that* distro compiled in (Debian's
// /etc/ssl/certs/ca-certificates.crt); run the same binary on Fedora or SUSE
// and every https request fails with "error setting certificate file". The
// AppImage therefore points curl at whichever well-known bundle exists.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tb {

// Candidate bundle files, most common first. SSL_CERT_FILE (the OpenSSL
// convention) leads when set.
std::vector<std::string> caBundleCandidates();

// The first candidate that is a readable regular file, if any.
std::optional<std::string> firstExistingFile(const std::vector<std::string>& candidates);

}  // namespace tb
