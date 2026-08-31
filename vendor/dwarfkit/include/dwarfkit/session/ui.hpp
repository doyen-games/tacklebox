// Port of session src/ui.ts. Promise-returning notifications become blocking
// Result<void>; prompt takes a CancelToken (Cancelable, BLUEPRINT.md 2).
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <dwarfkit/antelope.hpp>
#include <dwarfkit/common/locale.hpp>
#include <dwarfkit/core/cancel.hpp>

namespace dwarfkit {

class LoginContext;
class CreateAccountContext;

// The different types of elements that can be used in PromptArgs.
enum class PromptElementType {
    accept,
    asset,
    button,
    close,
    countdown,
    link,
    qr,
    textarea,
};

struct PromptElement {
    PromptElementType type = PromptElementType::button;
    std::optional<std::string> label;
    json data;
};

// The arguments for a UserInterface::prompt call.
struct PromptArgs {
    std::string title;
    std::optional<std::string> body;
    bool optional = false;
    std::vector<PromptElement> elements;
};

// The response for a UserInterface::prompt call.
struct PromptResponse {};

// The response for a login call of a UserInterface.
struct UserInterfaceLoginResponse {
    std::optional<Checksum256> chainId;
    std::optional<PermissionLevel> permissionLevel;
    int walletPluginIndex = 0;
};

// The response for an account creation call of a UserInterface.
struct UserInterfaceAccountCreationResponse {
    // If account creation can only be done on one chain.
    std::optional<Checksum256> chain;
    // The id of the plugin that was selected.
    std::optional<std::string> pluginId;
};

// The options to pass to UserInterface::translate.
struct UserInterfaceTranslateOptions {
    std::string defaultValue;
    // interpolation values, TS `[key: string]: unknown`
    json values;
};

// The translate function the UserInterface expects and uses.
using UserInterfaceTranslateFunction =
    std::function<std::string(const std::string& key, const UserInterfaceTranslateOptions&)>;

// Interface which all 3rd party user interface plugins must implement.
struct UserInterface {
    // Interact with the user to collect the data needed for a login response.
    virtual Result<UserInterfaceLoginResponse> login(LoginContext& context) = 0;
    // Inform the UI that an error has occurred.
    virtual Result<void> onError(const Error& error) = 0;
    // Inform the UI that an account creation process has started.
    virtual Result<UserInterfaceAccountCreationResponse> onAccountCreate(
        CreateAccountContext& context) = 0;
    // Inform the UI that an account creation call has completed.
    virtual Result<void> onAccountCreateComplete() = 0;
    // Inform the UI that a login call has started.
    virtual Result<void> onLogin() = 0;
    // Inform the UI that a login call has completed.
    virtual Result<void> onLoginComplete() = 0;
    // Inform the UI that a transact call has started.
    virtual Result<void> onTransact() = 0;
    // Inform the UI that a transact call has completed.
    virtual Result<void> onTransactComplete() = 0;
    // Inform the UI that a transact call has started signing the transaction.
    virtual Result<void> onSign() = 0;
    // Inform the UI that a transact call has completed signing the transaction.
    virtual Result<void> onSignComplete() = 0;
    // Inform the UI that a transact call has started broadcasting.
    virtual Result<void> onBroadcast() = 0;
    // Inform the UI that a transact call has completed broadcasting.
    virtual Result<void> onBroadcastComplete() = 0;
    // Prompt the user with a custom UI element.
    virtual Result<PromptResponse> prompt(const PromptArgs& args, CancelToken token) = 0;
    // Update the displayed modal status from a TransactPlugin.
    virtual void status(const std::string& message) = 0;
    // Translate a string using the UI's language.
    virtual std::string translate(const std::string& key,
                                  const UserInterfaceTranslateOptions& options = {},
                                  const std::string& ns = "") = 0;
    // Returns a translator for a specific namespace.
    virtual UserInterfaceTranslateFunction getTranslate(const std::string& ns = "") = 0;
    // Programmatically add new localization strings to the user interface.
    virtual void addTranslations(const LocaleDefinitions& translations) = 0;
    virtual ~UserInterface() = default;
};

// Abstract class which 3rd party UserInterface implementations may extend.
// The upstream translate/addTranslations defaults throw "must be implemented";
// without exceptions translate falls back to the default value or key and
// addTranslations is a no-op (see DIVERGENCES.md).
class AbstractUserInterface : public UserInterface {
public:
    std::string translate(const std::string& key, const UserInterfaceTranslateOptions& options = {},
                          const std::string& ns = "") override {
        (void)ns;
        return options.defaultValue.empty() ? key : options.defaultValue;
    }
    UserInterfaceTranslateFunction getTranslate(const std::string& ns = "") override {
        return [this, ns](const std::string& key, const UserInterfaceTranslateOptions& options) {
            return this->translate(key, options, ns);
        };
    }
    void addTranslations(const LocaleDefinitions& translations) override { (void)translations; }
};

}  // namespace dwarfkit
