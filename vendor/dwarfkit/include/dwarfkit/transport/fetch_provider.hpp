// The HTTP transport boundary (BLUEPRINT.md 5.3). Engines implement this;
// CurlFetchProvider is the default outside engines when DK_WITH_CURL is on.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

struct FetchRequest {
    std::string url;
    std::string method{"POST"};
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
};

struct FetchResponse {
    int status = 0;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
};

struct FetchProvider {
    virtual Result<FetchResponse> fetch(const FetchRequest& request) = 0;
    virtual ~FetchProvider() = default;
};

}  // namespace dwarfkit
