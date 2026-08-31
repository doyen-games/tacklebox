// Port of antelope src/api/provider.ts
#pragma once

#include <memory>
#include <optional>
#include <string>

#include <dwarfkit/core/result.hpp>
#include <dwarfkit/transport/fetch_provider.hpp>

namespace dwarfkit {

struct APIRequest {
    // The endpoint path, e.g. /v1/chain/get_info
    std::string path;
    // The request body if any.
    std::optional<json> params;
    std::string method{"POST"};
    std::vector<std::pair<std::string, std::string>> headers;
};

// Response to an API call.
struct APIResponse {
    int status = 0;
    std::string text;
    std::optional<json> json;
    std::vector<std::pair<std::string, std::string>> headers;

    std::optional<std::string> header(std::string_view name) const;
};

struct APIProvider {
    // Call an API endpoint and return the response. The provider is
    // responsible for JSON encoding the params and decoding the response.
    virtual Result<APIResponse> call(const APIRequest& request) = 0;
    virtual ~APIProvider() = default;
};

// Default provider that uses a FetchProvider transport to call a single node
// (the TS api FetchProvider class; renamed to avoid colliding with the
// transport interface, see DIVERGENCES.md).
class FetchAPIProvider final : public APIProvider {
public:
    FetchAPIProvider(std::string url, std::shared_ptr<FetchProvider> fetch,
                     std::vector<std::pair<std::string, std::string>> headers = {});

    Result<APIResponse> call(const APIRequest& request) override;

    const std::string& url() const { return url_; }

private:
    std::string url_;
    std::shared_ptr<FetchProvider> fetch_;
    std::vector<std::pair<std::string, std::string>> headers_;
};

}  // namespace dwarfkit
