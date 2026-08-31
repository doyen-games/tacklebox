// Port of antelope test/utils/mock-provider.ts. Replays the recorded fixtures
// keyed by ripemd160(api + path + context + JSON.stringify(params)). Recording
// against live nodes is not ported; fixtures come from the upstream repos.
#pragma once

#include <fstream>
#include <sstream>

#include <dwarfkit/antelope/api/provider.hpp>
#include <dwarfkit/antelope/chain/checksum.hpp>

namespace dwarfkit::test {

class MockProvider final : public APIProvider {
public:
    explicit MockProvider(std::string api = "https://jungle4.greymass.com",
                          std::string dataDir = DK_FIXTURE_DIR "/antelope/data")
        : api_(std::move(api)), dataDir_(std::move(dataDir)) {}

    void setContext(std::string name) { context_ = std::move(name); }

    std::string getFilename(const APIRequest& request) const {
        const std::string key =
            api_ + request.path + context_ + (request.params ? request.params->dump() : "");
        const std::vector<uint8_t> keyBytes(key.begin(), key.end());
        return dataDir_ + "/" + Checksum160::hash(keyBytes).hexString() + ".json";
    }

    Result<APIResponse> call(const APIRequest& request) override {
        const std::string filename = getFilename(request);
        std::ifstream file(filename);
        if (!file.good()) {
            return err(ErrorKind::NotFound, "No data for " + request.path);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        const json recorded = json::parse(buffer.str(), nullptr, false);
        if (recorded.is_discarded()) {
            return err(ErrorKind::Invalid, "Invalid fixture " + filename);
        }
        APIResponse response;
        response.status = recorded.value("status", 200);
        response.text = recorded.value("text", "");
        if (recorded.contains("json")) {
            response.json = recorded.at("json");
        }
        const json recordedHeaders = recorded.value("headers", json::object());
        for (const auto& [key, value] : recordedHeaders.items()) {
            response.headers.emplace_back(key, value.is_string() ? value.get<std::string>()
                                                                 : value.dump());
        }
        return response;
    }

private:
    std::string api_;
    std::string dataDir_;
    std::string context_;
};

}  // namespace dwarfkit::test
