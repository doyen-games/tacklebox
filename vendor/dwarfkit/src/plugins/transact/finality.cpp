#include <dwarfkit/plugins/transact/finality.hpp>

#include <cstdio>

namespace dwarfkit {

Result<api::v1::GetTransactionStatusResponse> waitForFinality(
    const Checksum256& transactionId, TransactContext& context,
    std::chrono::milliseconds pollInterval, CancelToken token) {
    int retries = 0;
    while (true) {
        if (token.cancelled()) {
            return err(ErrorKind::Canceled, "Cancelled");
        }
        const auto response = context.client->v1.chain.get_transaction_status(transactionId);
        if (response) {
            if (response->state == "IRREVERSIBLE") {
                return *response;
            }
            // not final yet; poll again after the interval
            if (token.waitFor(pollInterval)) {
                return err(ErrorKind::Canceled, "Cancelled");
            }
            continue;
        }
        const Error& error = response.error();
        const int status = error.kind == ErrorKind::Api ? error.code : 0;
        if (status == 404 && retries < 3) {
            retries++;
            if (token.waitFor(pollInterval)) {
                return err(ErrorKind::Canceled, "Cancelled");
            }
            continue;
        }
        if (status == 500) {
            return err(ErrorKind::Plugin,
                       "This API node cannot be used with the finality checker plugin. Full "
                       "Error: " +
                           error.message);
        }
        return err(error);
    }
}

// ---- checker ---------------------------------------------------------------

TransactPluginFinalityChecker::TransactPluginFinalityChecker(
    const FinalityCheckerOptions& options)
    : logging(options.logging),
      startCheckingAfter(options.startCheckingAfter),
      pollInterval(options.pollInterval) {}

LocaleDefinitions TransactPluginFinalityChecker::translations() const {
    static const json locales = json::parse(R"({
        "en": {
            "resolved_request_not_returned": "Resolved Request not returned on afterBroadcast hook. This value is needed for the Finality Callback plugin to work.",
            "reversible": {
                "title": "Transaction is not yet final",
                "body": "Your transaction has been broadcasted to the network, but is still reversible.",
                "countdown-label": "Finality expected in:"
            },
            "title-final": "Transaction is final",
            "body-final": "Your transaction has been broadcasted to the network and is now irrevirsible."
        }
    })");
    return locales;
}

void TransactPluginFinalityChecker::log(const std::string& message) const {
    if (logging) {
        std::fprintf(stderr, "TransactPluginFinalityChecker, LOG: %s\n", message.c_str());
    }
}

void TransactPluginFinalityChecker::register_(TransactContext& context) {
    context.addHook(
        TransactHookTypes::afterBroadcast,
        [this](TransactResult& result, TransactContext& ctx) -> Result<TransactHookResponseType> {
            if (!ctx.ui) {
                return err(ErrorKind::Plugin, "UI not available");
            }
            const auto t = ctx.ui->getTranslate(id());
            if (!result.resolved) {
                return err(ErrorKind::Plugin,
                           t("resolved_request_not_returned",
                             {.defaultValue =
                                  "Resolved Request not returned on afterBroadcast hook. This "
                                  "value is needed for the Finality Callback plugin to work.",
                              .values = {}}));
            }

            // Prompt the user that the transaction is not yet final
            PromptArgs prompt;
            prompt.title = t("reversible.title",
                             {.defaultValue = "Transaction is not yet final", .values = {}});
            prompt.body =
                t("reversible.body",
                  {.defaultValue = "Your transaction has been broadcasted to the network, but is "
                                   "still reversible.",
                   .values = {}});
            PromptElement countdown;
            countdown.type = PromptElementType::countdown;
            countdown.data = json{
                {"label", t("reversible.countdown-label",
                            {.defaultValue = "Finality expected in:", .values = {}})}};
            prompt.elements.push_back(std::move(countdown));
            (void)ctx.ui->prompt(prompt, CancelToken());

            // Wait, then poll for finality
            CancelToken token;
            (void)token.waitFor(startCheckingAfter);
            log("Checking transaction finality");
            const auto finality =
                waitForFinality(result.resolved->transaction.id(), ctx, pollInterval);
            if (!finality) {
                log("Error while checking transaction finality: " + finality.error().message);
                return TransactHookResponseType{};
            }
            log("Transaction finality reached");
            PromptArgs finalPrompt;
            finalPrompt.title =
                t("title-final", {.defaultValue = "Transaction is final", .values = {}});
            finalPrompt.body =
                t("body-final",
                  {.defaultValue = "Your transaction has been broadcasted to the network and is "
                                   "now irrevirsible.",
                   .values = {}});
            (void)ctx.ui->prompt(finalPrompt, CancelToken());
            return TransactHookResponseType{};
        });
}

// ---- callback --------------------------------------------------------------

TransactPluginFinalityCallback::TransactPluginFinalityCallback(
    const FinalityCallbackOptions& options)
    : onFinalityCallback(options.onFinalityCallback),
      finalityCheckDelay(options.finalityCheckDelay),
      pollInterval(options.pollInterval),
      logging(options.logging) {}

LocaleDefinitions TransactPluginFinalityCallback::translations() const {
    static const json locales = json::parse(R"({
        "en": {
            "resolved_request_not_returned": "Resolved Request not returned on afterBroadcast hook. This value is needed for the Finality Callback plugin to work."
        }
    })");
    return locales;
}

void TransactPluginFinalityCallback::log(const std::string& message) const {
    if (logging) {
        std::fprintf(stderr, "TransactPluginFinalityChecker, LOG: %s\n", message.c_str());
    }
}

void TransactPluginFinalityCallback::register_(TransactContext& context) {
    context.addHook(
        TransactHookTypes::afterBroadcast,
        [this](TransactResult& result, TransactContext& ctx) -> Result<TransactHookResponseType> {
            if (!result.resolved) {
                return err(ErrorKind::Plugin,
                           "Resolved Request not returned on afterBroadcast hook. This value is "
                           "needed for the Finality Callback plugin to work.");
            }
            CancelToken token;
            (void)token.waitFor(finalityCheckDelay);
            log("Checking transaction finality");
            const auto finality =
                waitForFinality(result.resolved->transaction.id(), ctx, pollInterval);
            if (!finality) {
                log("Error while checking transaction finality: " + finality.error().message);
                return TransactHookResponseType{};
            }
            log("Transaction finality reached");
            if (onFinalityCallback) {
                onFinalityCallback(*finality);
            }
            return TransactHookResponseType{};
        });
}

}  // namespace dwarfkit
