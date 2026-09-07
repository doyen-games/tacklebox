// Dashboard: a customizable board of tiles. Drag any tile's grip (or drop
// onto a card) to rearrange; the order persists in the vault. Tiles can be
// unpinned here and re-added from the ADD TILE palette or pinned from their
// home pages (RAM market, chain status, contract table pins...).
#include <algorithm>
#include <cfloat>
#include <cinttypes>
#include <cstdlib>

#include "app/account_util.hpp"
#include "tb_version.h"
#include "chain/prices.hpp"
#include "core/util.hpp"
#include "guard/engine.hpp"
#include "guard/rules.hpp"
#include "ui/app_ui.hpp"
#include "ui/fx.hpp"
#include "ui/layout.hpp"
#include "ui/qa.hpp"
#include "ui/ui_helpers.h"
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

std::string formatEta(int64_t sec) {
    if (sec <= 0) return "due now";
    char buf[48];
    if (sec < 3600)
        std::snprintf(buf, sizeof buf, "in %" PRId64 "m", (sec + 59) / 60);
    else if (sec < 86400)
        std::snprintf(buf, sizeof buf, "in %" PRId64 "h %" PRId64 "m", sec / 3600,
                      (sec % 3600) / 60);
    else
        std::snprintf(buf, sizeof buf, "in %" PRId64 "d %" PRId64 "h", sec / 86400,
                      (sec % 86400) / 3600);
    return buf;
}

// "12.3456 WAX" against the oracle map -> {unit usd, position usd}.
struct PricedBalance {
    double unit = 0.0;
    double value = 0.0;
    bool priced = false;
};
PricedBalance priceBalance(const AppState& state, const BalanceView& balance) {
    PricedBalance out;
    auto space = balance.quantity.find(' ');
    if (space == std::string::npos) return out;
    auto it = state.prices.usd.find(
        priceKey(balance.contract, balance.quantity.substr(space + 1)));
    if (it == state.prices.usd.end()) return out;
    out.unit = it->second;
    out.value = std::strtod(balance.quantity.c_str(), nullptr) * it->second;
    out.priced = true;
    return out;
}

// --- tile catalog ------------------------------------------------------------

struct TileInfo {
    const char* kind;
    const char* title;
    int span;
};
constexpr TileInfo kTileCatalog[] = {
    {"balance", "LIQUID BALANCE", 2},   {"resources", "RESOURCES", 1},
    {"guard", "GUARD", 1},              {"activity", "RECENT ACTIVITY", 2},
    {"ram", "RAM MARKET", 1},           {"chaininfo", "CHAIN STATUS", 1},
    {"prices", "PRICES", 1},            {"schedules", "AUTOPILOT", 1},
    // One per remaining sidebar page (Explore, Resources, Whitelist, Autopilot
    // and History are the tiles above), each a summary plus a jump.
    {"transfer", "TRANSFER", 1},        {"assets", "ASSETS", 1},
    {"contracts", "CONTRACTS", 1},      {"governance", "GOVERNANCE", 1},
    {"msig", "MULTISIG", 1},            {"vault", "VAULT", 1},
    {"create", "CREATE ACCOUNT", 1},    {"settings", "SETTINGS", 1},
};

const TileInfo* tileInfo(const std::string& kind) {
    for (const auto& info : kTileCatalog)
        if (kind == info.kind) return &info;
    return nullptr;
}

// --- tile bodies -------------------------------------------------------------

// The jump every page tile ends with.
void openPageButton(const char* label, AppState& state, Page page, bool disabled = false) {
    if (neonButton(label, BtnKind::Subtle, {150, 30}, disabled)) state.page = page;
}

void drawBalanceTile(AppState& state, Controller& controller, const AccountRef& account,
                     AccountData& data, const NetworkDef* network) {
    if (data.loading && !data.loaded) {
        spinner(14.0f);
        return;
    }
    if (!data.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextWrapped("%s", data.error.c_str());
        ImGui::PopStyleColor();
        return;
    }
    // Amounts on this card show at most four decimals (the chain's full
    // precision stays in the snapshot for anything that transacts).
    std::string balance =
        data.snap.coreBalance().empty() ? "-" : acct::displayAsset(data.snap.coreBalance());
    std::string amount = balance, symbol;
    if (auto sp = balance.find(' '); sp != std::string::npos) {
        amount = balance.substr(0, sp);
        symbol = balance.substr(sp + 1);
    }
    ImGui::PushFont(fonts().mono, kHero);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Amount));
    ImGui::TextUnformatted(amount.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (!symbol.empty()) {
        ImGui::SameLine(0, 10);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 16);
        ImGui::PushFont(fonts().uiSemi, kTextLg);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Ticker));
        ImGui::TextUnformatted(symbol.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    // Live USD line for the core position (oracle-driven, display-only).
    double portfolioUsd = 0.0;
    bool anyPriced = false;
    if (!data.snap.balances.empty()) {
        PricedBalance core = priceBalance(state, data.snap.balances[0]);
        if (core.priced) {
            portfolioUsd += core.value;
            anyPriced = true;
            ImGui::PushFont(fonts().mono, kText);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::CyanDim));
            ImGui::TextUnformatted(formatUsd(core.value).c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::SameLine(0, 8);
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
            ImGui::Text("at %s / %s", formatUsd(core.unit).c_str(), symbol.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
    }
    // The whole position: available stays the headline number above; this
    // line adds staked + delegated + refunding and their sum.
    {
        const NetworkDef* net = state.currentNetwork();
        auto breakdown =
            acct::stakeBreakdown(data.snap.raw, net ? net->coreSymbol : "4,EOS");
        if (breakdown.any) {
            bool phone = layout().phone();
            auto part = [&](const char* label, const std::string& asset, bool first) {
                if (!first) {
                    if (phone)
                        ImGui::Dummy({0, 0});
                    else
                        ImGui::SameLine(0, 12);
                }
                ImGui::PushFont(fonts().ui, kMonoSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::SameLine(0, 5);
                assetText(acct::displayAsset(asset), kMonoSm, /*dim=*/true);
            };
            part("total", breakdown.total, true);
            part("staked", breakdown.stakedSelf, false);
            part("delegated", breakdown.stakedDelegated, false);
            if (breakdown.refunding.rfind("0.", 0) != 0)
                part("refunding", breakdown.refunding, false);
            vspace(2);
        }
    }

    // Registered tokens under the core balance, priced where known.
    if (data.snap.balances.size() > 1 &&
        ImGui::BeginTable("##tokens", 4, ImGuiTableFlags_SizingFixedFit |
                                             ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("qty", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("contract", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("price", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed);
        for (size_t i = 1; i < data.snap.balances.size(); ++i) {
            const BalanceView& token = data.snap.balances[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            assetText(acct::displayAsset(token.quantity), kMono);
            ImGui::TableNextColumn();
            ImGui::PushFont(fonts().ui, kMonoSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
            ImGui::TextUnformatted(token.contract.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            PricedBalance priced = priceBalance(state, token);
            ImGui::TableNextColumn();
            if (priced.priced) {
                portfolioUsd += priced.value;
                anyPriced = true;
                monoText(formatUsd(priced.unit), col::Slate, kMonoSm);
            }
            ImGui::TableNextColumn();
            if (priced.priced) monoText(formatUsd(priced.value), col::CyanDim, kMonoSm);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (anyPriced) {
        ImGui::PushFont(fonts().ui, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
        ImGui::Text("portfolio = %s", formatUsd(portfolioUsd).c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    } else if (!state.prices.error.empty()) {
        ImGui::PushFont(fonts().ui, kMonoSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
        ImGui::Text("oracle: %s", state.prices.error.c_str());
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
    vspace(4);
    if (neonButton("SEND", BtnKind::Primary, {130, 40}, account.watch))
        state.page = Page::Transfer;
    ImGui::SameLine(0, 10);
    if (neonButton("RECEIVE", BtnKind::Ghost, {130, 40}) || qa::forceOpen("receive"))
        ImGui::OpenPopup("##receive");
    ImGui::SetNextWindowSize({320, 0});
    if (qa::active())
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                ImGuiCond_Appearing, {0.5f, 0.5f});
    if (ImGui::BeginPopup("##receive")) {
        ImGui::PushFont(fonts().uiSemi, kText);
        ImGui::TextUnformatted("Receive to this account");
        ImGui::PopFont();
        vspace(4);
        float qw = 240.0f;
        ImGui::SetCursorPosX((320 - qw) * 0.5f);
        drawQr(account.actor, qw);
        vspace(4);
        monoText(account.actor, col::Cyan, kTextLg);
        ImGui::SameLine();
        if (iconButton("##cpacct", Icon::Copy, "Copy account name", col::Steel, 14.0f))
            ImGui::SetClipboardText(account.actor.c_str());
        ImGui::EndPopup();
    }
    (void)network;
    (void)controller;
}

void drawResourcesTile(AccountData& data) {
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

void drawGuardTile(AppState& state) {
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
    if (stale == 0) openPageButton("OPEN WHITELIST", state, Page::Whitelist);
}

void drawActivityTile(AppState& state) {
    if (state.vault.audit.empty()) {
        subtext("Nothing signed yet. Activity from this vault will land here.");
        openPageButton("OPEN HISTORY", state, Page::History);
        return;
    }
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
        ImGui::Text("%s  -  %s ago", entry.signer.c_str(), formatAgo(entry.time).c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::PopID();
    }
    openPageButton("OPEN HISTORY", state, Page::History);
}

void drawRamTile(AppState& state, Controller& controller) {
    controller.loadRamMarket(false);
    const auto& rv = state.resources;
    if (rv.ram.fetchedAt == 0) {
        if (!rv.ramError.empty())
            subtext(rv.ramError.c_str());
        else
            spinner(11.0f);
        return;
    }
    ImGui::PushFont(fonts().mono, 24.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Cyan));
    ImGui::TextUnformatted(rv.ram.pricePerKb.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    subtext("per KB, before the 0.5% fee");
    kvRow("For sale", formatBytes(rv.ram.baseBytes));
    if (neonButton("TRADE RAM", BtnKind::Subtle, {110, 30}))
        state.page = Page::Resources;
}

void drawChainInfoTile(AppState& state, Controller& controller) {
    controller.exploreOverview(false);
    const auto& ex = state.explore;
    if (ex.info.is_null()) {
        if (!ex.overviewError.empty())
            subtext(ex.overviewError.c_str());
        else
            spinner(11.0f);
        return;
    }
    kvRow("Head block", std::to_string(ex.info.value("head_block_num", 0)), true);
    kvRow("Irreversible", std::to_string(ex.info.value("last_irreversible_block_num", 0)),
          true);
    kvRow("Producer", ex.info.value("head_block_producer", std::string("-")), true);
    if (neonButton("EXPLORE", BtnKind::Subtle, {100, 30})) state.page = Page::Explore;
}

void drawPricesTile(AppState& state, Controller& controller, const AccountRef& account,
                    const NetworkDef* network) {
    if (!network || network->oracle.provider == static_cast<int>(OracleProvider::Off)) {
        subtext("No price oracle for this chain. Pick one in Settings.");
        return;
    }
    AccountData& data = state.accountData[state.accountKey(account)];
    std::string code = network->coreSymbolCode();
    auto it = state.prices.usd.find(priceKey("eosio.token", code));
    if (it != state.prices.usd.end()) {
        ImGui::PushFont(fonts().mono, 24.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Cyan));
        ImGui::TextUnformatted((formatUsd(it->second) + " / " + code).c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    } else if (state.prices.loading) {
        spinner(11.0f);
    } else if (!state.prices.error.empty()) {
        subtext(("oracle: " + state.prices.error).c_str());
    }
    double total = 0.0;
    bool any = false;
    for (const auto& balance : data.snap.balances) {
        PricedBalance priced = priceBalance(state, balance);
        if (priced.priced) {
            total += priced.value;
            any = true;
        }
    }
    if (any) kvRow("Portfolio", formatUsd(total), true);
    kvRow("Source",
          oracleProviderName(static_cast<OracleProvider>(network->oracle.provider)));
    if (state.prices.fetchedAt)
        kvRow("Updated", formatAgo(state.prices.fetchedAt) + " ago");
    (void)controller;
}

void drawSchedulesTile(AppState& state) {
    std::vector<const Schedule*> upcoming;
    for (const auto& schedule : state.vault.schedules)
        if (schedule.enabled && schedule.nextRunAt > 0) upcoming.push_back(&schedule);
    std::sort(upcoming.begin(), upcoming.end(),
              [](const Schedule* a, const Schedule* b) { return a->nextRunAt < b->nextRunAt; });
    if (upcoming.empty()) {
        subtext("No armed schedules.");
        if (neonButton("OPEN AUTOPILOT", BtnKind::Subtle, {140, 30}))
            state.page = Page::Autopilot;
        return;
    }
    int shown = 0;
    int64_t now = nowSec();
    for (const Schedule* schedule : upcoming) {
        if (shown++ >= 3) break;
        ImGui::PushID(shown);
        ImGui::PushFont(fonts().uiSemi, kText);
        ImGui::TextUnformatted(schedule->label.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::PushFont(fonts().mono, kMonoSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::CyanDim));
        ImGui::TextUnformatted(formatEta(schedule->nextRunAt - now).c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::PopID();
    }
    if (neonButton("OPEN AUTOPILOT", BtnKind::Subtle, {140, 30}))
        state.page = Page::Autopilot;
}

void drawPinTile(AppState& state, const PinnedQuery& pin) {
    const PinnedData& pdata = state.pinnedData[pin.id];
    if (!pdata.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextWrapped("%s", pdata.error.c_str());
        ImGui::PopStyleColor();
    } else if (pdata.rows.is_null()) {
        spinner(10.0f);
    } else if (!pin.fieldPath.empty()) {
        const json* value = pdata.rows.is_array() && !pdata.rows.empty()
                                ? guard::lookupField(pdata.rows[0], pin.fieldPath)
                                : nullptr;
        if (value) {
            std::string text =
                value->is_string() ? value->get<std::string>() : value->dump();
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

// Header row inside a tile card, on one line: grip, title, unpin at right.
// Returns the drop-source index when a row is dropped on the grip.
// --- one tile per sidebar page ---------------------------------------------

void drawTransferTile(AppState& state, const AccountRef& account) {
    if (state.vault.contacts.empty()) {
        subtext("Send tokens to any account; saved contacts land here.");
    } else {
        int shown = 0;
        for (const auto& contact : state.vault.contacts) {
            if (shown++ >= 3) break;
            ImGui::PushID(shown);
            kvRow(contact.label.empty() ? contact.actor.c_str() : contact.label.c_str(),
                  contact.actor, true);
            ImGui::PopID();
        }
        if (state.vault.contacts.size() > 3)
            subtext(("+ " + std::to_string(state.vault.contacts.size() - 3) + " more contacts")
                        .c_str());
    }
    if (account.watch) subtext("Watch-only: no signing key for this account.");
    openPageButton("OPEN TRANSFER", state, Page::Transfer);
}

void drawAssetsTile(AppState& state, const AccountRef& account) {
    const AssetsViewState& av = state.assets;
    const bool mine = av.owner == account.actor;
    if (mine && av.assets.is_object() && av.assets.contains("data") &&
        av.assets["data"].is_array()) {
        kvRow("Loaded", std::to_string(av.assets["data"].size()) + " NFTs", true);
        if (av.fetchedAt) kvRow("Updated", formatAgo(av.fetchedAt) + " ago");
    } else if (mine && av.loading) {
        spinner(11.0f);
    } else if (mine && !av.error.empty()) {
        subtext(av.error.c_str());
    } else {
        subtext("NFTs from the Atomic Assets API load on the Assets page.");
    }
    openPageButton("OPEN ASSETS", state, Page::Assets);
}

void drawContractsTile(AppState& state) {
    kvRow("Saved contracts", std::to_string(state.vault.savedContracts.size()));
    kvRow("Saved actions", std::to_string(state.vault.savedActions.size()));
    kvRow("Pinned tables", std::to_string(state.vault.pinnedQueries.size()));
    if (!state.contracts.account.empty()) kvRow("Inspecting", state.contracts.account, true);
    openPageButton("OPEN CONTRACTS", state, Page::Contracts);
}

void drawGovernanceTile(AppState& state, AccountData& data) {
    const json& raw = data.snap.raw;
    if (data.loaded && raw.contains("voter_info") && raw["voter_info"].is_object()) {
        const json& voter = raw["voter_info"];
        const std::string proxy = voter.value("proxy", std::string());
        const size_t producers = voter.contains("producers") && voter["producers"].is_array()
                                     ? voter["producers"].size()
                                     : 0;
        if (!proxy.empty())
            kvRow("Vote", "proxied to " + proxy, true);
        else if (producers > 0)
            kvRow("Vote", std::to_string(producers) + " producer" + (producers == 1 ? "" : "s"));
        else
            kvRow("Vote", "not voting");
    } else if (data.loading) {
        spinner(11.0f);
    } else {
        subtext("Vote for producers or pick a proxy on the Governance page.");
    }
    openPageButton("OPEN GOVERNANCE", state, Page::Governance);
}

void drawMsigTile(AppState& state, const AccountRef& account) {
    const MsigViewState& mv = state.msig;
    const bool mine = mv.proposer == account.actor;
    if (mine && mv.proposals.is_object() && mv.proposals.contains("rows") &&
        mv.proposals["rows"].is_array()) {
        kvRow("Open proposals", std::to_string(mv.proposals["rows"].size()));
    } else if (mine && mv.loading) {
        spinner(11.0f);
    } else {
        subtext("Proposals load on the Multisig page.");
    }
    kvRow("Templates", std::to_string(state.vault.msigTemplates.size()));
    openPageButton("OPEN MULTISIG", state, Page::Msig);
}

void drawVaultTile(AppState& state) {
    kvRow("Keys", std::to_string(state.vault.keys.size()));
    kvRow("Accounts", std::to_string(state.vault.accounts.size()));
    kvRow("Networks", std::to_string(state.vault.networks.size()));
    kvRow("Last backup", state.vault.lastBackupAt > 0
                             ? formatAgo(state.vault.lastBackupAt) + " ago"
                             : std::string("never"));
    openPageButton("OPEN VAULT", state, Page::Vault);
}

void drawCreateTile(AppState& state) {
    subtext("Create an on-chain account (name, keys, RAM, stake) or port one in.");
    kvRow("Keys in the vault", std::to_string(state.vault.keys.size()));
    openPageButton("OPEN CREATE", state, Page::CreateAccount);
}

void drawSettingsTile(AppState& state, const NetworkDef* network) {
    if (network) {
        kvRow("Network", network->name + (network->testnet ? " (testnet)" : ""));
        kvRow("RPC endpoints", std::to_string(network->rpc.nodes.size()));
    }
    kvRow("Version", TB_VERSION, true);
    openPageButton("OPEN SETTINGS", state, Page::Settings);
}

int drawTileHeader(const std::string& title, int index, std::string* removeKind,
                   const std::string& kind) {
    int dropped = dragGrip("##dashtiles", index, title.c_str());
    ImGui::SameLine(0, 8);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    ImGui::PushFont(fonts().uiSemi, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
    float endX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(endX - ::ui::S(22.0f));
    if (iconButton("##unpin", Icon::XCircle, "Remove from dashboard", col::Slate, 12.0f))
        *removeKind = kind;
    ::ui::VSpace(0.2f);
    return dropped;
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
        controller.refreshPrices(true);
        controller.noteActivity();
    }
    if (account->watch) {
        badge("WATCH-ONLY", col::Warn);
        ImGui::SameLine();
        subtext("no signing key in the vault for this authority");
    }
    vspace(6);

    // --- the board ----------------------------------------------------------
    const std::vector<DashTile>& tiles = state.vault.dashboardTiles;
    int moveFrom = -1, moveTo = -1;
    std::string removeKind;
    const bool phone = layout().phone();
    float halfW = pairWidth();

    // Draws tile i at the given card width; records drops and removals.
    auto drawOneTile = [&](size_t i, float cardW) {
        const DashTile& tile = tiles[i];
        std::string title;
        const PinnedQuery* pin = nullptr;
        if (startsWith(tile.kind, "pin:")) {
            std::string id = tile.kind.substr(4);
            for (const auto& query : state.vault.pinnedQueries)
                if (query.id == id) pin = &query;
            if (!pin) return;  // stale tile; the vault hooks normally prevent this
            title = pin->label.empty() ? pin->contract + "/" + pin->table : pin->label;
        } else if (const TileInfo* info = tileInfo(tile.kind)) {
            title = info->title;
        } else {
            return;
        }

        ImGui::PushID(tile.kind.c_str());
        if (beginCard("tile", cardW, tile.kind == "balance")) {
            int dropped =
                drawTileHeader(title, static_cast<int>(i), &removeKind, tile.kind);
            if (dropped >= 0) {
                moveFrom = dropped;
                moveTo = static_cast<int>(i);
            }
            if (tile.kind == "balance")
                drawBalanceTile(state, controller, *account, data, network);
            else if (tile.kind == "resources")
                drawResourcesTile(data);
            else if (tile.kind == "guard")
                drawGuardTile(state);
            else if (tile.kind == "activity")
                drawActivityTile(state);
            else if (tile.kind == "ram")
                drawRamTile(state, controller);
            else if (tile.kind == "chaininfo")
                drawChainInfoTile(state, controller);
            else if (tile.kind == "prices")
                drawPricesTile(state, controller, *account, network);
            else if (tile.kind == "schedules")
                drawSchedulesTile(state);
            else if (tile.kind == "transfer")
                drawTransferTile(state, *account);
            else if (tile.kind == "assets")
                drawAssetsTile(state, *account);
            else if (tile.kind == "contracts")
                drawContractsTile(state);
            else if (tile.kind == "governance")
                drawGovernanceTile(state, data);
            else if (tile.kind == "msig")
                drawMsigTile(state, *account);
            else if (tile.kind == "vault")
                drawVaultTile(state);
            else if (tile.kind == "create")
                drawCreateTile(state);
            else if (tile.kind == "settings")
                drawSettingsTile(state, network);
            else if (pin)
                drawPinTile(state, *pin);
        }
        endCard();
        // The whole card accepts drops too, not just the grip.
        if (int dropped = acceptDropOnLastItem("##dashtiles"); dropped >= 0) {
            moveFrom = dropped;
            moveTo = static_cast<int>(i);
        }
        ImGui::PopID();
    };

    // Row packing: two consecutive half-span tiles share a row (never on
    // phones); anything else takes the full row.
    size_t i = 0;
    while (i < tiles.size()) {
        bool pair = !phone && tiles[i].span == 1 && i + 1 < tiles.size() &&
                    tiles[i + 1].span == 1;
        if (pair) {
            drawOneTile(i, halfW);
            ImGui::SameLine(0, 12);
            drawOneTile(i + 1, halfW);
            i += 2;
        } else {
            drawOneTile(i, tiles[i].span == 1 && !phone ? halfW : 0.0f);
            i += 1;
        }
        vspace(12);
    }

    if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo &&
        moveFrom < static_cast<int>(tiles.size()) &&
        moveTo < static_cast<int>(tiles.size())) {
        std::vector<DashTile> reordered = tiles;
        DashTile moved = reordered[static_cast<size_t>(moveFrom)];
        reordered.erase(reordered.begin() + moveFrom);
        reordered.insert(reordered.begin() + moveTo, moved);
        controller.saveDashboardTiles(std::move(reordered));
    }
    if (!removeKind.empty()) {
        if (startsWith(removeKind, "pin:"))
            controller.removePinnedQuery(removeKind.substr(4));
        else
            controller.removeDashboardTile(removeKind);
    }

    // --- palette ------------------------------------------------------------
    int addable = 0;
    for (const auto& info : kTileCatalog) {
        bool present = false;
        for (const auto& tile : tiles) present |= tile.kind == info.kind;
        if (!present) ++addable;
    }
    bool addClicked = neonButton("ADD TILE +", BtnKind::Ghost, {130, 34});
    // Anchor the palette to the button (its own screen rect) so it opens in
    // the right place whether clicked or force-opened by the tour. The button
    // ends the board, often near the bottom edge, so when the rows would run
    // past the visible page the palette opens upward instead of off-screen.
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 buttonMin = ImGui::GetItemRectMin();
        const ImVec2 buttonMax = ImGui::GetItemRectMax();
        const float rows = static_cast<float>(addable > 0 ? addable : 2);  // the note is two lines
        const float paletteH =
            rows * ImGui::GetTextLineHeightWithSpacing() + style.WindowPadding.y * 2.0f;
        const float gap = style.ItemSpacing.y;
        const bool upward = buttonMax.y + gap + paletteH > pageBottom();
        ImGui::SetNextWindowPos(upward ? ImVec2{buttonMin.x, buttonMin.y - gap}
                                       : ImVec2{buttonMin.x, buttonMax.y + gap},
                                ImGuiCond_Always,
                                upward ? ImVec2{0.0f, 1.0f} : ImVec2{0.0f, 0.0f});
    }
    if (addClicked || qa::forceOpen("add-tile")) ImGui::OpenPopup("##addtile");
    if (ImGui::BeginPopup("##addtile")) {
        for (const auto& info : kTileCatalog) {
            bool present = false;
            for (const auto& tile : tiles) present |= tile.kind == info.kind;
            if (present) continue;
            if (ImGui::Selectable(info.title)) {
                controller.addDashboardTile(info.kind, info.span);
                // The new tile lands at the end of the board, below this
                // button: bring it into view rather than leaving it off-screen.
                requestPageScroll(FLT_MAX);
                ImGui::CloseCurrentPopup();
            }
            ::ui::HandOnHover();
        }
        if (addable == 0) {
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
            ImGui::TextUnformatted("Everything is on the board. Pin contract tables\n"
                                   "from the Contracts page for more tiles.");
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::EndPopup();
    }

    // Kick lazy refreshes when the page shows stale data.
    if (!data.loading && !data.loaded && data.error.empty()) controller.refreshAccount(false);
    controller.refreshPrices(false);  // TTL-gated; no-op with the oracle off
}

}  // namespace tb::ui
