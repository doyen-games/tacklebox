#include "guard/risk.hpp"

#include <algorithm>

#include "core/util.hpp"
#include "guard/rules.hpp"

namespace tb::guard {

const char* riskSeverityName(RiskSeverity s) {
    switch (s) {
        case RiskSeverity::Info: return "info";
        case RiskSeverity::Warn: return "warn";
        case RiskSeverity::Critical: return "critical";
    }
    return "info";
}

namespace {

bool memoLooksPhishy(const std::string& memoLower) {
    static const char* needles[] = {"http://", "https://", "www.",     "claim",
                                    "airdrop", "verify",   "giveaway", "swap to",
                                    "kyc",     "upgrade",  "migrate"};
    for (const char* n : needles)
        if (memoLower.find(n) != std::string::npos) return true;
    return false;
}

}  // namespace

std::vector<RiskFlag> analyze(const std::vector<RiskActionInput>& actions,
                              const RiskContext& context) {
    std::vector<RiskFlag> flags;
    auto add = [&](RiskSeverity sev, const char* code, std::string msg, size_t idx) {
        flags.push_back({sev, code, std::move(msg), idx});
    };

    std::optional<AssetValue> balance;
    if (context.coreBalance) balance = parseAsset(*context.coreBalance);

    for (size_t i = 0; i < actions.size(); ++i) {
        const auto& a = actions[i];

        // Authority surgery: the most dangerous thing a wallet can sign.
        if (a.contract == "eosio") {
            if (a.action == "updateauth" || a.action == "deleteauth" || a.action == "linkauth" ||
                a.action == "unlinkauth") {
                add(RiskSeverity::Critical, "perm-change",
                    "modifies account permissions (" + a.action +
                        "); a malicious version of this hands over the account",
                    i);
            } else if (a.action == "setcode" || a.action == "setabi") {
                add(RiskSeverity::Critical, "code-deploy",
                    "deploys contract code/ABI (" + a.action + ") to " +
                        a.data.value("account", std::string("?")),
                    i);
            } else if (a.action == "newaccount") {
                add(RiskSeverity::Info, "new-account", "creates a new account", i);
            } else if (a.action == "delegatebw" || a.action == "undelegatebw" ||
                       a.action == "buyram" || a.action == "buyrambytes" ||
                       a.action == "sellram" || a.action == "powerup") {
                add(RiskSeverity::Info, "resource-op", "resource management (" + a.action + ")",
                    i);
            }
        }

        if (a.contract == "eosio.msig") {
            add(RiskSeverity::Info, "msig",
                "multisig operation (" + a.action + "); the proposed transaction executes later",
                i);
        }

        // Token movements.
        if (a.action == "transfer" && a.data.is_object()) {
            std::string from = a.data.value("from", std::string());
            std::string quantityStr = a.data.value("quantity", std::string());
            std::string memo = a.data.value("memo", std::string());

            if (!memo.empty() && memoLooksPhishy(toLower(memo)))
                add(RiskSeverity::Warn, "memo-phish",
                    "memo contains link/claim language, a common scam pattern", i);

            if (from == context.signerActor && balance) {
                if (auto q = parseAsset(quantityStr);
                    q && q->code == balance->code && balance->amount > 0) {
                    double ratio =
                        static_cast<double>(q->amount) / static_cast<double>(balance->amount);
                    if (ratio >= 0.9)
                        add(RiskSeverity::Critical, "drain-transfer",
                            "sends " + quantityStr + ", nearly the entire " + balance->code +
                                " balance",
                            i);
                    else if (ratio >= 0.5)
                        add(RiskSeverity::Warn, "large-transfer",
                            "sends " + quantityStr + ", more than half the " + balance->code +
                                " balance",
                            i);
                }
            }
        }

        // Open approvals in NFT/token standards.
        if (a.action == "approve" || a.action == "approveall" || a.action == "open")
            add(RiskSeverity::Info, "approval",
                "grants an allowance/approval (" + a.contract + "::" + a.action + ")", i);

        if (!context.knownContracts.empty() && !context.knownContracts.count(a.contract) &&
            a.contract != "eosio" && a.contract != "eosio.token")
            add(RiskSeverity::Info, "first-contact",
                "first recorded interaction with contract " + a.contract, i);
    }

    std::stable_sort(flags.begin(), flags.end(), [](const RiskFlag& a, const RiskFlag& b) {
        return static_cast<int>(a.severity) > static_cast<int>(b.severity);
    });
    return flags;
}

}  // namespace tb::guard
