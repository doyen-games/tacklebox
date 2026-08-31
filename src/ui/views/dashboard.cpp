#include <cinttypes>

#include "core/util.hpp"
#include "guard/engine.hpp"
#include "guard/rules.hpp"
#include "ui/app_ui.hpp"
#include "ui/fx.hpp"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

std::string formatBytes(int64_t bytes) {
    char buf[48];
    if (bytes < 0) return "unlimited";
    if (bytes < 1024) {
        std::snprintf(buf, sizeof buf, "%" PRId64 " B", bytes);
    } else if (bytes < 1024 * 1024) {
        std::snprintf(buf, sizeof buf, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else if (bytes < 1024ll * 1024 * 1024) {
        std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024));
    } else {
        std::snprintf(buf, sizeof buf, "%.2f GB",
                      static_cast<double>(bytes) / (1024.0 * 1024 * 1024));
    }
    return buf;
}

std::string formatMicroseconds(int64_t us) {
    char buf[48];
    if (us < 0) return "unlimited";
    if (us < 1000) {
        std::snprintf(buf, sizeof buf, "%" PRId64 " us", us);
    } else if (us < 1000000) {
        std::snprintf(buf, sizeof buf, "%.1f ms", static_cast<double>(us) / 1000.0);
    } else {
        std::snprintf(buf, sizeof buf, "%.2f s", static_cast<double>(us) / 1e6);
    }
    return buf;
}

}  // namespace

void drawDashboard(AppState& state, Controller& controller) {
    const AccountRef* account = state.currentAccount();
    if (!account) {
        emptyState(Icon::Anchor, "No account on deck",
                   "Open the Vault page to add a key and link an account.");
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((w - 180) * 0.5f);
        if (neonButton("OPEN VAULT", BtnKind::Primary, {180, 42})) state.page = Page::Vault;
        return;
    }

    const std::string key = state.accountKey(*account);
    AccountData& data = state.accountData[key];
    const NetworkDef* network = state.currentNetwork();

    // --- header row ---------------------------------------------------------
    ImGui::PushFont(fonts().uiBold, kH1);
    ImGui::TextUnformatted(account->actor.c_str());
    ImGui::PopFont();
    ImGui::SameLine(0, 10);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
    ImGui::PushFont(fonts().mono, kMono);
    ImGui::TextUnformatted(("@" + account->permission).c_str());
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    float rx = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(rx + ImGui::GetContentRegionAvail().x - 36);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8);
    if (iconButton("##refresh", Icon::Refresh, "Refresh account data")) {
        controller.refreshAccount(true);
        controller.noteActivity();
    }
    if (account->watch) {
        badge("WATCH-ONLY", col::Warn);
        ImGui::SameLine();
        subtext("no signing key in the vault for this authority");
    }
    vspace(6);

    // --- balance hero -------------------------------------------------------
    if (beginCard("balance", 0, true)) {
        ImGui::PushFont(fonts().uiSemi, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
        ImGui::TextUnformatted(network ? ("LIQUID BALANCE  -  " + network->name).c_str()
                                       : "LIQUID BALANCE");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        vspace(2);

        if (data.loading && !data.loaded) {
            spinner(14.0f);
        } else if (!data.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
            ImGui::TextWrapped("%s", data.error.c_str());
            ImGui::PopStyleColor();
        } else {
            std::string balance = data.snap.coreBalance().empty() ? "-"
                                                                  : data.snap.coreBalance();
            // Split amount and symbol for typographic contrast.
            std::string amount = balance, symbol;
            if (auto sp = balance.find(' '); sp != std::string::npos) {
                amount = balance.substr(0, sp);
                symbol = balance.substr(sp + 1);
            }
            ImGui::PushFont(fonts().mono, kHero);
            ImGui::TextUnformatted(amount.c_str());
            ImGui::PopFont();
            if (!symbol.empty()) {
                ImGui::SameLine(0, 10);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 16);
                ImGui::PushFont(fonts().uiSemi, kTextLg);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Cyan));
                ImGui::TextUnformatted(symbol.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            // Registered tokens under the core balance.
            for (size_t i = 1; i < data.snap.balances.size(); ++i) {
                const BalanceView& token = data.snap.balances[i];
                monoText(token.quantity, col::Ice, kMono);
                ImGui::SameLine();
                ImGui::PushFont(fonts().ui, kMonoSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                ImGui::TextUnformatted(token.contract.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            if (data.loaded) {
                ImGui::PushFont(fonts().ui, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                ImGui::Text("updated %s ago", formatAgo(data.snap.fetchedAt).c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
        }
        vspace(4);
        if (neonButton("SEND", BtnKind::Primary, {130, 40}, account->watch))
            state.page = Page::Transfer;
        ImGui::SameLine(0, 10);
        if (neonButton("RECEIVE", BtnKind::Ghost, {130, 40})) ImGui::OpenPopup("##receive");

        ImGui::SetNextWindowSize({320, 0});
        if (ImGui::BeginPopup("##receive")) {
            ImGui::PushFont(fonts().uiSemi, kText);
            ImGui::TextUnformatted("Receive to this account");
            ImGui::PopFont();
            vspace(4);
            float qw = 240.0f;
            ImGui::SetCursorPosX((320 - qw) * 0.5f);
            drawQr(account->actor, qw);
            vspace(4);
            monoText(account->actor, col::Cyan, kTextLg);
            ImGui::SameLine();
            if (iconButton("##cpacct", Icon::Copy, "Copy account name", col::Steel, 14.0f))
                ImGui::SetClipboardText(account->actor.c_str());
            ImGui::EndPopup();
        }
    }
    endCard();
    vspace(12);

    // --- resources + guard status two-up (stacked on phones) ----------------
    float colW = pairWidth();
    if (beginCard("resources", colW)) {
        sectionTitle("Resources");
        const AccountSnapshot& snap = data.snap;
        resourceBar("CPU", static_cast<double>(snap.cpuUs.used),
                    static_cast<double>(snap.cpuUs.max),
                    formatMicroseconds(snap.cpuUs.used) + " / " +
                        formatMicroseconds(snap.cpuUs.max));
        resourceBar("NET", static_cast<double>(snap.netBytes.used),
                    static_cast<double>(snap.netBytes.max),
                    formatBytes(snap.netBytes.used) + " / " + formatBytes(snap.netBytes.max));
        resourceBar("RAM", static_cast<double>(snap.ramBytes.used),
                    static_cast<double>(snap.ramBytes.max),
                    formatBytes(snap.ramBytes.used) + " / " + formatBytes(snap.ramBytes.max));
    }
    endCard();
    maybeSameLine();
    if (beginCard("guardstat", colW)) {
        sectionTitle("Guard");
        int active = 0, stale = 0, autoRules = 0;
        for (const auto& rule : state.vault.rules) {
            if (rule.status == guard::RuleStatus::Active) ++active;
            if (rule.status == guard::RuleStatus::Stale) ++stale;
            if (rule.autoSign && rule.status == guard::RuleStatus::Active) ++autoRules;
        }
        kvRow("Active rules", std::to_string(active));
        kvRow("Auto-sign rules", std::to_string(autoRules) +
                                     (state.vault.security.allowAutoSign ? "" : "  (master off)"));
        if (stale > 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
            ImGui::PushFont(fonts().uiSemi, kText);
            ImGui::Text("%d rule%s suspended: contract changed", stale, stale == 1 ? "" : "s");
            ImGui::PopFont();
            ImGui::PopStyleColor();
            if (neonButton("REVIEW", BtnKind::Ghost, {110, 32})) state.page = Page::Whitelist;
        } else {
            kvRow("Integrity", "all pins verified");
        }
        kvRow("Auto-lock",
              state.vault.security.autoLockMinutes > 0
                  ? std::to_string(state.vault.security.autoLockMinutes) + " min"
                  : "off");
    }
    endCard();
    vspace(12);

    // --- pinned queries -------------------------------------------------------
    {
        std::vector<const PinnedQuery*> pins;
        for (const auto& pin : state.vault.pinnedQueries)
            if (pin.chainId.empty() || pin.chainId == account->chainId)
                pins.push_back(&pin);
        if (!pins.empty()) {
            float avail = ImGui::GetContentRegionAvail().x;
            int columns = static_cast<int>(avail / 260.0f);
            if (columns < 1) columns = 1;
            float pinW = (avail - static_cast<float>(columns - 1) * 12.0f) /
                         static_cast<float>(columns);
            int i = 0;
            for (const PinnedQuery* pin : pins) {
                if (i % columns != 0) ImGui::SameLine(0, 12);
                ++i;
                ImGui::PushID(pin->id.c_str());
                if (beginCard("pin", pinW)) {
                    ImGui::PushFont(fonts().uiSemi, kTextSm);
                    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                    ImGui::TextUnformatted(
                        pin->label.empty() ? (pin->contract + "/" + pin->table).c_str()
                                           : pin->label.c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                    ImGui::SameLine();
                    float endX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
                    ImGui::SetCursorPosX(endX - 26);
                    if (iconButton("##unpin", Icon::XCircle, "Unpin", col::Slate, 12.0f))
                        controller.removePinnedQuery(pin->id);

                    const PinnedData& pdata = state.pinnedData[pin->id];
                    if (!pdata.error.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
                        ImGui::TextWrapped("%s", pdata.error.c_str());
                        ImGui::PopStyleColor();
                    } else if (pdata.rows.is_null()) {
                        spinner(10.0f);
                    } else if (!pin->fieldPath.empty()) {
                        // Single-field stat tile via the guard's path lookup.
                        const json* value =
                            pdata.rows.is_array() && !pdata.rows.empty()
                                ? guard::lookupField(pdata.rows[0], pin->fieldPath)
                                : nullptr;
                        if (value) {
                            std::string text = value->is_string()
                                                   ? value->get<std::string>()
                                                   : value->dump();
                            ImGui::PushFont(fonts().mono, 24.0f);
                            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Cyan));
                            ImGui::TextWrapped("%s", text.c_str());
                            ImGui::PopStyleColor();
                            ImGui::PopFont();
                        } else {
                            subtext("field not found in row 0");
                        }
                    } else if (pdata.rows.is_array() && !pdata.rows.empty()) {
                        jsonTree(pdata.rows[0], "pinrow");
                    } else {
                        subtext("no rows");
                    }
                    if (pdata.fetchedAt) {
                        ImGui::PushFont(fonts().ui, kMonoSm);
                        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                        ImGui::Text("%s ago", formatAgo(pdata.fetchedAt).c_str());
                        ImGui::PopStyleColor();
                        ImGui::PopFont();
                    }
                }
                endCard();
                ImGui::PopID();
            }
            vspace(12);
        }
    }

    // --- recent signing activity --------------------------------------------
    if (beginCard("recent")) {
        sectionTitle("Recent activity");
        if (state.vault.audit.empty()) {
            subtext("Nothing signed yet. Activity from this vault will land here.");
        } else {
            int shown = 0;
            for (const auto& entry : state.vault.audit) {
                if (shown++ >= 6) break;
                ImGui::PushID(shown);
                statusDot(entry.approved ? col::Success : col::Danger, false);
                ImGui::SameLine(0, 10);
                ImGui::PushFont(fonts().mono, kMono);
                ImGui::TextUnformatted(entry.summary.c_str());
                ImGui::PopFont();
                ImGui::SameLine();
                ImGui::PushFont(fonts().ui, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                ImGui::Text("%s  -  %s ago", entry.signer.c_str(),
                            formatAgo(entry.time).c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::PopID();
            }
            if (state.vault.audit.size() > 6) {
                if (neonButton("FULL LOG", BtnKind::Subtle, {100, 30}))
                    state.page = Page::History;
            }
        }
    }
    endCard();

    // Kick a lazy refresh when the page shows stale data.
    if (!data.loading && !data.loaded && data.error.empty()) controller.refreshAccount(false);
}

}  // namespace tb::ui
