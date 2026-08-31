// dkgen CLI (BLUEPRINT.md 7):
//   dkgen generate -u <api url> <account> [-f out.hpp]
//   dkgen generate --abi <file.json> <account> [-f out.hpp]
#include <fstream>
#include <iostream>
#include <sstream>

#include <dwarfkit/contract.hpp>
#ifdef DK_WITH_CURL
#include <dwarfkit/transport/curl_fetch_provider.hpp>
#endif

#include "generator.hpp"

namespace dk = dwarfkit;

namespace {

int usage() {
    std::cerr << "usage: dkgen generate [-u <api url>] [--abi <file.json>] [-f <out.hpp>] "
                 "<account>\n";
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) != "generate") {
        return usage();
    }
    std::string url;
    std::string abiFile;
    std::string outFile;
    std::string account;
    for (int i = 2; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "-u" && i + 1 < argc) {
            url = argv[++i];
        } else if (arg == "--abi" && i + 1 < argc) {
            abiFile = argv[++i];
        } else if (arg == "-f" && i + 1 < argc) {
            outFile = argv[++i];
        } else if (!arg.starts_with("-")) {
            account = arg;
        } else {
            return usage();
        }
    }
    if (account.empty()) {
        std::cerr << "dkgen: an account name is required\n";
        return usage();
    }

    dk::ABI abi;
    if (!abiFile.empty()) {
        std::ifstream file(abiFile);
        if (!file.good()) {
            std::cerr << "dkgen: cannot read " << abiFile << "\n";
            return 1;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        // a JSON ABI, or a base64 blob (the get_raw_abi form)
        std::string text = buffer.str();
        auto parsed = dk::ABI::from(std::string_view(text));
        if (!parsed) {
            while (!text.empty() &&
                   (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
                text.pop_back();
            }
            const auto blob = dk::Blob::from(text);
            if (blob) {
                parsed = dk::ABI::from(*blob);
            }
        }
        if (!parsed) {
            std::cerr << "dkgen: invalid ABI: " << parsed.error().message << "\n";
            return 1;
        }
        abi = *parsed;
    } else if (!url.empty()) {
#ifdef DK_WITH_CURL
        const auto client = std::make_shared<dk::APIClient>(dk::APIClientOptions{
            .url = url, .fetch = std::make_shared<dk::CurlFetchProvider>()});
        const dk::ContractKit kit({.client = client});
        const auto contract = kit.load(dk::Name::from(account));
        if (!contract) {
            std::cerr << "dkgen: failed to fetch ABI: " << contract.error().message << "\n";
            return 1;
        }
        abi = contract->abi;
#else
        std::cerr << "dkgen: built without curl; use --abi <file.json>\n";
        return 1;
#endif
    } else {
        std::cerr << "dkgen: either -u <api url> or --abi <file.json> is required\n";
        return usage();
    }

    const auto code = dk::dkgen::generateContractHeader(account, abi);
    if (!code) {
        std::cerr << "dkgen: " << code.error().message << "\n";
        return 1;
    }
    if (outFile.empty()) {
        std::cout << *code;
    } else {
        std::ofstream file(outFile, std::ios::binary);
        file << *code;
        std::cout << "Generated contract helper for " << account << " saved to " << outFile
                  << "\n";
    }
    return 0;
}
