// Where TackleBox plugs into the dwarfkit session pipeline.
//
// VaultWalletPlugin is the WalletPlugin the Session signs with: its sign()
// runs the guard (whitelist + risk + contract hash pinning) and either signs
// silently on an auto-sign rule or blocks on the signing modal.
//
// TackleUI is the UserInterface handed to sessions so transact plugins
// (resource provider fees etc.) can talk to the human through our modals.
#pragma once

#include <memory>
#include <string>

#include <dwarfkit/session.hpp>

namespace tb {

class Controller;

class VaultWalletPlugin : public dwarfkit::AbstractWalletPlugin {
public:
    VaultWalletPlugin(Controller& controller, std::string publicKey);

    std::string id() const override { return "tacklebox-vault"; }
    dwarfkit::Result<dwarfkit::WalletPluginLoginResponse> login(
        dwarfkit::LoginContext& context) override;
    dwarfkit::Result<dwarfkit::WalletPluginSignResponse> sign(
        const dwarfkit::ResolvedSigningRequest& resolved,
        dwarfkit::TransactContext& context) override;

private:
    Controller& controller_;
    std::string publicKey_;
};

class TackleUI : public dwarfkit::AbstractUserInterface {
public:
    explicit TackleUI(Controller& controller) : controller_(controller) {}

    dwarfkit::Result<dwarfkit::UserInterfaceLoginResponse> login(
        dwarfkit::LoginContext& context) override;
    dwarfkit::Result<void> onError(const dwarfkit::Error& error) override;
    dwarfkit::Result<dwarfkit::UserInterfaceAccountCreationResponse> onAccountCreate(
        dwarfkit::CreateAccountContext& context) override;
    dwarfkit::Result<void> onAccountCreateComplete() override;
    dwarfkit::Result<void> onLogin() override;
    dwarfkit::Result<void> onLoginComplete() override;
    dwarfkit::Result<void> onTransact() override;
    dwarfkit::Result<void> onTransactComplete() override;
    dwarfkit::Result<void> onSign() override;
    dwarfkit::Result<void> onSignComplete() override;
    dwarfkit::Result<void> onBroadcast() override;
    dwarfkit::Result<void> onBroadcastComplete() override;
    dwarfkit::Result<dwarfkit::PromptResponse> prompt(const dwarfkit::PromptArgs& args,
                                                      dwarfkit::CancelToken token) override;
    void status(const std::string& message) override;

private:
    Controller& controller_;
};

}  // namespace tb
