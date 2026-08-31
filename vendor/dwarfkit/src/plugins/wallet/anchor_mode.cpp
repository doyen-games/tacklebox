#include <dwarfkit/plugins/wallet/anchor/mode.hpp>

namespace dwarfkit::anchor {

const char* modeString(AnchorMode mode) {
    return mode == AnchorMode::web ? "web" : "app";
}

std::optional<AnchorMode> parseMode(const json& value) {
    if (value.is_string()) {
        const auto& str = value.get_ref<const std::string&>();
        if (str == "web") {
            return AnchorMode::web;
        }
        if (str == "app") {
            return AnchorMode::app;
        }
    }
    return std::nullopt;
}

std::optional<AnchorMode> readMode(const json& data) {
    if (data.is_object()) {
        if (data.contains("mode")) {
            if (const auto mode = parseMode(data["mode"])) {
                return mode;
            }
        }
        const auto truthy = [&](const char* key) {
            if (!data.contains(key)) {
                return false;
            }
            const json& value = data[key];
            return !(value.is_null() ||
                     (value.is_string() && value.get_ref<const std::string&>().empty()) ||
                     (value.is_boolean() && !value.get<bool>()));
        };
        if (truthy("channelUrl") || truthy("signerKey")) {
            return AnchorMode::app;
        }
        if (truthy("encryptionKey") && truthy("messageKey")) {
            return AnchorMode::web;
        }
    }
    return std::nullopt;
}

void writeMode(json& data, AnchorMode mode) {
    data["mode"] = modeString(mode);
}

void clearMode(json& data) {
    if (data.is_object()) {
        data.erase("mode");
    }
}

Result<AnchorLoginOptions> readLoginOptions(const std::string& id, const json& arbitrary) {
    if (!arbitrary.is_object() || !arbitrary.contains(id) || arbitrary[id].is_null()) {
        return AnchorLoginOptions{};
    }
    const json& options = arbitrary[id];
    if (!options.is_object()) {
        return err(ErrorKind::Invalid, "Invalid Anchor login options: " + options.dump());
    }
    AnchorLoginOptions result;
    if (options.contains("mode")) {
        const auto mode = parseMode(options["mode"]);
        if (!mode) {
            return err(ErrorKind::Invalid, "Invalid Anchor mode: " + options["mode"].dump());
        }
        result.mode = mode;
    }
    return result;
}

}  // namespace dwarfkit::anchor
