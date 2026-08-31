// Port of @wharfkit/mock-data's makeMockFetch/makeClient. Fixture files are
// keyed by ripemd160(url + JSON.stringify({method, body?, headers})), the
// exact init object the TS api FetchProvider passes to fetch.
#pragma once

#include <fstream>
#include <sstream>

#include <dwarfkit/antelope/api/client.hpp>
#include <dwarfkit/antelope/chain/checksum.hpp>

namespace dwarfkit::test {

inline constexpr const char* mockUrl = "https://jungle4.greymass.com";
inline constexpr const char* mockChainId =
    "73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d";
inline constexpr const char* mockPrivateKey = "5Jtoxgny5tT7NiNFp1MLogviuPJ9NniWjnU4wKzaX4t7pL4kJ8s";
inline constexpr const char* mockAccountName = "wharfkit1111";
inline constexpr const char* mockPermissionName = "test";

class MockFetchProvider final : public FetchProvider {
public:
    explicit MockFetchProvider(std::string dataDir) : dataDir_(std::move(dataDir)) {}

    Result<FetchResponse> fetch(const FetchRequest& request) override {
        // the fetch init object as recorded by mock-data. Recordings differ by
        // the antelope version used: older ones omit the headers key when no
        // headers were set, newer ones always record "headers": {}. Try the
        // bare form first, then the empty-headers form.
        json init = {{"method", request.method}};
        if (!request.body.empty()) {
            init["body"] = request.body;
        }
        if (!request.headers.empty()) {
            json headers = json::object();
            for (const auto& [key, value] : request.headers) {
                headers[key] = value;
            }
            init["headers"] = std::move(headers);
        }
        const auto filenameFor = [&](const json& params) {
            // replace, not strict: a binary body (e.g. a sealed buoy message)
            // must miss the fixture lookup, not abort it
            const std::string key =
                request.url + params.dump(-1, ' ', false, json::error_handler_t::replace);
            const std::vector<uint8_t> keyBytes(key.begin(), key.end());
            return dataDir_ + "/" + Checksum160::hash(keyBytes).hexString() + ".json";
        };
        std::string filename = filenameFor(init);
        std::ifstream file(filename);
        if (!file.good() && request.headers.empty()) {
            json withEmptyHeaders = init;
            withEmptyHeaders["headers"] = json::object();
            filename = filenameFor(withEmptyHeaders);
            file = std::ifstream(filename);
        }
        if (!file.good()) {
            return err(ErrorKind::NotFound, "No data for " + request.url);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        const json recorded = json::parse(buffer.str(), nullptr, false);
        if (recorded.is_discarded()) {
            return err(ErrorKind::Invalid, "Invalid fixture " + filename);
        }
        FetchResponse response;
        response.status = recorded.value("status", 200);
        response.body = recorded.value("text", "");
        const json recordedHeaders = recorded.value("headers", json::object());
        for (const auto& [headerKey, headerValue] : recordedHeaders.items()) {
            response.headers.emplace_back(
                headerKey,
                headerValue.is_string() ? headerValue.get<std::string>() : headerValue.dump());
        }
        return response;
    }

private:
    std::string dataDir_;
};

// mock-data makeClient()
inline std::shared_ptr<APIClient> makeClient(const std::string& dataDir,
                                             const std::string& url = mockUrl) {
    return std::make_shared<APIClient>(APIClientOptions{
        .url = url, .fetch = std::make_shared<MockFetchProvider>(dataDir)});
}

}  // namespace dwarfkit::test
