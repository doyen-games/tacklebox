#include <dwarfkit/session/account_creation.hpp>

namespace dwarfkit {

AccountCreationPluginMetadata AccountCreationPluginMetadata::from(const json& data) {
    AccountCreationPluginMetadata rv;
    const auto text = [&](const char* key) -> std::optional<std::string> {
        if (data.is_object() && data.contains(key) && data[key].is_string()) {
            return data[key].get<std::string>();
        }
        return std::nullopt;
    };
    rv.name = data.is_object() ? data.value("name", "") : "";
    rv.description = text("description");
    if (data.is_object() && data.contains("logo") && !data["logo"].is_null()) {
        const auto logo = Logo::from(data["logo"]);
        if (logo) {
            rv.logo = *logo;
        }
    }
    rv.homepage = text("homepage");
    return rv;
}

json AccountCreationPluginMetadata::toJSON() const {
    json rv = json::object();
    rv["name"] = name;
    if (description) rv["description"] = *description;
    if (logo) rv["logo"] = Serializer::objectify(*logo);
    if (homepage) rv["homepage"] = *homepage;
    return rv;
}

CreateAccountContext::CreateAccountContext(const CreateAccountContextOptions& options)
    : accountCreationPlugins(options.accountCreationPlugins),
      appName(options.appName),
      chain(options.chain),
      chains(options.chains),
      fetch(options.fetch),
      ui(options.ui) {
    if (options.uiRequirements) {
        uiRequirements = *options.uiRequirements;
    }
}

std::shared_ptr<APIClient> CreateAccountContext::getClient(const ChainDefinition& forChain) const {
    return std::make_shared<APIClient>(APIClientOptions{.url = forChain.url, .fetch = fetch});
}

}  // namespace dwarfkit
