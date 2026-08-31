// Port of antelope src/api/client.ts
#pragma once

#include <memory>

#include <dwarfkit/antelope/api/provider.hpp>
#include <dwarfkit/antelope/api/v1/chain.hpp>
#include <dwarfkit/antelope/api/v1/history.hpp>
#include <dwarfkit/antelope/serializer.hpp>

namespace dwarfkit {

struct APIClientOptions {
    // URL to the API node to use, only used if the provider option is not set.
    std::string url;
    // API provider to use; if omitted and url is set the default provider is used.
    std::shared_ptr<APIProvider> provider;
    // Transport used by the default provider; CurlFetchProvider when omitted
    // and DK_WITH_CURL is enabled.
    std::shared_ptr<FetchProvider> fetch;
    // Headers applied to every request by the default provider.
    std::vector<std::pair<std::string, std::string>> headers;
};

// Free-function equivalents of the TS APIError accessors; every failed call
// returns an Error{kind: Api} whose details carry {path, response}.
namespace apierror {

Error make(const std::string& path, const APIResponse& response);
// The nodeos error object, or null.
json error(const Error& error);
// The nodeos error name, e.g. tx_net_usage_exceeded; "unspecified" fallback.
std::string name(const Error& error);
// The nodeos error code, e.g. 3080002; 0 fallback.
int64_t code(const Error& error);
// List of exception details, if any.
json details(const Error& error);
// The full response object {status, headers, json, text}.
json response(const Error& error);

}  // namespace apierror

class APIClient {
public:
    explicit APIClient(APIClientOptions options);

    APIClient(const APIClient&) = delete;
    APIClient& operator=(const APIClient&) = delete;

    std::shared_ptr<APIProvider> provider;

    struct V1 {
        ChainAPI chain;
        HistoryAPI history;
    };
    V1 v1;

    Result<json> call(const APIRequest& request);

    template <class T>
    Result<T> call(const APIRequest& request) {
        DK_TRY(payload, call(request));
        return abi_traits<T>::fromJSON(payload);
    }
};

}  // namespace dwarfkit
