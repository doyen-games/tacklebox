// Port of wharfkit/transact-plugin-finality-checker and
// transact-plugin-finality-callback. setTimeout scheduling becomes blocking
// waits inside the afterBroadcast hook (run transact on a worker thread); the
// checker's never-resolving promise returns after the final prompt instead
// (see DIVERGENCES.md). The delays are configurable for tests.
#pragma once

#include <chrono>
#include <functional>

#include <dwarfkit/session.hpp>

namespace dwarfkit {

// Poll get_transaction_status until the transaction is IRREVERSIBLE. 404s are
// retried up to three times; a 500 means the API cannot be used with the
// finality plugins.
Result<api::v1::GetTransactionStatusResponse> waitForFinality(
    const Checksum256& transactionId, TransactContext& context,
    std::chrono::milliseconds pollInterval = std::chrono::milliseconds(5000),
    CancelToken token = {});

struct FinalityCheckerOptions {
    bool logging = false;
    // 3 minutes upstream; configurable so tests can skip the wait
    std::chrono::milliseconds startCheckingAfter = std::chrono::milliseconds(180000);
    std::chrono::milliseconds pollInterval = std::chrono::milliseconds(5000);
};

class TransactPluginFinalityChecker : public AbstractTransactPlugin {
public:
    explicit TransactPluginFinalityChecker(const FinalityCheckerOptions& options = {});

    std::string id() const override { return "transact-plugin-finality-checker"; }
    LocaleDefinitions translations() const override;
    void register_(TransactContext& context) override;

    bool logging = false;
    std::chrono::milliseconds startCheckingAfter;
    std::chrono::milliseconds pollInterval;

    void log(const std::string& message) const;
};

struct FinalityCallbackOptions {
    std::function<void(const api::v1::GetTransactionStatusResponse&)> onFinalityCallback;
    // 2.5 minutes upstream; configurable so tests can skip the wait
    std::chrono::milliseconds finalityCheckDelay = std::chrono::milliseconds(150000);
    std::chrono::milliseconds pollInterval = std::chrono::milliseconds(5000);
    bool logging = false;
};

class TransactPluginFinalityCallback : public AbstractTransactPlugin {
public:
    explicit TransactPluginFinalityCallback(const FinalityCallbackOptions& options);

    std::string id() const override { return "transact-plugin-finality-callback"; }
    LocaleDefinitions translations() const override;
    void register_(TransactContext& context) override;

    std::function<void(const api::v1::GetTransactionStatusResponse&)> onFinalityCallback;
    std::chrono::milliseconds finalityCheckDelay;
    std::chrono::milliseconds pollInterval;
    bool logging = false;

    void log(const std::string& message) const;
};

}  // namespace dwarfkit
