#include <cstring>

#include "core/util.hpp"
#include "guard/rules.hpp"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/ui_helpers.h"
#include "ui/widgets.hpp"

namespace tb::ui {

void drawTransfer(AppState& state, Controller& controller) {
    const AccountRef* account = state.currentAccount();
    if (!account) {
        emptyState(Icon::Send, "No account selected",
                   "Add or select an account before sending tokens.");
        return;
    }
    const NetworkDef* network = state.currentNetwork();
    AccountData& data = state.accountData[state.accountKey(*account)];

    heading("Transfer");
    subtext("Token transfers run through the guard like everything else: whitelist "
            "evaluation, contract integrity check, then your signature.");
    vspace(10);

    static char to[64] = {};
    static char amount[64] = {};
    static char memo[256] = {};
    static char tokenContract[64] = "eosio.token";
    static char symbolBuf[16] = {};

    // Reset per-account symbol default.
    std::string coreSymbol = network ? network->coreSymbolCode() : "EOS";
    if (!symbolBuf[0]) std::snprintf(symbolBuf, sizeof symbolBuf, "%s", coreSymbol.c_str());

    float cardW = 560.0f;
    if (beginCard("xfer", cardW, true)) {
        kvRow("From", account->display(), true);

        // Token picker: core + registered tokens; picking one sets contract
        // and symbol together.
        static int tokenIdx = 0;
        if (tokenIdx >= static_cast<int>(data.snap.balances.size())) tokenIdx = 0;
        std::string balance;
        if (!data.snap.balances.empty()) {
            const BalanceView& picked =
                data.snap.balances[static_cast<size_t>(tokenIdx)];
            balance = picked.quantity;
            if (data.snap.balances.size() > 1) {
                ImGui::PushFont(fonts().uiSemi, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
                ImGui::TextUnformatted("Token");
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::SetNextItemWidth(280);
                if (ImGui::BeginCombo("##tokenpick", balance.c_str())) {
                    for (int i = 0; i < static_cast<int>(data.snap.balances.size()); ++i) {
                        const BalanceView& option =
                            data.snap.balances[static_cast<size_t>(i)];
                        std::string label = option.quantity + "  (" + option.contract + ")";
                        if (ImGui::Selectable(label.c_str(), tokenIdx == i)) {
                            tokenIdx = i;
                            std::snprintf(tokenContract, sizeof tokenContract, "%s",
                                          option.contract.c_str());
                            if (auto sp = option.quantity.find(' ');
                                sp != std::string::npos)
                                std::snprintf(symbolBuf, sizeof symbolBuf, "%s",
                                              option.quantity.substr(sp + 1).c_str());
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }
        if (!balance.empty()) kvRow("Available", balance, true);
        vspace(8);

        FieldOpts toOpts;
        toOpts.placeholder = "recipient account";
        toOpts.mono = true;
        static std::string toError;
        toOpts.error = toError.empty() ? nullptr : toError.c_str();
        textField("To", to, sizeof to, toOpts);

        // Address book: recognize saved recipients, warn on first-timers,
        // and catch the classic exchange-deposit-without-memo mistake.
        {
            const AccountRef* self = state.currentAccount();
            std::string chainId = self ? self->chainId : "";
            std::string toStr = toLower(trim(to));
            const Contact* known = nullptr;
            for (const auto& contact : state.vault.contacts)
                if (contact.actor == toStr &&
                    (contact.chainId.empty() || contact.chainId == chainId))
                    known = &contact;

            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::BeginCombo("##contacts", "contacts...")) {
                for (const auto& contact : state.vault.contacts) {
                    if (!contact.chainId.empty() && contact.chainId != chainId) continue;
                    ImGui::PushID(contact.actor.c_str());
                    std::string row =
                        contact.actor +
                        (contact.label.empty() ? "" : "  -  " + contact.label);
                    if (ImGui::Selectable(row.c_str(), false))
                        std::snprintf(to, sizeof to, "%s", contact.actor.c_str());
                    ::ui::HandOnHover();
                    ImGui::SameLine();
                    if (iconButton("##rmct", Icon::Trash, "Remove contact", col::Slate,
                                   11.0f))
                        controller.removeContact(contact.actor, contact.chainId);
                    ImGui::PopID();
                }
                if (state.vault.contacts.empty()) {
                    ImGui::PushFont(fonts().ui, kTextSm);
                    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                    ImGui::TextUnformatted("No saved recipients yet.");
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
                ImGui::EndCombo();
            }
            ::ui::HandOnHover();
            if (!toStr.empty() && !known) {
                ImGui::SameLine(0, 8);
                if (neonButton("SAVE CONTACT", BtnKind::Subtle, {120, 26}))
                    controller.addContact({toStr, "", chainId});
            }

            static const char* kExchangeDeposits[] = {
                "binancecleos", "binancewaxbp", "huobideposit", "okbtothemoon",
                "krakenkraken", "gateiowallet", "mxcexdeposit", "bybitdeposit",
                "kucoindoteos", "bitfinexdep1", "coinbasebase", "upbitdeposit"};
            bool exchange = false;
            for (const char* name : kExchangeDeposits) exchange |= toStr == name;

            if (exchange && !memo[0]) {
                ImGui::PushFont(fonts().ui, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
                ImGui::TextWrapped("This looks like an exchange deposit account. Sending "
                                   "WITHOUT the memo the exchange assigned you usually "
                                   "means lost funds.");
                ImGui::PopStyleColor();
                ImGui::PopFont();
            } else if (known) {
                ImGui::PushFont(fonts().ui, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Success));
                ImGui::Text("contact: %s", known->label.empty() ? known->actor.c_str()
                                                                : known->label.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            } else if (toStr.size() >= 3) {
                ImGui::PushFont(fonts().ui, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
                ImGui::TextWrapped("First time sending to this account - double-check "
                                   "every character. Transfers cannot be reversed.");
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
        }

        // Amount + symbol on one row (amount gets its own row on phones).
        bool narrow = layout().phone();
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::BeginGroup();
        {
            FieldOpts amtOpts;
            amtOpts.placeholder = "0.0000";
            amtOpts.mono = true;
            amtOpts.width = narrow ? avail : avail - 150.0f;
            static std::string amtError;
            amtOpts.error = amtError.empty() ? nullptr : amtError.c_str();
            textField("Amount", amount, sizeof amount, amtOpts);
        }
        ImGui::EndGroup();
        if (!narrow) ImGui::SameLine(0, 10);
        ImGui::BeginGroup();
        {
            FieldOpts symOpts;
            symOpts.mono = true;
            symOpts.width = 90.0f;
            textField("Symbol", symbolBuf, sizeof symbolBuf, symOpts);
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, 8);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 24);
        if (neonButton("MAX", BtnKind::Subtle, {44, 32}) && !balance.empty()) {
            if (auto sp = balance.find(' '); sp != std::string::npos) {
                std::snprintf(amount, sizeof amount, "%s", balance.substr(0, sp).c_str());
                std::snprintf(symbolBuf, sizeof symbolBuf, "%s",
                              balance.substr(sp + 1).c_str());
            }
        }

        FieldOpts memoOpts;
        memoOpts.placeholder = "optional note carried on-chain (public!)";
        textField("Memo", memo, sizeof memo, memoOpts);

        if (ImGui::TreeNodeEx("advanced", 0, "Advanced")) {
            FieldOpts tcOpts;
            tcOpts.mono = true;
            tcOpts.hint = "token contract account (eosio.token for the core token)";
            textField("Token contract", tokenContract, sizeof tokenContract, tcOpts);
            ImGui::TreePop();
        }
        vspace(12);

        // Build + validate the quantity: enforce the balance's precision when
        // the symbol matches the core balance.
        bool busy = state.busyTransfer;
        if (busy) {
            spinner(13.0f);
            ImGui::SameLine(0, 10);
            subtext("waiting for the signing pipeline...");
        } else if (neonButton("REVIEW & SEND", BtnKind::Primary, {cardW - 36, 46},
                              account->watch)) {
            std::string toStr = trim(to);
            std::string amountStr = trim(amount);
            std::string symbol = trim(symbolBuf);
            bool ok = true;
            if (toStr.empty() || toStr.size() > 13) {
                controller.toast(Toast::Error, "Enter a valid recipient account");
                ok = false;
            }
            // Normalize precision: pad the decimals to the balance's precision
            // if the symbol matches, else keep what the user typed.
            if (ok) {
                uint8_t precision = 4;
                if (!balance.empty()) {
                    if (auto parsed = guard::parseAsset(balance);
                        parsed && parsed->code == symbol)
                        precision = parsed->precision;
                }
                std::string normalized = amountStr;
                auto dot = normalized.find('.');
                if (dot == std::string::npos) {
                    if (precision > 0) {
                        normalized += ".";
                        normalized.append(precision, '0');
                    }
                } else {
                    size_t decimals = normalized.size() - dot - 1;
                    while (decimals < precision) {
                        normalized += "0";
                        ++decimals;
                    }
                }
                std::string quantity = normalized + " " + symbol;
                if (!guard::parseAsset(quantity)) {
                    controller.toast(Toast::Error, "Amount does not parse as a valid quantity");
                    ok = false;
                } else {
                    controller.sendTransfer(toStr, quantity, memo, trim(tokenContract));
                }
            }
        }
        if (account->watch) subtext("Watch-only account: signing is unavailable.");
    }
    endCard();
}

}  // namespace tb::ui
