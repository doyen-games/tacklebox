#include <dwarfkit/transport/curl_fetch_provider.hpp>

#include <mutex>

#include <curl/curl.h>

namespace dwarfkit {

namespace {

void globalInit() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t writeBody(char* data, size_t size, size_t count, void* userdata) {
    static_cast<std::string*>(userdata)->append(data, size * count);
    return size * count;
}

size_t writeHeader(char* data, size_t size, size_t count, void* userdata) {
    auto* headers = static_cast<std::vector<std::pair<std::string, std::string>>*>(userdata);
    const std::string line(data, size * count);
    const size_t colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        const auto trim = [](std::string& s) {
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) {
                s.pop_back();
            }
            while (!s.empty() && s.front() == ' ') {
                s.erase(s.begin());
            }
        };
        trim(key);
        trim(value);
        headers->emplace_back(std::move(key), std::move(value));
    }
    return size * count;
}

}  // namespace

Result<FetchResponse> CurlFetchProvider::fetch(const FetchRequest& request) {
    globalInit();
    CURL* curl = curl_easy_init();
    if (!curl) {
        return err(ErrorKind::Transport, "Failed to initialize curl");
    }
    FetchResponse response;
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_.count()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    curl_slist* headerList = nullptr;
    for (const auto& [key, value] : request.headers) {
        headerList = curl_slist_append(headerList, (key + ": " + value).c_str());
    }
    if (request.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    } else if (request.method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    }
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }

    const CURLcode status = curl_easy_perform(curl);
    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    response.status = static_cast<int>(httpStatus);
    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    if (status != CURLE_OK) {
        return err(ErrorKind::Transport, curl_easy_strerror(status));
    }
    return response;
}

}  // namespace dwarfkit
