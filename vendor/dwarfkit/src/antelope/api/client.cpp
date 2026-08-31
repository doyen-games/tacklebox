#include <dwarfkit/antelope/api/client.hpp>

namespace dwarfkit {

namespace apierror {

namespace {

// APIError.formatError
std::string formatError(const json& error) {
    const std::string what = error.value("what", "");
    const json details = error.value("details", json::array());
    if (what == "unspecified" && details.is_array() && !details.empty() &&
        details[0].value("file", "") == "http_plugin.cpp" &&
        details[0].value("message", "").substr(0, 11) == "unknown key") {
        // fix cryptic error messages from nodeos for missing accounts
        return "Account not found";
    }
    if (what == "unspecified" && details.is_array() && !details.empty()) {
        return details[0].value("message", "");
    }
    if (!what.empty()) {
        return what;
    }
    return "Unknown API error";
}

}  // namespace

Error make(const std::string& path, const APIResponse& apiResponse) {
    std::string message;
    if (apiResponse.json && apiResponse.json->is_object() && apiResponse.json->contains("error")) {
        message = formatError(apiResponse.json->at("error")) + " at " + path;
    } else {
        message = "HTTP " + std::to_string(apiResponse.status) + " at " + path;
    }
    json headers = json::object();
    for (const auto& [key, value] : apiResponse.headers) {
        headers[key] = value;
    }
    json details = {{"path", path},
                    {"response",
                     {{"status", apiResponse.status},
                      {"headers", std::move(headers)},
                      {"json", apiResponse.json ? *apiResponse.json : json(nullptr)},
                      {"text", apiResponse.text}}}};
    return Error{ErrorKind::Api, std::move(message), apiResponse.status, std::move(details)};
}

json error(const Error& e) {
    if (e.details.is_object() && e.details.contains("response")) {
        const json& payload = e.details["response"]["json"];
        if (payload.is_object() && payload.contains("error")) {
            return payload["error"];
        }
    }
    return nullptr;
}

std::string name(const Error& e) {
    const json chainError = error(e);
    return chainError.is_object() ? chainError.value("name", "unspecified") : "unspecified";
}

int64_t code(const Error& e) {
    const json chainError = error(e);
    return chainError.is_object() ? chainError.value("code", int64_t(0)) : 0;
}

json details(const Error& e) {
    const json chainError = error(e);
    return chainError.is_object() ? chainError.value("details", json::array()) : json::array();
}

json response(const Error& e) {
    if (e.details.is_object() && e.details.contains("response")) {
        return e.details["response"];
    }
    return nullptr;
}

}  // namespace apierror

APIClient::APIClient(APIClientOptions options) : v1{ChainAPI(this), HistoryAPI(this)} {
    if (options.provider) {
        provider = std::move(options.provider);
    } else if (!options.url.empty()) {
        provider = std::make_shared<FetchAPIProvider>(options.url, std::move(options.fetch),
                                                      std::move(options.headers));
    }
}

Result<json> APIClient::call(const APIRequest& request) {
    if (!provider) {
        return err(ErrorKind::Invalid, "Missing url or provider");
    }
    DK_TRY(response, provider->call(request));
    const bool jsonError = response.json && response.json->is_object() &&
                           response.json->contains("error") &&
                           response.json->at("error").is_object();
    if (response.status / 100 != 2 || jsonError) {
        return err(apierror::make(request.path, response));
    }
    if (response.json) {
        return *response.json;
    }
    return json(response.text);
}

}  // namespace dwarfkit
