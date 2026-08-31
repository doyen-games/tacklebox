#include <dwarfkit/antelope/api/provider.hpp>

#include <algorithm>

namespace dwarfkit {

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

}  // namespace

std::optional<std::string> APIResponse::header(std::string_view name) const {
    for (const auto& [key, value] : headers) {
        if (toLower(key) == toLower(std::string(name))) {
            return value;
        }
    }
    return std::nullopt;
}

FetchAPIProvider::FetchAPIProvider(std::string url, std::shared_ptr<FetchProvider> fetch,
                                   std::vector<std::pair<std::string, std::string>> headers)
    : url_(std::move(url)), fetch_(std::move(fetch)), headers_(std::move(headers)) {
    // trim trailing slash like upstream
    while (url_.ends_with('/')) {
        url_.pop_back();
    }
}

Result<APIResponse> FetchAPIProvider::call(const APIRequest& request) {
    if (!fetch_) {
        return err(ErrorKind::Transport, "Missing fetch");
    }
    FetchRequest fetchRequest;
    fetchRequest.url = url_ + request.path;
    fetchRequest.method = request.method;
    if (request.params) {
        fetchRequest.body = request.params->dump();
    }
    fetchRequest.headers = headers_;
    for (const auto& header : request.headers) {
        fetchRequest.headers.push_back(header);
    }
    DK_TRY(response, fetch_->fetch(fetchRequest));

    APIResponse rv;
    rv.status = response.status;
    rv.text = std::move(response.body);
    const json parsed = json::parse(rv.text, nullptr, false);
    if (!parsed.is_discarded()) {
        rv.json = parsed;
    }
    for (auto& [key, value] : response.headers) {
        rv.headers.emplace_back(toLower(key), std::move(value));
    }
    return rv;
}

}  // namespace dwarfkit
