// Default HTTP transport on libcurl; built into the optional dwarfkit_curl
// target when DK_WITH_CURL is enabled. Engine builds supply their own
// FetchProvider instead.
#pragma once

#include <chrono>

#include <dwarfkit/transport/fetch_provider.hpp>

namespace dwarfkit {

class CurlFetchProvider final : public FetchProvider {
public:
    explicit CurlFetchProvider(std::chrono::milliseconds timeout = std::chrono::seconds(30))
        : timeout_(timeout) {}

    Result<FetchResponse> fetch(const FetchRequest& request) override;

private:
    std::chrono::milliseconds timeout_;
};

}  // namespace dwarfkit
