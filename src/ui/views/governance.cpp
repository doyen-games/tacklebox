// Governance: block producer voting (pick up to 30) and vote proxying, with
// a one-click path to put the vote on autopilot.
#include <cstring>

#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

// Current vote state straight from get_account.
void drawCurrentVote(AppState& state) {
    const AccountRef* account = state.currentAccount();
    if (!account) return;
    AccountData& data = state.accountData[state.accountKey(*account)];
    if (!data.loaded || !data.snap.raw.contains("voter_info") ||
        !data.snap.raw["voter_info"].is_object())
        return;
    const json& voter = data.snap.raw["voter_info"];
    if (beginCard("curvote")) {
        sectionTitle("Current vote");
        std::string proxy = voter.value("proxy", std::string());
        if (!proxy.empty()) {
            kvRow("Voting via proxy", proxy, true);
        } else if (voter.contains("producers") && voter["producers"].is_array() &&
                   !voter["producers"].empty()) {
            std::string names;
            for (const auto& p : voter["producers"]) {
                if (!names.empty()) names += ", ";
                names += p.get<std::string>();
            }
            std::string key = "Producers (" + std::to_string(voter["producers"].size()) + ")";
            kvRow(key.c_str(), names, true);
        } else {
            subtext("This account is not voting.");
        }
    }
    endCard();
    vspace(10);
}

}  // namespace

void drawGovernance(AppState& state, Controller& controller) {
    heading("Governance");
    subtext("Vote for block producers or delegate to a proxy. Chains weigh votes by "
            "stake; keep votes fresh - many chains decay them over time.");
    vspace(8);
    const AccountRef* account = state.currentAccount();
    if (!account) {
        emptyState(Icon::Shield, "No account selected", "Pick an account first.");
        return;
    }

    drawCurrentVote(state);
    controller.loadProducers(false);
    GovernanceViewState& gv = state.governance;

    static int tab = 0;
    const char* tabs[] = {"PRODUCERS", "PROXY"};
    for (int i = 0; i < 2; ++i) {
        if (neonButton(tabs[i], tab == i ? BtnKind::Primary : BtnKind::Subtle, {120, 32}))
            tab = i;
        if (i < 1) ImGui::SameLine(0, 6);
    }
    vspace(8);

    if (tab == 1) {
        if (beginCard("proxy")) {
            sectionTitle("Vote through a proxy");
            static char proxy[16] = {};
            FieldOpts opts;
            opts.mono = true;
            opts.placeholder = "proxy account";
            opts.width = 260;
            textField("##proxy", proxy, sizeof proxy, opts);
            ImGui::SameLine(0, 8);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1);
            if (neonButton("SET PROXY", BtnKind::Primary, {130, 38}, gv.busyVote) && proxy[0])
                controller.voteProxy(toLower(trim(proxy)));
        }
        endCard();
        return;
    }

    // Producer table.
    if (!gv.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextWrapped("%s", gv.error.c_str());
        ImGui::PopStyleColor();
    }
    if (gv.loading && gv.producers.is_null()) {
        spinner(13.0f);
        return;
    }
    if (!gv.producers.contains("rows")) return;

    // Action bar.
    {
        char label[64];
        std::snprintf(label, sizeof label, "VOTE (%zu/30)", gv.selected.size());
        if (gv.busyVote) {
            spinner(12.0f);
        } else if (neonButton(label, BtnKind::Primary, {150, 38}, gv.selected.empty())) {
            controller.voteProducers(
                std::vector<std::string>(gv.selected.begin(), gv.selected.end()));
        }
        ImGui::SameLine(0, 6);
        if (neonButton("CLEAR", BtnKind::Subtle, {80, 38})) gv.selected.clear();
        ImGui::SameLine(0, 6);
        if (iconButton("##bpsref", Icon::Refresh, "Refresh producers"))
            controller.loadProducers(true);
        // Autopilot handoff: draft a schedule prefilled with this exact vote.
        ImGui::SameLine(0, 12);
        if (!gv.selected.empty() &&
            neonButton("SCHEDULE THIS VOTE", BtnKind::Ghost, {180, 38})) {
            std::vector<std::string> producers(gv.selected.begin(), gv.selected.end());
            std::sort(producers.begin(), producers.end());
            Schedule schedule;
            schedule.id = uuid4();
            schedule.label = "re-vote " + std::to_string(producers.size()) + " producers";
            schedule.chainId = account->chainId;
            schedule.actor = account->actor;
            schedule.permission = account->permission;
            schedule.contract = "eosio";
            schedule.action = "voteproducer";
            schedule.data = json{{"voter", account->actor},
                                 {"proxy", ""},
                                 {"producers", producers}};
            schedule.intervalSec = 7 * 24 * 3600;
            controller.saveSchedule(schedule);
            state.page = Page::Autopilot;
        }
    }
    vspace(6);

    if (beginCard("bps")) {
        // The URL column earns its keep only on wide layouts.
        bool wide = !layout().phone();
        if (ImGui::BeginTable("bptable", wide ? 4 : 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("##pick", ImGuiTableColumnFlags_WidthFixed,
                                    layout().hit() - 8);
            ImGui::TableSetupColumn("Producer", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            if (wide)
                ImGui::TableSetupColumn("Url", ImGuiTableColumnFlags_WidthStretch, 0.5f);
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::TableHeadersRow();
            ImGui::PopFont();

            int rank = 0;
            for (const auto& producer : gv.producers["rows"]) {
                ++rank;
                std::string owner = producer.value("owner", std::string());
                if (owner.empty()) continue;
                bool active = producer.value("is_active", 1) != 0;
                ImGui::PushID(owner.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool picked = gv.selected.count(owner) > 0;
                if (ImGui::Checkbox("##pick", &picked)) {
                    if (picked && gv.selected.size() < 30)
                        gv.selected.insert(owner);
                    else
                        gv.selected.erase(owner);
                    controller.noteActivity();
                }
                ImGui::TableNextColumn();
                ImGui::PushFont(fonts().mono, kMono);
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      col::vec(active ? col::Ice : col::Slate));
                ImGui::TextUnformatted(owner.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                if (!active) {
                    ImGui::SameLine();
                    badge("INACTIVE", col::Slate);
                }
                ImGui::TableNextColumn();
                ImGui::PushFont(fonts().mono, kMonoSm);
                ImGui::Text("#%d", rank);
                ImGui::PopFont();
                if (!wide) {
                    ImGui::PopID();
                    continue;
                }
                ImGui::TableNextColumn();
                ImGui::PushFont(fonts().mono, kMonoSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                ImGui::TextUnformatted(producer.value("url", std::string()).c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    endCard();
}

}  // namespace tb::ui
