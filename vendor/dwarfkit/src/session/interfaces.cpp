#include <dwarfkit/session/interfaces.hpp>

#include <iostream>

#include <dwarfkit/session/account_creation.hpp>

namespace dwarfkit {

namespace {

// read a selection index [0, count); returns nullopt on invalid input
std::optional<size_t> readSelection(size_t count) {
    std::string line;
    if (!std::getline(std::cin, line)) {
        return std::nullopt;
    }
    try {
        const size_t value = std::stoul(line);
        if (value < count) {
            return value;
        }
    } catch (...) {
    }
    return std::nullopt;
}

}  // namespace

Result<UserInterfaceLoginResponse> ConsoleUserInterface::login(LoginContext& context) {
    UserInterfaceLoginResponse response;
    if (context.uiRequirements.requiresWalletSelect) {
        std::cout << "Select a wallet:\n";
        for (size_t i = 0; i < context.walletPlugins.size(); i++) {
            std::cout << "  [" << i << "] "
                      << context.walletPlugins[i].metadata.name.value_or("unnamed wallet") << "\n";
        }
        const auto selected = readSelection(context.walletPlugins.size());
        if (!selected) {
            return err(ErrorKind::Canceled, "No wallet selected");
        }
        response.walletPluginIndex = static_cast<int>(*selected);
    } else if (context.walletPluginIndex) {
        response.walletPluginIndex = *context.walletPluginIndex;
    }
    if (context.uiRequirements.requiresChainSelect && !context.chains.empty()) {
        std::cout << "Select a chain:\n";
        for (size_t i = 0; i < context.chains.size(); i++) {
            std::cout << "  [" << i << "] " << context.chains[i].id.hexString() << " ("
                      << context.chains[i].url << ")\n";
        }
        const auto selected = readSelection(context.chains.size());
        if (!selected) {
            return err(ErrorKind::Canceled, "No chain selected");
        }
        response.chainId = context.chains[*selected].id;
    }
    if (context.uiRequirements.requiresPermissionSelect ||
        context.uiRequirements.requiresPermissionEntry) {
        std::cout << "Enter the permission to use (actor@permission): ";
        std::string line;
        if (!std::getline(std::cin, line) || line.empty()) {
            return err(ErrorKind::Canceled, "No permission provided");
        }
        DK_TRY(permission, PermissionLevel::from(line));
        response.permissionLevel = permission;
    }
    return response;
}

Result<void> ConsoleUserInterface::onError(const Error& error) {
    std::cerr << "[error] " << error.message << "\n";
    return {};
}

Result<UserInterfaceAccountCreationResponse> ConsoleUserInterface::onAccountCreate(
    CreateAccountContext& context) {
    UserInterfaceAccountCreationResponse response;
    if (context.uiRequirements.requiresPluginSelect && !context.accountCreationPlugins.empty()) {
        std::cout << "Select an account creation service:\n";
        for (size_t i = 0; i < context.accountCreationPlugins.size(); i++) {
            std::cout << "  [" << i << "] " << context.accountCreationPlugins[i]->name() << "\n";
        }
        const auto selected = readSelection(context.accountCreationPlugins.size());
        if (!selected) {
            return err(ErrorKind::Canceled, "No account creation plugin selected");
        }
        response.pluginId = context.accountCreationPlugins[*selected]->id();
    }
    if (context.uiRequirements.requiresChainSelect && !context.chains.empty()) {
        std::cout << "Select a chain:\n";
        for (size_t i = 0; i < context.chains.size(); i++) {
            std::cout << "  [" << i << "] " << context.chains[i].id.hexString() << "\n";
        }
        const auto selected = readSelection(context.chains.size());
        if (!selected) {
            return err(ErrorKind::Canceled, "No chain selected");
        }
        response.chain = context.chains[*selected].id;
    }
    return response;
}

Result<void> ConsoleUserInterface::onAccountCreateComplete() { return {}; }
Result<void> ConsoleUserInterface::onLogin() {
    std::cout << "Logging in...\n";
    return {};
}
Result<void> ConsoleUserInterface::onLoginComplete() {
    std::cout << "Login complete.\n";
    return {};
}
Result<void> ConsoleUserInterface::onTransact() { return {}; }
Result<void> ConsoleUserInterface::onTransactComplete() { return {}; }
Result<void> ConsoleUserInterface::onSign() { return {}; }
Result<void> ConsoleUserInterface::onSignComplete() { return {}; }
Result<void> ConsoleUserInterface::onBroadcast() { return {}; }
Result<void> ConsoleUserInterface::onBroadcastComplete() { return {}; }

Result<PromptResponse> ConsoleUserInterface::prompt(const PromptArgs& args, CancelToken token) {
    std::cout << args.title << "\n";
    if (args.body) {
        std::cout << *args.body << "\n";
    }
    for (const auto& element : args.elements) {
        if (element.label) {
            std::cout << "  - " << *element.label << "\n";
        }
        if (element.type == PromptElementType::link && element.data.is_object() &&
            element.data.contains("href")) {
            std::cout << "    " << element.data["href"].get<std::string>() << "\n";
        }
    }
    if (token.cancelled()) {
        return err(ErrorKind::Canceled, "Prompt cancelled");
    }
    return PromptResponse{};
}

void ConsoleUserInterface::status(const std::string& message) {
    std::cout << "[status] " << message << "\n";
}

}  // namespace dwarfkit
