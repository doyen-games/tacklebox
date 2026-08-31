#include <dwarfkit/session/wallet.hpp>

namespace dwarfkit {

WalletPluginMetadata WalletPluginMetadata::from(const json& data) {
    WalletPluginMetadata rv;
    const auto text = [&](const char* key) -> std::optional<std::string> {
        if (data.is_object() && data.contains(key) && data[key].is_string()) {
            return data[key].get<std::string>();
        }
        return std::nullopt;
    };
    rv.name = text("name");
    rv.description = text("description");
    if (data.is_object() && data.contains("logo") && !data["logo"].is_null()) {
        const auto logo = Logo::from(data["logo"]);
        if (logo) {
            rv.logo = *logo;
        }
    }
    rv.homepage = text("homepage");
    rv.download = text("download");
    rv.publicKey = text("publicKey");
    return rv;
}

json WalletPluginMetadata::toJSON() const {
    json rv = json::object();
    if (name) rv["name"] = *name;
    if (description) rv["description"] = *description;
    if (logo) rv["logo"] = Serializer::objectify(*logo);
    if (homepage) rv["homepage"] = *homepage;
    if (download) rv["download"] = *download;
    if (publicKey) rv["publicKey"] = *publicKey;
    return rv;
}

}  // namespace dwarfkit
