// The user interfaces dwarfkit ships (BLUEPRINT.md 6.6): a console UI for
// examples and tests, and a null UI for headless transact-only usage.
#pragma once

#include <dwarfkit/session/login.hpp>
#include <dwarfkit/session/ui.hpp>

namespace dwarfkit {

// A UserInterface that never interacts: notifications are no-ops, login and
// prompt fail. Use for sessions that only transact with pre-set permissions.
class NullUserInterface : public AbstractUserInterface {
public:
    Result<UserInterfaceLoginResponse> login(LoginContext&) override {
        return err(ErrorKind::Unsupported, "NullUserInterface cannot perform a login");
    }
    Result<void> onError(const Error&) override { return {}; }
    Result<UserInterfaceAccountCreationResponse> onAccountCreate(CreateAccountContext&) override {
        return err(ErrorKind::Unsupported, "NullUserInterface cannot create accounts");
    }
    Result<void> onAccountCreateComplete() override { return {}; }
    Result<void> onLogin() override { return {}; }
    Result<void> onLoginComplete() override { return {}; }
    Result<void> onTransact() override { return {}; }
    Result<void> onTransactComplete() override { return {}; }
    Result<void> onSign() override { return {}; }
    Result<void> onSignComplete() override { return {}; }
    Result<void> onBroadcast() override { return {}; }
    Result<void> onBroadcastComplete() override { return {}; }
    Result<PromptResponse> prompt(const PromptArgs&, CancelToken) override {
        return err(ErrorKind::Unsupported, "NullUserInterface cannot prompt");
    }
    void status(const std::string&) override {}
};

// A stdin/stdout UserInterface for console examples: prints status lines and
// asks for numbered selections when the login flow requires them.
class ConsoleUserInterface : public AbstractUserInterface {
public:
    Result<UserInterfaceLoginResponse> login(LoginContext& context) override;
    Result<void> onError(const Error& error) override;
    Result<UserInterfaceAccountCreationResponse> onAccountCreate(
        CreateAccountContext& context) override;
    Result<void> onAccountCreateComplete() override;
    Result<void> onLogin() override;
    Result<void> onLoginComplete() override;
    Result<void> onTransact() override;
    Result<void> onTransactComplete() override;
    Result<void> onSign() override;
    Result<void> onSignComplete() override;
    Result<void> onBroadcast() override;
    Result<void> onBroadcastComplete() override;
    Result<PromptResponse> prompt(const PromptArgs& args, CancelToken token) override;
    void status(const std::string& message) override;
};

}  // namespace dwarfkit
