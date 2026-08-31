#include <dwarfkit/contract/utils.hpp>

#include <cctype>
#include <regex>

namespace dwarfkit {

std::string pascalCase(const std::string& value) {
    std::string rv;
    std::string word;
    const auto flush = [&] {
        if (!word.empty()) {
            rv += static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
            for (size_t i = 1; i < word.size(); i++) {
                rv += static_cast<char>(std::tolower(static_cast<unsigned char>(word[i])));
            }
            word.clear();
        }
    };
    for (const char c : value) {
        if (c == '_' || c == ' ') {
            flush();
        } else {
            word += c;
        }
    }
    flush();
    return rv;
}

std::string capitalize(const std::string& value) {
    if (value.empty()) {
        return "";
    }
    std::string rv = value;
    rv[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(rv[0])));
    return rv;
}

std::string singularize(const std::string& word) {
    if (word.ends_with("ies")) {
        return word.substr(0, word.size() - 3) + "y";
    }
    if (word.ends_with("ches") || word.ends_with("ses")) {
        return word.substr(0, word.size() - 2);
    }
    if (word.ends_with("s") && word.size() > 1 && word[word.size() - 2] != 's') {
        return word.substr(0, word.size() - 1);
    }
    return word;
}

std::string indexPositionInWords(size_t index) {
    static const char* words[] = {"primary", "secondary", "tertiary", "fourth", "fifth",
                                  "sixth",   "seventh",   "eighth",   "ninth",  "tenth"};
    return index < 10 ? words[index] : "";
}

json wrapIndexValue(const json& value) {
    if (value.is_null()) {
        return json();
    }
    if (value.is_number_unsigned() || value.is_number_integer()) {
        return abi_traits<uint64_t>::toJSON(value.get<uint64_t>());
    }
    if (value.is_number_float()) {
        return abi_traits<uint64_t>::toJSON(static_cast<uint64_t>(value.get<double>()));
    }
    // strings pass through: names, hex checksums and pre-wrapped values all
    // serialize to their string forms anyway
    return value;
}

bool isAbsentScope(const json& value) {
    return value.is_null() || (value.is_string() && value.get_ref<const std::string&>().empty());
}

Result<json> wrapScopeValue(const json& value) {
    if (value.is_null()) {
        return err(ErrorKind::Invalid, "Scope is required");
    }
    if (value.is_string()) {
        // strings reach the chain untouched, which reads an all-digit scope
        // as a number and the rest as a name
        return value;
    }
    if (value.is_number_float()) {
        const double number = value.get<double>();
        if (number != static_cast<double>(static_cast<int64_t>(number))) {
            return err(ErrorKind::Invalid,
                       "Scope " + value.dump() +
                           " is not an integer a number can hold, use UInt64.from() to pass it "
                           "instead");
        }
    }
    if ((value.is_number_integer() && !value.is_number_unsigned() &&
         value.get<int64_t>() < 0) ||
        (value.is_number_float() && value.get<double>() < 0)) {
        return err(ErrorKind::Invalid, "Scope " + value.dump() + " underflows uint64");
    }
    return abi_traits<uint64_t>::toJSON(value.get<uint64_t>());
}

Blob abiToBlob(const ABI& abi) {
    const auto serialized = Serializer::encode(abi);
    return Blob(serialized ? serialized->array : std::vector<uint8_t>());
}

Result<ABI> blobStringToAbi(const std::string& blobString) {
    DK_TRY(blob, Blob::from(blobString));
    return Serializer::decode<ABI>(blob.array);
}

std::string formatExceptionMessage(const json& except) {
    if (except.contains("stack") && except["stack"].is_array() && !except["stack"].empty()) {
        const json& top = except["stack"][0];
        const std::string format = top.value("format", "");
        if (!format.empty()) {
            const json data = top.contains("data") && top["data"].is_object() ? top["data"]
                                                                              : json::object();
            std::string substituted;
            const std::regex placeholder("\\$\\{(\\w+)\\}");
            auto begin = std::sregex_iterator(format.begin(), format.end(), placeholder);
            auto end = std::sregex_iterator();
            size_t last = 0;
            for (auto it = begin; it != end; ++it) {
                const auto& match = *it;
                substituted += format.substr(last, static_cast<size_t>(match.position()) - last);
                const std::string key = match[1].str();
                if (data.contains(key)) {
                    const json& item = data[key];
                    substituted += item.is_string() ? item.get<std::string>() : item.dump();
                } else {
                    substituted += "${" + key + "}";
                }
                last = static_cast<size_t>(match.position() + match.length());
            }
            substituted += format.substr(last);
            if (!substituted.empty()) {
                return substituted;
            }
        }
        if (top.contains("data") && top["data"].is_object() && top["data"].contains("s") &&
            top["data"]["s"].is_string()) {
            return top["data"]["s"].get<std::string>();
        }
    }
    return except.value("message", "");
}

}  // namespace dwarfkit
