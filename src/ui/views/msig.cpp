// Msig: browse eosio.msig proposals on-chain (no third-party service), draft
// new proposals from staged actions, approve/unapprove/exec/cancel.
#include <cstring>

#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

void drawBuilder(AppState& state, Controller& controller) {
    MsigViewState& mv = state.msig;
    if (!beginCard("msigbuild")) {
        endCard();
        return;
    }
    sectionTitle("Proposal builder");
    if (mv.draftActions.empty()) {
        subtext("Stage actions from the Contracts page (build an action, then TO MSIG) "
                "or paste a raw action object below.");
    } else {
        int removeAt = -1;
        for (size_t i = 0; i < mv.draftActions.size(); ++i) {
            const json& action = mv.draftActions[i];
            ImGui::PushID(static_cast<int>(i));
            monoText(action.value("account", "?") + "::" + action.value("name", "?"),
                     col::Cyan, kMono);
            ImGui::SameLine();
            if (iconButton("##rmact", Icon::Trash, "Remove", col::Slate, 13.0f))
                removeAt = static_cast<int>(i);
            if (action.contains("data")) jsonTree(action["data"], "draftdata");
            ImGui::PopID();
        }
        if (removeAt >= 0) mv.draftActions.erase(mv.draftActions.begin() + removeAt);
    }

    // Raw action paste lane.
    static char rawAction[2048] = {};
    FieldOpts rawOpts;
    rawOpts.mono = true;
    rawOpts.placeholder =
        "{\"account\":\"eosio.token\",\"name\":\"transfer\",\"authorization\":[...],"
        "\"data\":{...}}";
    textField("##rawact", rawAction, sizeof rawAction, rawOpts);
    if (neonButton("+ STAGE ACTION", BtnKind::Subtle, {130, 30}) && rawAction[0]) {
        auto parsed = json::parse(std::string(rawAction), nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("account") ||
            !parsed.contains("name"))
            controller.toast(Toast::Error, "Not a valid action object");
        else {
            controller.stageMsigAction(parsed);
            rawAction[0] = 0;
        }
    }
    vspace(8);

    static char proposalName[16] = {};
    static char requested[256] = {};
    static int expireHours = 168;
    float third = (ImGui::GetContentRegionAvail().x - 150) / 2.0f;
    ImGui::BeginGroup();
    {
        FieldOpts opts;
        opts.mono = true;
        opts.width = third;
        opts.hint = "a-z, 1-5, up to 12 chars";
        textField("Proposal name", proposalName, sizeof proposalName, opts);
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 8);
    ImGui::BeginGroup();
    {
        FieldOpts opts;
        opts.mono = true;
        opts.width = third;
        opts.hint = "alice@active, bob@active";
        textField("Requested approvals", requested, sizeof requested, opts);
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 8);
    ImGui::BeginGroup();
    ImGui::PushFont(fonts().uiSemi, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
    ImGui::TextUnformatted("Expires (hours)");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("##exphours", &expireHours, 24);
    ImGui::EndGroup();
    if (expireHours < 1) expireHours = 1;

    if (mv.busyAction) {
        spinner(12.0f);
    } else if (neonButton("PROPOSE", BtnKind::Primary, {150, 40},
                          mv.draftActions.empty() || !proposalName[0])) {
        std::vector<std::string> levels;
        std::string list = requested;
        size_t start = 0;
        while (start <= list.size()) {
            size_t comma = list.find(',', start);
            std::string token = comma == std::string::npos ? list.substr(start)
                                                           : list.substr(start, comma - start);
            token = trim(token);
            if (!token.empty()) levels.push_back(token);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (levels.empty())
            controller.toast(Toast::Error, "List at least one requested approval");
        else
            controller.msigPropose(toLower(trim(proposalName)), levels, expireHours);
    }
    endCard();
}

}  // namespace

void drawMsig(AppState& state, Controller& controller) {
    heading("Multisig");
    subtext("eosio.msig proposals read straight from chain tables. Proposing, "
            "approving and executing all pass the guard like any other action.");
    vspace(8);
    const AccountRef* account = state.currentAccount();
    if (!account) {
        emptyState(Icon::Key, "No account selected", "Pick an account first.");
        return;
    }
    MsigViewState& mv = state.msig;

    drawBuilder(state, controller);
    vspace(12);

    // Browser.
    static char proposerBuf[16] = {};
    if (!proposerBuf[0] && mv.proposer.empty())
        std::snprintf(proposerBuf, sizeof proposerBuf, "%s", account->actor.c_str());
    {
        float avail = ImGui::GetContentRegionAvail().x;
        FieldOpts opts;
        opts.mono = true;
        opts.placeholder = "proposer account";
        opts.width = avail - 130.0f;
        bool entered = textField("##proposer", proposerBuf, sizeof proposerBuf, opts);
        ImGui::SameLine(0, 8);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1);
        if ((neonButton("LOAD", BtnKind::Ghost, {110, 38}) || entered) && proposerBuf[0])
            controller.loadProposals(toLower(trim(proposerBuf)));
    }
    vspace(8);

    if (mv.loading) spinner(13.0f);
    if (!mv.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextWrapped("%s", mv.error.c_str());
        ImGui::PopStyleColor();
    }
    if (mv.proposals.contains("rows")) {
        if (beginCard("proposals")) {
            std::string title =
                mv.proposer.empty() ? "Proposals" : "Proposals: " + mv.proposer;
            sectionTitle(title.c_str());
            const json& rows = mv.proposals["rows"];
            if (rows.empty()) subtext("No open proposals for this account.");
            for (const auto& row : rows) {
                std::string name = row.value("proposal_name", std::string());
                ImGui::PushID(name.c_str());
                bool selected = mv.selectedName == name;
                if (ImGui::Selectable("##prop", selected, 0, {0, 26}))
                    controller.loadProposalDetail(mv.proposer, name);
                ImVec2 rmin = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(fonts().mono, kMono,
                                                    {rmin.x + 6, rmin.y + 4},
                                                    selected ? col::Cyan : col::Ice,
                                                    name.c_str());
                ImGui::PopID();
            }
        }
        endCard();
    }

    // Detail.
    if (!mv.selectedName.empty()) {
        vspace(10);
        if (beginCard("propdetail", 0, true)) {
            ImGui::PushFont(fonts().uiSemi, kTextLg);
            ImGui::TextUnformatted((mv.proposer + " / " + mv.selectedName).c_str());
            ImGui::PopFont();
            if (mv.detailLoading) spinner(12.0f);
            if (!mv.detailError.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
                ImGui::TextWrapped("%s", mv.detailError.c_str());
                ImGui::PopStyleColor();
            }
            if (!mv.approvals.is_null() && mv.approvals.is_object()) {
                size_t provided = mv.approvals.contains("provided_approvals")
                                      ? mv.approvals["provided_approvals"].size()
                                      : 0;
                size_t requested = mv.approvals.contains("requested_approvals")
                                       ? mv.approvals["requested_approvals"].size()
                                       : 0;
                kvRow("Approvals",
                      std::to_string(provided) + " given, " + std::to_string(requested) +
                          " outstanding");
                for (const char* bucket : {"provided_approvals", "requested_approvals"}) {
                    if (!mv.approvals.contains(bucket)) continue;
                    bool given = std::string(bucket) == "provided_approvals";
                    for (const auto& entry : mv.approvals[bucket]) {
                        const json& level =
                            entry.contains("level") ? entry["level"] : entry;
                        std::string who = level.value("actor", std::string()) + "@" +
                                          level.value("permission", std::string());
                        ImGui::Dummy({8, 0});
                        ImGui::SameLine();
                        statusDot(given ? col::Success : col::Slate);
                        ImGui::SameLine(0, 8);
                        monoText(who, given ? col::Ice : col::Steel, kMonoSm);
                    }
                }
            }
            if (!mv.decodedTrx.is_null()) {
                vspace(4);
                sectionTitle("Proposed transaction");
                jsonTree(mv.decodedTrx, "proptrx");
            }
            vspace(8);
            bool busy = mv.busyAction;
            if (neonButton("APPROVE", BtnKind::Primary, {110, 36}, busy))
                controller.msigApprove(mv.proposer, mv.selectedName, true);
            ImGui::SameLine(0, 6);
            if (neonButton("UNAPPROVE", BtnKind::Ghost, {110, 36}, busy))
                controller.msigApprove(mv.proposer, mv.selectedName, false);
            ImGui::SameLine(0, 6);
            if (neonButton("EXECUTE", BtnKind::Primary, {110, 36}, busy))
                controller.msigExec(mv.proposer, mv.selectedName);
            ImGui::SameLine(0, 6);
            if (mv.proposer == account->actor &&
                neonButton("CANCEL PROPOSAL", BtnKind::Danger, {150, 36}, busy))
                controller.msigCancel(mv.proposer, mv.selectedName);
        }
        endCard();
    }
}

}  // namespace tb::ui
