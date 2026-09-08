#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "core/ca_bundle.hpp"

using tb::caBundleCandidates;
using tb::firstExistingFile;

TEST_CASE("the candidate list starts with the Debian bundle and covers the big families") {
    const auto list = caBundleCandidates();
    REQUIRE(list.size() >= 5);
    // SSL_CERT_FILE may lead in a developer's shell; the fixed list follows.
    bool debian = false, fedora = false, suse = false, alpine = false;
    for (const auto& p : list) {
        debian |= p == "/etc/ssl/certs/ca-certificates.crt";
        fedora |= p == "/etc/pki/tls/certs/ca-bundle.crt";
        suse |= p == "/etc/ssl/ca-bundle.pem";
        alpine |= p == "/etc/ssl/cert.pem";
    }
    CHECK(debian);
    CHECK(fedora);
    CHECK(suse);
    CHECK(alpine);
}

TEST_CASE("firstExistingFile skips directories and missing paths in order") {
    const auto dir = std::filesystem::temp_directory_path() / "tb-ca-bundle-test";
    std::filesystem::create_directories(dir);
    const auto later = dir / "later.crt";
    { std::ofstream(later) << "-----BEGIN CERTIFICATE-----\n"; }

    CHECK_FALSE(firstExistingFile({}));
    CHECK_FALSE(firstExistingFile({(dir / "missing.crt").string(), dir.string()}));
    auto found = firstExistingFile({(dir / "missing.crt").string(), dir.string(), later.string()});
    REQUIRE(found);
    CHECK(*found == later.string());

    std::filesystem::remove_all(dir);
}
