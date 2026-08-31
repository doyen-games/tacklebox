#include "app/bridge.hpp"

#include "app/controller.hpp"
#include "core/log.hpp"

namespace tb {

namespace dk = dwarfkit;

// --- VaultWalletPlugin -------------------------------------------------------

VaultWalletPlugin::VaultWalletPlugin(Controller& controller, std::string publicKey)
    : controller_(controller), publicKey_(std::move(publicKey)) {
    config_ = {.requiresChainSelect = false, .requiresPermissionSelect = false};
    metadata_.name = "TackleBox Vault";
    metadata_.description = "Signs with keys held in the local encrypted vault, "
                            "gated by the whitelist guard.";
    metadata_.publicKey = publicKey_;
    // No key material is ever placed into data_: serialized sessions must not
    // carry secrets.
}

dk::Result<dk::WalletPluginLoginResponse> VaultWalletPlugin::login(dk::LoginContext& context) {
    dk::WalletPluginLoginResponse response;
    if (context.chain)
        response.chain = context.chain->id;
    else if (!context.chains.empty())
        response.chain = context.chains[0].id;
    if (!context.permissionLevel)
        return dk::err(dk::ErrorKind::Plugin, "TackleBox sessions always carry a permission level");
    response.permissionLevel = *context.permissionLevel;
    return response;
}

dk::Result<dk::WalletPluginSignResponse> VaultWalletPlugin::sign(
    const dk::ResolvedSigningRequest& resolved, dk::TransactContext& context) {
    return controller_.guardedSign(resolved, context, publicKey_);
}

// --- TackleUI ----------------------------------------------------------------

dk::Result<dk::UserInterfaceLoginResponse> TackleUI::login(dk::LoginContext&) {
    return dk::err(dk::ErrorKind::Unsupported,
                   "interactive kit login is not used; TackleBox builds sessions directly");
}

dk::Result<void> TackleUI::onError(const dk::Error& error) {
    controller_.runner().postMain([this, message = error.message] {
        controller_.toast(Toast::Error, message);
    });
    return {};
}

dk::Result<dk::UserInterfaceAccountCreationResponse> TackleUI::onAccountCreate(
    dk::CreateAccountContext&) {
    return dk::err(dk::ErrorKind::Unsupported, "account creation flows are not wired up");
}

dk::Result<void> TackleUI::onAccountCreateComplete() { return {}; }
dk::Result<void> TackleUI::onLogin() { return {}; }
dk::Result<void> TackleUI::onLoginComplete() { return {}; }

dk::Result<void> TackleUI::onTransact() {
    controller_.pipelineStatus("assembling transaction");
    return {};
}
dk::Result<void> TackleUI::onTransactComplete() {
    controller_.pipelineStatus("");
    return {};
}
dk::Result<void> TackleUI::onSign() {
    controller_.pipelineStatus("awaiting signature");
    return {};
}
dk::Result<void> TackleUI::onSignComplete() {
    controller_.pipelineStatus("signed");
    return {};
}
dk::Result<void> TackleUI::onBroadcast() {
    controller_.pipelineStatus("broadcasting");
    return {};
}
dk::Result<void> TackleUI::onBroadcastComplete() {
    controller_.pipelineStatus("");
    return {};
}

dk::Result<dk::PromptResponse> TackleUI::prompt(const dk::PromptArgs& args, dk::CancelToken) {
    // Flatten prompt elements into display lines; buttons/links keep labels,
    // assets show their quantity payload.
    std::vector<std::string> lines;
    for (const auto& element : args.elements) {
        std::string line;
        if (element.label) line = *element.label;
        if (element.data.is_string()) {
            if (!line.empty()) line += ": ";
            line += element.data.get<std::string>();
        } else if (!element.data.is_null() && !element.data.empty()) {
            if (!line.empty()) line += ": ";
            line += element.data.dump();
        }
        if (!line.empty()) lines.push_back(line);
    }
    bool accepted = controller_.pluginPromptBlocking(args.title, args.body.value_or(""), lines);
    if (!accepted)
        return dk::err(dk::ErrorKind::Canceled, "declined in TackleBox");
    return dk::PromptResponse{};
}

void TackleUI::status(const std::string& message) { controller_.pipelineStatus(message); }

}  // namespace tb
