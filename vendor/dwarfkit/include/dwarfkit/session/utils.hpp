// Port of session src/utils.ts. getFetch has no C++ equivalent (no global
// fetch); a FetchProvider must be supplied where TS falls back to it.
#pragma once

#include <dwarfkit/session/transact.hpp>

namespace dwarfkit {

// Append an action to the end of the array of actions in a SigningRequest.
Result<SigningRequest> appendAction(const SigningRequest& request, const Action& action);
Result<SigningRequest> appendAction(const SigningRequest& request, const json& action);

// Prepend an action to the start of the array of actions in a SigningRequest.
Result<SigningRequest> prependAction(const SigningRequest& request, const Action& action);
Result<SigningRequest> prependAction(const SigningRequest& request, const json& action);

// Prefix a plugin's translations with its id: {lang: {id: definitions}}.
template <class Plugin>
LocaleDefinitions getPluginTranslations(const Plugin& plugin) {
    const LocaleDefinitions translations = plugin.translations();
    if (!translations.is_object() || translations.empty()) {
        return json::object();
    }
    json prefixed = json::object();
    for (const auto& item : translations.items()) {
        prefixed[item.key()] = json{{plugin.id(), item.value()}};
    }
    return prefixed;
}

}  // namespace dwarfkit
