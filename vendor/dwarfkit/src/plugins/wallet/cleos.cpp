#include <dwarfkit/plugins/wallet/cleos.hpp>

#include <dwarfkit/abicache.hpp>

namespace dwarfkit {

WalletPluginCleos::WalletPluginCleos() {
    config_ = {.requiresChainSelect = true,
               .requiresPermissionSelect = false,
               .requiresPermissionEntry = true};
    metadata_ = WalletPluginMetadata::from(
        json{{"name", "cleos"},
             {"description", "Copy and paste the transactions to sign in cleos."},
             {"homepage", "https://github.com/antelopeio/leap"},
             {"download", "https://github.com/antelopeio/leap"}});
}

Result<WalletPluginLoginResponse> WalletPluginCleos::login(LoginContext& context) {
    if (!context.chain) {
        return err(ErrorKind::Invalid, "The cleos wallet plugin requires a chain to be selected.");
    }
    if (!context.permissionLevel) {
        return err(ErrorKind::Invalid,
                   "The cleos wallet plugin requires a permissionLevel to be specified.");
    }
    return WalletPluginLoginResponse{context.chain->id, *context.permissionLevel, std::nullopt};
}

Result<WalletPluginSignResponse> WalletPluginCleos::sign(const ResolvedSigningRequest& resolved,
                                                         TransactContext& context) {
    if (!context.ui) {
        return err(ErrorKind::Invalid, "The cleos wallet plugin requires a UI to be provided.");
    }
    // During the signing process, add a hook to display the cleos command to
    // the user
    const Transaction transaction = resolved.transaction;
    context.addHook(
        TransactHookTypes::afterSign,
        TransactHookImmutable([transaction](TransactResult&, TransactContext& ctx)
                                  -> Result<TransactHookResponseType> {
            // Decode the transaction to be human readable
            json actions = json::array();
            for (const Action& action : transaction.actions) {
                DK_TRY(abi, ctx.abiCache->getAbi(action.account));
                DK_TRY(data, action.decodeData(abi));
                actions.push_back(json{{"account", action.account.toString()},
                                       {"name", action.name.toString()},
                                       {"authorization",
                                        Serializer::objectify(action.authorization)},
                                       {"data", data}});
            }
            json decoded = Serializer::objectify(transaction);
            decoded["actions"] = actions;
            // Create the cleos command that will be used to sign the
            // transaction
            const std::string command = "cleos -u " + ctx.chain.url + " push transaction '" +
                                        decoded.dump(4) + "'";
            // Prompt the user with the command to sign the transaction
            if (ctx.ui) {
                (void)ctx.ui->prompt(
                    {.title = "Sign with cleos",
                     .body = "Copy the command to sign the transaction using cleos.",
                     .elements =
                         {PromptElement{PromptElementType::textarea, std::nullopt,
                                        json{{"content", command}}},
                          // the clipboard onClick has no meaning here; the
                          // element remains for UIs that can render one
                          PromptElement{PromptElementType::button, std::nullopt,
                                        json{{"label", "Copy to clipboard"}}}}},
                    CancelToken{});
            }
            return TransactHookResponseType{};
        }));
    // Return no signatures, as the cleos command will be used to sign the
    // transaction
    return WalletPluginSignResponse{};
}

}  // namespace dwarfkit
