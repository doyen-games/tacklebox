// Msig: browse eosio.msig proposals on-chain (no third-party service), draft
// new proposals from staged actions, approve/unapprove/exec/cancel.
#include <cstring>

#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/ui_helpers.h"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

// --- msig templates ----------------------------------------------------------
// A template stores the whole proposal shape (actions, approvals, expiry,
// suggested name) so recurring proposals start predisposed, not blank.
struct TplEditor {
    bool open = false;
    std::string id;             // empty = creating
    char label[64] = {};
    char name[16] = {};
    char requested[256] = {};
    int expireHours = 168;
    char actionsBuf[8192] = {};
};
TplEditor tplEditor;

void openMsigTemplateFromBuilder(AppState& state) {
    MsigViewState& mv = state.msig;
    tplEditor = TplEditor{};
    tplEditor.open = true;
    std::snprintf(tplEditor.name, sizeof tplEditor.name, "%s", mv.draftName);
    std::snprintf(tplEditor.requested, sizeof tplEditor.requested, "%s",
                  mv.draftRequested);
    tplEditor.expireHours = mv.draftExpireHours;
    json actions = json::array();
    for (const auto& action : mv.draftActions) actions.push_back(action);
    std::snprintf(tplEditor.actionsBuf, sizeof tplEditor.actionsBuf, "%s",
                  actions.dump(1).c_str());
}

void drawTemplateEditor(AppState& state, Controller& controller) {
    (void)state;
    // Reopen every frame while open: survives ImGui-side closes (window
    // resize across breakpoints); ESC cancels explicitly.
    if (tplEditor.open) ImGui::OpenPopup("Msig template");
    if (beginAdaptiveModal("Msig template", 620.0f)) {
        if (!tplEditor.open) {
            ImGui::CloseCurrentPopup();
            endAdaptiveModal();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) tplEditor.open = false;
        heading(tplEditor.id.empty() ? "New msig template" : "Edit msig template", 24.0f);
        subtext("Applying a template predisposes the proposal builder: actions, "
                "requested approvals, expiry and a suggested name.");
        vspace(6);
        {
            FieldOpts opts;
            opts.placeholder = "template name, e.g. weekly payroll";
            textField("Label", tplEditor.label, sizeof tplEditor.label, opts);
        }
        if (beginFieldPair("##tplrow")) {
            nextField();
            {
                FieldOpts opts;
                opts.mono = true;
                opts.hint = "suggested proposal name";
                textField("Proposal name", tplEditor.name, sizeof tplEditor.name, opts);
            }
            nextField();
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
            ImGui::TextUnformatted("Expires (hours)");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy({0, 1});
            ImGui::SetNextItemWidth(::ui::S(160.0f));
            ImGui::InputInt("##tplexp", &tplEditor.expireHours, 24);
            if (tplEditor.expireHours < 1) tplEditor.expireHours = 1;
            endFieldPair();
        }
        {
            FieldOpts opts;
            opts.mono = true;
            opts.hint = "alice@active, bob@active";
            textField("Requested approvals", tplEditor.requested,
                      sizeof tplEditor.requested, opts);
        }
        ImGui::PushFont(fonts().uiSemi, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
        ImGui::TextUnformatted("Actions (JSON array)");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::PushFont(fonts().mono, kMonoSm);
        ImGui::InputTextMultiline("##tplactions", tplEditor.actionsBuf,
                                  sizeof tplEditor.actionsBuf,
                                  {-FLT_MIN, ImGui::GetTextLineHeight() * 8});
        ImGui::PopFont();
        vspace(8);

        if (neonButton("SAVE TEMPLATE", BtnKind::Primary, {160, 40},
                       !tplEditor.label[0])) {
            auto actions = json::parse(std::string(tplEditor.actionsBuf), nullptr, false);
            bool shapeOk = !actions.is_discarded() && actions.is_array();
            if (shapeOk)
                for (const auto& action : actions)
                    if (!action.is_object() || !action.contains("account") ||
                        !action.contains("name"))
                        shapeOk = false;
            if (!shapeOk) {
                controller.toast(Toast::Error,
                                 "Actions must be a JSON array of {account, name, ...} "
                                 "objects");
            } else {
                MsigTemplate tpl;
                tpl.id = tplEditor.id.empty() ? uuid4() : tplEditor.id;
                tpl.label = trim(tplEditor.label);
                tpl.proposalName = toLower(trim(tplEditor.name));
                tpl.requested = trim(tplEditor.requested);
                tpl.expireHours = tplEditor.expireHours;
                tpl.actions = actions;
                controller.saveMsigTemplate(tpl);
                tplEditor.open = false;
            }
        }
        ImGui::SameLine(0, 8);
        if (neonButton("CANCEL", BtnKind::Ghost, {110, 40})) tplEditor.open = false;
        endAdaptiveModal();
    }
}

void drawTemplates(AppState& state, Controller& controller) {
    if (state.vault.msigTemplates.empty()) return;
    if (beginCard("msigtpl")) {
        sectionTitle("Templates");
        for (const auto& tpl : state.vault.msigTemplates) {
            ImGui::PushID(tpl.id.c_str());
            monoText(tpl.label, col::Ice, kMono);
            ImGui::SameLine();
            ImGui::PushFont(fonts().ui, kMonoSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
            ImGui::Text("%zu action%s%s%s", tpl.actions.size(),
                        tpl.actions.size() == 1 ? "" : "s",
                        tpl.requested.empty() ? "" : "  -  ",
                        tpl.requested.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::SameLine();
            float endX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(endX - 176);
            if (neonButton("APPLY", BtnKind::Subtle, {84, 26}))
                controller.applyMsigTemplate(tpl.id);
            ImGui::SameLine(0, 4);
            if (iconButton("##edittpl", Icon::Gear, "Edit template", col::Steel, 14.0f)) {
                tplEditor = TplEditor{};
                tplEditor.open = true;
                tplEditor.id = tpl.id;
                std::snprintf(tplEditor.label, sizeof tplEditor.label, "%s",
                              tpl.label.c_str());
                std::snprintf(tplEditor.name, sizeof tplEditor.name, "%s",
                              tpl.proposalName.c_str());
                std::snprintf(tplEditor.requested, sizeof tplEditor.requested, "%s",
                              tpl.requested.c_str());
                tplEditor.expireHours = tpl.expireHours;
                std::snprintf(tplEditor.actionsBuf, sizeof tplEditor.actionsBuf, "%s",
                              tpl.actions.dump(1).c_str());
            }
            ImGui::SameLine(0, 4);
            if (iconButton("##deltpl", Icon::Trash, "Delete template", col::Danger, 14.0f))
                controller.removeMsigTemplate(tpl.id);
            ImGui::PopID();
        }
    }
    endCard();
}

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

    // Builder fields live in MsigViewState so templates can predispose them.
    float third = (ImGui::GetContentRegionAvail().x - 150) / 2.0f;
    ImGui::BeginGroup();
    {
        FieldOpts opts;
        opts.mono = true;
        opts.width = third;
        opts.hint = "a-z, 1-5, up to 12 chars";
        textField("Proposal name", mv.draftName, sizeof mv.draftName, opts);
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 8);
    ImGui::BeginGroup();
    {
        FieldOpts opts;
        opts.mono = true;
        opts.width = third;
        opts.hint = "alice@active, bob@active";
        textField("Requested approvals", mv.draftRequested, sizeof mv.draftRequested, opts);
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 8);
    ImGui::BeginGroup();
    ImGui::PushFont(fonts().uiSemi, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
    ImGui::TextUnformatted("Expires (hours)");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SetNextItemWidth(::ui::S(160.0f));
    ImGui::InputInt("##exphours", &mv.draftExpireHours, 24);
    ImGui::EndGroup();
    if (mv.draftExpireHours < 1) mv.draftExpireHours = 1;

    if (mv.busyAction) {
        spinner(12.0f);
    } else {
        if (neonButton("PROPOSE", BtnKind::Primary, {150, 40},
                       mv.draftActions.empty() || !mv.draftName[0])) {
            std::vector<std::string> levels;
            std::string list = mv.draftRequested;
            size_t start = 0;
            while (start <= list.size()) {
                size_t comma = list.find(',', start);
                std::string token = comma == std::string::npos
                                        ? list.substr(start)
                                        : list.substr(start, comma - start);
                token = trim(token);
                if (!token.empty()) levels.push_back(token);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            if (levels.empty())
                controller.toast(Toast::Error, "List at least one requested approval");
            else
                controller.msigPropose(toLower(trim(mv.draftName)), levels,
                                       mv.draftExpireHours);
        }
        ImGui::SameLine(0, 8);
        if (neonButton("SAVE AS TEMPLATE", BtnKind::Subtle, {160, 40},
                       mv.draftActions.empty()))
            openMsigTemplateFromBuilder(state);
    }
    endCard();
}

}  // namespace

// QA hook: the tour opens the template editor without a click.
void openMsigTemplateEditor() {
    tplEditor = TplEditor{};
    tplEditor.open = true;
    std::snprintf(tplEditor.label, sizeof tplEditor.label, "weekly payroll");
    std::snprintf(tplEditor.name, sizeof tplEditor.name, "payroll");
    std::snprintf(tplEditor.requested, sizeof tplEditor.requested,
                  "alice@active, bob@active");
    std::snprintf(tplEditor.actionsBuf, sizeof tplEditor.actionsBuf,
                  "[\n {\n  \"account\": \"eosio.token\",\n  \"name\": \"transfer\"\n }\n]");
}

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

    drawTemplates(state, controller);
    if (!state.vault.msigTemplates.empty()) vspace(10);
    drawBuilder(state, controller);
    drawTemplateEditor(state, controller);
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
