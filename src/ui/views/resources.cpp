// Resources: the RAM Bancor market with buy/sell/transfer, CPU/NET staking,
// and PowerUp rentals (chains that run it).
#include <cstring>

#include "core/util.hpp"
#include "guard/rules.hpp"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/qa.hpp"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

std::string humanBytes(int64_t bytes) {
    char buf[32];
    if (bytes < 0) return "unlimited";
    if (bytes >= 1024 * 1024 * 1024)
        std::snprintf(buf, sizeof buf, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024 * 1024)
        std::snprintf(buf, sizeof buf, "%.2f MB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        std::snprintf(buf, sizeof buf, "%.1f KB", bytes / 1024.0);
    else
        std::snprintf(buf, sizeof buf, "%lld B", static_cast<long long>(bytes));
    return buf;
}

std::string humanMicroSec(int64_t us) {
    char buf[32];
    if (us < 0) return "unlimited";
    if (us >= 1000000)
        std::snprintf(buf, sizeof buf, "%.2f s", us / 1000000.0);
    else
        std::snprintf(buf, sizeof buf, "%.1f ms", us / 1000.0);
    return buf;
}

// The account's live RAM / CPU / NET quotas at a glance.
void drawUsage(AppState& state) {
    const AccountRef* account = state.currentAccount();
    if (!account) return;
    AccountData& data = state.accountData[state.accountKey(*account)];
    if (!data.loaded) return;
    if (beginCard("resusage")) {
        sectionTitle("Usage");
        if (ImGui::BeginTable("##meters", layout().phone() ? 1 : 3,
                              ImGuiTableFlags_SizingStretchSame)) {
            const auto& snap = data.snap;
            ImGui::TableNextColumn();
            resourceBar("RAM", double(snap.ramBytes.used), double(snap.ramBytes.max),
                        humanBytes(snap.ramBytes.used) + " / " +
                            humanBytes(snap.ramBytes.max));
            ImGui::TableNextColumn();
            resourceBar("CPU", double(snap.cpuUs.used), double(snap.cpuUs.max),
                        humanMicroSec(snap.cpuUs.used) + " / " +
                            humanMicroSec(snap.cpuUs.max));
            ImGui::TableNextColumn();
            resourceBar("NET", double(snap.netBytes.used), double(snap.netBytes.max),
                        humanBytes(snap.netBytes.used) + " / " +
                            humanBytes(snap.netBytes.max));
            ImGui::EndTable();
        }
    }
    endCard();
    vspace(10);
}

void drawRamTab(AppState& state, Controller& controller) {
    ResourcesViewState& rv = state.resources;
    const AccountRef* account = state.currentAccount();
    AccountData& data = state.accountData[account ? state.accountKey(*account) : ""];

    controller.loadRamMarket(false);

    // Market strip.
    if (beginCard("rammarket")) {
        sectionTitle("RAM market");
        ImGui::SameLine();
        float pinX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(pinX - 26);
        if (iconButton("##pinram", Icon::Pin, "Pin the RAM market to the dashboard",
                       col::Slate, 13.0f))
            controller.addDashboardTile("ram", 1);
        if (rv.ramLoading && rv.ram.fetchedAt == 0) {
            spinner(12.0f);
        } else if (!rv.ramError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
            ImGui::TextWrapped("%s", rv.ramError.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::PushFont(fonts().mono, 26.0f);
            ImGui::TextUnformatted(rv.ram.pricePerKb.empty() ? "-" : rv.ram.pricePerKb.c_str());
            ImGui::PopFont();
            ImGui::SameLine(0, 8);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
            ImGui::TextUnformatted("per KB (before the 0.5% market fee)");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            if (data.loaded)
                kvRow("Your RAM", std::to_string(data.snap.ramBytes.used) + " / " +
                                      std::to_string(data.snap.ramBytes.max) + " bytes",
                      true);
            if (rv.ram.fetchedAt) {
                ImGui::SameLine();
                if (iconButton("##ramref", Icon::Refresh, "Refresh price"))
                    controller.loadRamMarket(true);
            }
        }
    }
    endCard();
    vspace(10);

    float half = pairWidth();
    // Buy.
    if (beginCard("rambuy", half)) {
        sectionTitle("Buy RAM");
        static char buyBytes[24] = {};
        static char buyReceiver[16] = {};
        FieldOpts opts;
        opts.mono = true;
        opts.placeholder = "bytes (e.g. 8192)";
        textField("Bytes", buyBytes, sizeof buyBytes, opts);
        opts.placeholder = "receiver (default: you)";
        textField("Receiver", buyReceiver, sizeof buyReceiver, opts);
        // Live cost estimate off the market price.
        if (buyBytes[0] && !rv.ram.pricePerKb.empty()) {
            if (auto price = guard::parseAsset(rv.ram.pricePerKb)) {
                double bytes = std::atof(buyBytes);
                double cost = (bytes / 1000.0) * static_cast<double>(price->amount) /
                              std::pow(10.0, price->precision) * 1.005;
                char buf[64];
                std::snprintf(buf, sizeof buf, "~%.4f %s incl. fee", cost,
                              price->code.c_str());
                subtext(buf);
            }
        }
        if (neonButton("BUY", BtnKind::Primary, {120, 38}, rv.busyAction) && buyBytes[0])
            controller.buyRamBytes(trim(buyReceiver), std::atoll(buyBytes));
    }
    endCard();
    maybeSameLine();
    // Sell.
    if (beginCard("ramsell", half)) {
        sectionTitle("Sell RAM");
        static char sellBytes[24] = {};
        FieldOpts opts;
        opts.mono = true;
        opts.placeholder = "bytes to sell";
        textField("Bytes", sellBytes, sizeof sellBytes, opts);
        subtext("Sells from this account's free RAM back to the market.");
        if (neonButton("SELL", BtnKind::Ghost, {120, 38}, rv.busyAction) && sellBytes[0])
            controller.sellRam(std::atoll(sellBytes));
    }
    endCard();
    vspace(10);

    if (beginCard("ramxfer")) {
        sectionTitle("Transfer RAM");
        subtext("Moves RAM bytes to another account without touching the market "
                "(needs a system contract new enough to have ramtransfer).");
        static char to[16] = {}, bytes[24] = {}, memo[64] = {};
        float third = (ImGui::GetContentRegionAvail().x - 140) / 3.0f;
        FieldOpts opts;
        opts.mono = true;
        opts.width = third;
        ImGui::BeginGroup();
        opts.placeholder = "recipient";
        textField("##rrto", to, sizeof to, opts);
        ImGui::EndGroup();
        ImGui::SameLine(0, 8);
        ImGui::BeginGroup();
        opts.placeholder = "bytes";
        textField("##rrbytes", bytes, sizeof bytes, opts);
        ImGui::EndGroup();
        ImGui::SameLine(0, 8);
        ImGui::BeginGroup();
        opts.placeholder = "memo";
        textField("##rrmemo", memo, sizeof memo, opts);
        ImGui::EndGroup();
        ImGui::SameLine(0, 8);
        if (neonButton("SEND", BtnKind::Ghost, {110, 38}, rv.busyAction) && to[0] && bytes[0])
            controller.transferRam(trim(to), std::atoll(bytes), memo);
    }
    endCard();
}

void drawStakeTab(AppState& state, Controller& controller) {
    ResourcesViewState& rv = state.resources;
    const AccountRef* account = state.currentAccount();
    AccountData& data = state.accountData[account ? state.accountKey(*account) : ""];
    const NetworkDef* network = state.currentNetwork();
    std::string symbol = network ? network->coreSymbolCode() : "EOS";

    if (beginCard("stakecur")) {
        sectionTitle("Current stake");
        if (data.loaded && data.snap.raw.contains("self_delegated_bandwidth") &&
            data.snap.raw["self_delegated_bandwidth"].is_object()) {
            const json& stake = data.snap.raw["self_delegated_bandwidth"];
            kvAsset("CPU staked", stake.value("cpu_weight", std::string("-")));
            kvAsset("NET staked", stake.value("net_weight", std::string("-")));
        } else {
            subtext("No self-delegated stake (or the chain does not use staking).");
        }
        if (data.loaded && data.snap.raw.contains("refund_request") &&
            data.snap.raw["refund_request"].is_object()) {
            const json& refund = data.snap.raw["refund_request"];
            kvRow("Refund pending",
                  refund.value("cpu_amount", std::string("")) + " CPU / " +
                      refund.value("net_amount", std::string("")) + " NET since " +
                      refund.value("request_time", std::string("")),
                  true);
        }
    }
    endCard();
    vspace(10);

    float half = pairWidth();
    static char cpuQty[24] = {}, netQty[24] = {}, receiver[16] = {};
    if (beginCard("stake", half)) {
        sectionTitle("Stake / delegate");
        subtext("Leave the receiver empty to stake to yourself; name another account "
                "to delegate the CPU/NET to them instead.");
        FieldOpts opts;
        opts.mono = true;
        std::string cpuHint = "e.g. 1.0000 " + symbol;
        opts.placeholder = cpuHint.c_str();
        textField("CPU quantity", cpuQty, sizeof cpuQty, opts);
        textField("NET quantity", netQty, sizeof netQty, opts);
        opts.placeholder = "receiver (default: you)";
        textField("Receiver", receiver, sizeof receiver, opts);
        if (neonButton("STAKE", BtnKind::Primary, {130, 38}, rv.busyAction)) {
            std::string cpu = trim(cpuQty), net = trim(netQty);
            if (cpu.empty()) cpu = "0.0000 " + symbol;
            if (net.empty()) net = "0.0000 " + symbol;
            if (guard::parseAsset(cpu) && guard::parseAsset(net))
                controller.stake(trim(receiver), net, cpu);
            else
                controller.toast(Toast::Error, "Quantities must look like 1.0000 " + symbol);
        }
    }
    endCard();
    maybeSameLine();
    static char unCpu[24] = {}, unNet[24] = {}, unReceiver[16] = {};
    if (beginCard("unstake", half)) {
        sectionTitle("Unstake");
        FieldOpts opts;
        opts.mono = true;
        std::string cpuHint = "e.g. 1.0000 " + symbol;
        opts.placeholder = cpuHint.c_str();
        textField("CPU quantity", unCpu, sizeof unCpu, opts);
        textField("NET quantity", unNet, sizeof unNet, opts);
        opts.placeholder = "receiver (default: you)";
        textField("Receiver", unReceiver, sizeof unReceiver, opts);
        subtext("Unstaked funds refund after the chain's delay (typically 3 days).");
        if (neonButton("UNSTAKE", BtnKind::Ghost, {130, 38}, rv.busyAction)) {
            std::string cpu = trim(unCpu), net = trim(unNet);
            if (cpu.empty()) cpu = "0.0000 " + symbol;
            if (net.empty()) net = "0.0000 " + symbol;
            if (guard::parseAsset(cpu) && guard::parseAsset(net))
                controller.unstake(trim(unReceiver), net, cpu);
            else
                controller.toast(Toast::Error, "Quantities must look like 1.0000 " + symbol);
        }
    }
    endCard();
    vspace(10);

    // Every outgoing delegation (delband), with a one-click undelegate that
    // prefills the unstake card.
    controller.loadDelegations(false);
    if (beginCard("delegations")) {
        sectionTitle("Delegations");
        ImGui::SameLine();
        float pinX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(pinX - 26);
        if (iconButton("##delref", Icon::Refresh, "Refresh delegations"))
            controller.loadDelegations(true);
        if (rv.delegationsLoading && rv.delegationsFetchedAt == 0) {
            spinner(12.0f);
        } else if (!rv.delegationsError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
            ImGui::TextWrapped("%s", rv.delegationsError.c_str());
            ImGui::PopStyleColor();
        } else if (!rv.delegations.contains("rows") || rv.delegations["rows"].empty()) {
            subtext("No stake delegated anywhere (or the chain does not use staking).");
        } else if (ImGui::BeginTable("##delband", layout().phone() ? 3 : 4,
                                     ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_BordersInnerH)) {
            bool wide = !layout().phone();
            ImGui::TableSetupColumn("To", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthStretch, 0.25f);
            ImGui::TableSetupColumn("NET", ImGuiTableColumnFlags_WidthStretch, 0.25f);
            if (wide)
                ImGui::TableSetupColumn("##undel", ImGuiTableColumnFlags_WidthFixed,
                                        130.0f);
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::TableHeadersRow();
            ImGui::PopFont();
            for (const auto& row : rv.delegations["rows"]) {
                std::string to = row.value("to", std::string());
                std::string cpu = row.value("cpu_weight", std::string());
                std::string net = row.value("net_weight", std::string());
                ImGui::PushID(to.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushFont(fonts().mono, kMono);
                ImGui::TextUnformatted(to.c_str());
                ImGui::PopFont();
                if (account && to == account->actor) {
                    ImGui::SameLine();
                    badge("SELF", col::Slate);
                }
                ImGui::TableNextColumn();
                assetText(cpu, kMonoSm);
                ImGui::TableNextColumn();
                assetText(net, kMonoSm);
                if (wide) {
                    ImGui::TableNextColumn();
                    if (neonButton("UNDELEGATE", BtnKind::Subtle, {118, 26},
                                   rv.busyAction)) {
                        std::snprintf(unReceiver, sizeof unReceiver, "%s", to.c_str());
                        std::snprintf(unCpu, sizeof unCpu, "%s", cpu.c_str());
                        std::snprintf(unNet, sizeof unNet, "%s", net.c_str());
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    endCard();
}

void drawPowerUpTab(AppState& state, Controller& controller) {
    ResourcesViewState& rv = state.resources;
    const NetworkDef* network = state.currentNetwork();
    std::string symbol = network ? network->coreSymbolCode() : "EOS";

    if (beginCard("powerup")) {
        sectionTitle("PowerUp rental");
        subtext("Rents CPU/NET for 24 hours on chains that run the PowerUp model "
                "(EOS). Quote first; the max payment caps what the chain may take.");
        static char cpuMs[16] = "5", netKb[16] = "10";
        float quarter = (ImGui::GetContentRegionAvail().x - 160) / 2.0f;
        FieldOpts opts;
        opts.mono = true;
        opts.width = quarter;
        ImGui::BeginGroup();
        textField("CPU (ms)", cpuMs, sizeof cpuMs, opts);
        ImGui::EndGroup();
        ImGui::SameLine(0, 8);
        ImGui::BeginGroup();
        textField("NET (kb)", netKb, sizeof netKb, opts);
        ImGui::EndGroup();
        ImGui::SameLine(0, 8);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 22);
        if (rv.quoteLoading) {
            spinner(12.0f);
        } else if (neonButton("QUOTE", BtnKind::Ghost, {110, 38})) {
            controller.quotePowerUp(std::atof(cpuMs), std::atof(netKb));
        }
        if (!rv.quoteError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
            ImGui::TextWrapped("%s", rv.quoteError.c_str());
            ImGui::PopStyleColor();
        }
        if (rv.quote.cpuFrac > 0 || rv.quote.netFrac > 0) {
            char cost[96];
            std::snprintf(cost, sizeof cost, "estimated cost ~%.4f %s for 1 day",
                          rv.quote.costCore, symbol.c_str());
            kvRow("Quote", cost, true);
            static char maxPay[24] = {};
            FieldOpts payOpts;
            payOpts.mono = true;
            std::string payHint = "max payment, e.g. 0.5000 " + symbol;
            payOpts.placeholder = payHint.c_str();
            payOpts.width = 260;
            textField("##maxpay", maxPay, sizeof maxPay, payOpts);
            ImGui::SameLine(0, 8);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1);
            if (neonButton("POWER UP", BtnKind::Primary, {130, 38}, rv.busyAction)) {
                std::string pay = trim(maxPay);
                if (guard::parseAsset(pay))
                    controller.powerUp(1, rv.quote.cpuFrac, rv.quote.netFrac, pay);
                else
                    controller.toast(Toast::Error,
                                     "Max payment must look like 0.5000 " + symbol);
            }
        }
    }
    endCard();
}

}  // namespace

void drawResources(AppState& state, Controller& controller) {
    heading("Resources");
    subtext("RAM, stake and rentals for the active account. Every action here passes "
            "the same guard as any other transaction.");
    vspace(8);
    if (!state.currentAccount()) {
        emptyState(Icon::Pulse, "No account selected", "Pick an account first.");
        return;
    }

    drawUsage(state);

    static int tab = 0;
    if (qa::forceOpen("res-stake-tab")) tab = 1;
    const char* tabs[] = {"RAM", "STAKE", "POWERUP"};
    for (int i = 0; i < 3; ++i) {
        if (neonButton(tabs[i], tab == i ? BtnKind::Primary : BtnKind::Subtle, {110, 32}))
            tab = i;
        if (i < 2) ImGui::SameLine(0, 6);
    }
    vspace(8);
    switch (tab) {
        case 0: drawRamTab(state, controller); break;
        case 1: drawStakeTab(state, controller); break;
        case 2: drawPowerUpTab(state, controller); break;
    }
}

}  // namespace tb::ui
