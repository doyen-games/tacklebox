#include <dwarfkit/plugins/transact/explorerlink.hpp>

namespace dwarfkit {

LocaleDefinitions TransactPluginExplorerLink::translations() const {
    static const json locales = json::parse(R"({
        "en": {
            "no-chain": "Unable to generate explorer link, no chain definition.",
            "no-explorer": "Unable to generate explorer link, chain definition doesnt defined an explorer.",
            "no-resolved": "Unable to generate explorer link, no resolved transaction.",
            "complete": "Transaction Complete",
            "click": "Click the button below to view the transaction in a block explorer to verify its status.",
            "visit": "Visit Explorer"
        }
    })");
    return locales;
}

void TransactPluginExplorerLink::register_(TransactContext& context) {
    context.addHook(
        TransactHookTypes::afterBroadcast,
        [this](TransactResult& result, TransactContext& ctx) -> Result<TransactHookResponseType> {
            if (!ctx.ui) {
                return TransactHookResponseType{};
            }
            // Retrieve translation helper from the UI, passing the app ID
            const auto t = ctx.ui->getTranslate(id());

            // Ensure the chain has an explorer defined on it
            if (!result.chain.explorer) {
                return err(ErrorKind::Plugin,
                           t("no-explorer",
                             {.defaultValue = "Unable to generate explorer link, chain definition "
                                              "doesnt defined an explorer.",
                              .values = {}}));
            }

            // Ensure we have a resolved transaction
            if (!result.resolved) {
                return err(ErrorKind::Plugin,
                           t("no-resolved",
                             {.defaultValue =
                                  "Unable to generate explorer link, no resolved transaction.",
                              .values = {}}));
            }

            DK_TRY(href, getExplorerLink(ctx, result.chain,
                                         result.resolved->transaction.id().hexString()));

            // Prompt the user with the link to view the transaction
            PromptArgs prompt;
            prompt.title = t("complete", {.defaultValue = "Transaction Complete", .values = {}});
            prompt.body = t("click",
                            {.defaultValue = "Click the button below to view the transaction in "
                                             "a block explorer to verify its status.",
                             .values = {}});
            PromptElement link;
            link.type = PromptElementType::link;
            link.data = json{{"button", true},
                             {"variant", "primary"},
                             {"label", t("visit", {.defaultValue = "Visit Explorer", .values = {}})},
                             {"href", href}};
            prompt.elements.push_back(std::move(link));
            DK_CHECK(ctx.ui->prompt(prompt, CancelToken()));
            return TransactHookResponseType{};
        });
}

Result<std::string> TransactPluginExplorerLink::getExplorerLink(
    TransactContext& context, const ChainDefinition& chain, const std::string& transaction) const {
    if (!context.ui) {
        return err(ErrorKind::Plugin, "Unable to generate explorer link, no ui.");
    }
    if (!chain.explorer) {
        const auto t = context.ui->getTranslate(id());
        return err(ErrorKind::Plugin,
                   t("no-explorer",
                     {.defaultValue = "Unable to generate explorer link, chain definition doesnt "
                                      "defined an explorer.",
                      .values = {}}));
    }
    return chain.explorer->url(transaction);
}

}  // namespace dwarfkit
