// Whitelist manager: the rule table, the stale-pin review flow, and the rule
// editor (also reachable prefilled from the signing modal).
#include <cstring>
#include <optional>

#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/qa.hpp"
#include "ui/ui_helpers.h"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

// --- editor state ------------------------------------------------------------
struct ConstraintRow {
    char path[96] = {};
    int kind = 0;  // 0 any, 1 exact, 2 one-of, 3 range
    char value[256] = {};  // exact value / comma-separated set
    char minBuf[96] = {};
    char maxBuf[96] = {};
};

struct EditorState {
    bool open = false;
    bool isNew = true;
    std::string ruleId;
    char chain[80] = {};
    char signer[96] = {};
    char contract[64] = {};
    char action[64] = {};
    std::vector<ConstraintRow> rows;
    bool strictParams = false;
    bool pin = true;
    bool autoSign = false;
    char note[160] = {};
    bool saving = false;     // async save in flight; modal stays open
    std::string error;       // last save failure, shown inline
};

EditorState editor;
std::optional<guard::WhitelistRule> pendingDraft;

dwarfkit::json parseValueToken(const std::string& raw) {
    std::string t = trim(raw);
    if (t == "true") return true;
    if (t == "false") return false;
    // Numbers stay strings when large; jsonEquiv bridges both forms anyway.
    if (!t.empty() && (t.front() == '{' || t.front() == '[' || t.front() == '"')) {
        auto parsed = dwarfkit::json::parse(t, nullptr, false);
        if (!parsed.is_discarded()) return parsed;
    }
    return t;
}

void loadEditorFromRule(const guard::WhitelistRule& rule, bool isNew) {
    editor = EditorState{};
    editor.open = true;
    editor.isNew = isNew;
    editor.ruleId = rule.id;
    std::snprintf(editor.chain, sizeof editor.chain, "%s", rule.chainId.c_str());
    std::snprintf(editor.signer, sizeof editor.signer, "%s", rule.signer.c_str());
    std::snprintf(editor.contract, sizeof editor.contract, "%s", rule.contract.c_str());
    std::snprintf(editor.action, sizeof editor.action, "%s", rule.action.c_str());
    std::snprintf(editor.note, sizeof editor.note, "%s", rule.note.c_str());
    editor.strictParams = !rule.unlistedParamsAny;
    editor.pin = rule.pin.has_value() || isNew;
    editor.autoSign = rule.autoSign;
    for (const auto& [path, constraint] : rule.params) {
        ConstraintRow row;
        std::snprintf(row.path, sizeof row.path, "%s", path.c_str());
        switch (constraint.kind) {
            case guard::ConstraintKind::Any: row.kind = 0; break;
            case guard::ConstraintKind::Exact: {
                row.kind = 1;
                if (!constraint.values.empty()) {
                    std::string v = constraint.values[0].is_string()
                                        ? constraint.values[0].get<std::string>()
                                        : constraint.values[0].dump();
                    std::snprintf(row.value, sizeof row.value, "%s", v.c_str());
                }
                break;
            }
            case guard::ConstraintKind::OneOf: {
                row.kind = 2;
                std::string joined;
                for (const auto& v : constraint.values) {
                    if (!joined.empty()) joined += ", ";
                    joined += v.is_string() ? v.get<std::string>() : v.dump();
                }
                std::snprintf(row.value, sizeof row.value, "%s", joined.c_str());
                break;
            }
            case guard::ConstraintKind::Range: {
                row.kind = 3;
                if (constraint.min) {
                    std::string v = constraint.min->is_string()
                                        ? constraint.min->get<std::string>()
                                        : constraint.min->dump();
                    std::snprintf(row.minBuf, sizeof row.minBuf, "%s", v.c_str());
                }
                if (constraint.max) {
                    std::string v = constraint.max->is_string()
                                        ? constraint.max->get<std::string>()
                                        : constraint.max->dump();
                    std::snprintf(row.maxBuf, sizeof row.maxBuf, "%s", v.c_str());
                }
                break;
            }
        }
        editor.rows.push_back(row);
    }
}

guard::WhitelistRule editorToRule(const AppState& state) {
    guard::WhitelistRule rule;
    rule.id = editor.ruleId.empty() ? uuid4() : editor.ruleId;
    rule.chainId = trim(editor.chain);
    rule.signer = trim(editor.signer);
    rule.contract = trim(editor.contract);
    rule.action = trim(editor.action);
    if (rule.chainId.empty()) rule.chainId = "*";
    if (rule.signer.empty()) rule.signer = "*@*";
    if (rule.action.empty()) rule.action = "*";
    rule.unlistedParamsAny = !editor.strictParams;
    rule.autoSign = editor.autoSign;
    rule.note = editor.note;
    rule.createdAt = nowSec();
    rule.status = guard::RuleStatus::Active;
    // Preserve created/use stats on edit.
    for (const auto& existing : state.vault.rules)
        if (existing.id == rule.id) {
            rule.createdAt = existing.createdAt;
            rule.useCount = existing.useCount;
            rule.lastUsedAt = existing.lastUsedAt;
        }
    for (const auto& row : editor.rows) {
        std::string path = trim(row.path);
        if (path.empty()) continue;
        guard::ParamConstraint constraint;
        switch (row.kind) {
            case 0: constraint.kind = guard::ConstraintKind::Any; break;
            case 1:
                constraint.kind = guard::ConstraintKind::Exact;
                constraint.values = {parseValueToken(row.value)};
                break;
            case 2: {
                constraint.kind = guard::ConstraintKind::OneOf;
                std::string list = row.value;
                size_t start = 0;
                while (start <= list.size()) {
                    size_t comma = list.find(',', start);
                    std::string token = comma == std::string::npos
                                            ? list.substr(start)
                                            : list.substr(start, comma - start);
                    if (!trim(token).empty()) constraint.values.push_back(parseValueToken(token));
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
                break;
            }
            case 3:
                constraint.kind = guard::ConstraintKind::Range;
                if (trim(row.minBuf).size()) constraint.min = parseValueToken(row.minBuf);
                if (trim(row.maxBuf).size()) constraint.max = parseValueToken(row.maxBuf);
                break;
        }
        rule.params[path] = constraint;
    }
    return rule;
}

void drawEditor(AppState& state, Controller& controller) {
    // editor.open is the ONLY truth. ImGui may close the popup on its own
    // (a resize crossing a form-factor breakpoint reseeds the popup id);
    // re-issuing OpenPopup every frame reopens it with the draft intact.
    // ESC is handled explicitly inside the body as a cancel.
    if (editor.open) ImGui::OpenPopup("Rule editor");
    if (beginAdaptiveModal("Rule editor", 640.0f)) {
        if (!editor.open) {
            // Closed by the async save (or CANCEL) - dismiss on this frame.
            ImGui::CloseCurrentPopup();
            endAdaptiveModal();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) editor.open = false;
        heading(editor.isNew ? "New whitelist rule" : "Edit whitelist rule", 24.0f);
        subtext("The rule fast-tracks exactly what it describes; everything else still "
                "stops for review.");
        vspace(8);
        // The form scrolls inside a bounded child so the save/cancel row can
        // never fall off a short window.
        beginModalBody("##rulebody", ImGui::GetFrameHeight() * 2.4f + 60.0f);

        // Identity row: combo-first (nobody types 64 hex characters), with a
        // custom option that reveals the raw field for patterns.
        auto identityCombo = [&](const char* label, char* buf, size_t bufSize,
                                 bool chain) {
            std::string current = trim(buf);
            std::string preview;
            bool known = false;
            if (current.empty() || current == "*" || current == "*@*") {
                preview = chain ? "any chain (*)" : "any signer (*@*)";
                known = true;
            } else if (chain) {
                for (const auto& net : state.vault.networks)
                    if (net.chainId == current) {
                        preview = net.name;
                        known = true;
                    }
            } else {
                for (const auto& account : state.vault.accounts)
                    if (account.display() == current) {
                        preview = current;
                        known = true;
                    }
            }
            if (!known) preview = current;  // custom pattern shows verbatim

            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo((std::string("##pick") + label).c_str(),
                                  preview.c_str())) {
                if (ImGui::Selectable(chain ? "any chain (*)" : "any signer (*@*)"))
                    std::snprintf(buf, bufSize, "%s", chain ? "*" : "*@*");
                if (chain) {
                    for (const auto& net : state.vault.networks) {
                        ImGui::PushID(net.chainId.c_str());
                        if (ImGui::Selectable(net.name.c_str(), net.chainId == current))
                            std::snprintf(buf, bufSize, "%s", net.chainId.c_str());
                        ::ui::HandOnHover();
                        ImGui::PopID();
                    }
                } else {
                    for (const auto& account : state.vault.accounts) {
                        ImGui::PushID(account.key().c_str());
                        if (ImGui::Selectable(account.display().c_str(),
                                              account.display() == current))
                            std::snprintf(buf, bufSize, "%s",
                                          account.display().c_str());
                        ::ui::HandOnHover();
                        ImGui::PopID();
                    }
                }
                ImGui::EndCombo();
            }
            ::ui::HandOnHover();
            // Custom patterns stay editable underneath.
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::PushFont(fonts().mono, kMonoSm);
            ImGui::InputTextWithHint((std::string("##raw") + label).c_str(),
                                     chain ? "or raw chain id / *"
                                           : "or actor@permission pattern",
                                     buf, bufSize);
            ImGui::PopFont();
        };
        if (beginFieldPair("##identity")) {
            nextField();
            identityCombo("Chain", editor.chain, sizeof editor.chain, true);
            nextField();
            identityCombo("Signer", editor.signer, sizeof editor.signer, false);
            endFieldPair();
        }
        vspace(4);

        if (beginFieldPair("##target")) {
            nextField();
            {
                FieldOpts opts;
                opts.mono = true;
                opts.hint = "exact contract account (never wildcard)";
                textField("Contract", editor.contract, sizeof editor.contract, opts);
            }
            nextField();
            {
                FieldOpts opts;
                opts.mono = true;
                opts.hint = "action name, or * for all actions";
                textField("Action", editor.action, sizeof editor.action, opts);
            }
            endFieldPair();
        }
        vspace(6);

        sectionTitle("Parameter constraints");
        static const char* kindNames[] = {"any", "exact", "one of", "range"};
        int removeAt = -1;
        // One table so every row's field, kind, value and remove button sit
        // on shared columns and one centerline.
        if (!editor.rows.empty() &&
            ImGui::BeginTable("##constraints", 4,
                              ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn("path", ImGuiTableColumnFlags_WidthStretch, 0.34f);
            ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed,
                                    ::ui::S(118.0f));
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.66f);
            ImGui::TableSetupColumn("del", ImGuiTableColumnFlags_WidthFixed,
                                    ::ui::S(28.0f));
            for (size_t i = 0; i < editor.rows.size(); ++i) {
                ConstraintRow& row = editor.rows[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::PushFont(fonts().mono, kMonoSm);
                ImGui::InputTextWithHint("##path", "field.path", row.path, sizeof row.path);
                ImGui::PopFont();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::Combo("##kind", &row.kind, kindNames, 4);
                ::ui::HandOnHover();
                ImGui::TableNextColumn();
                ImGui::PushFont(fonts().mono, kMonoSm);
                if (row.kind == 1) {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputTextWithHint("##v", "expected value", row.value,
                                             sizeof row.value);
                } else if (row.kind == 2) {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputTextWithHint("##v", "a, b, c", row.value, sizeof row.value);
                } else if (row.kind == 3) {
                    float halfCell = (ImGui::GetContentRegionAvail().x - 6) * 0.5f;
                    ImGui::SetNextItemWidth(halfCell);
                    ImGui::InputTextWithHint("##min", "min (empty = open)", row.minBuf,
                                             sizeof row.minBuf);
                    ImGui::SameLine(0, 6);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputTextWithHint("##max", "max: 10.0000 EOS", row.maxBuf,
                                             sizeof row.maxBuf);
                } else {
                    ImGui::AlignTextToFramePadding();
                    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                    ImGui::TextUnformatted("matches anything");
                    ImGui::PopStyleColor();
                }
                ImGui::PopFont();
                ImGui::TableNextColumn();
                // Center the trash on the field row.
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                                     (ImGui::GetFrameHeight() - 25.0f) * 0.5f);
                if (iconButton("##del", Icon::Trash, "Remove constraint", col::Slate,
                               13.0f))
                    removeAt = static_cast<int>(i);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (removeAt >= 0) editor.rows.erase(editor.rows.begin() + removeAt);
        if (neonButton("+ CONSTRAINT", BtnKind::Subtle, {130, 30}))
            editor.rows.push_back(ConstraintRow{});
        vspace(4);
        toggle("Strict parameters", &editor.strictParams,
               "Reject any field the rule does not list (default allows unlisted fields)");
        vspace(8);

        sectionTitle("Integrity & trust");
        toggle("Pin contract code + ABI", &editor.pin,
               "Record the contract's current hashes; any redeploy suspends this rule "
               "until you re-approve");
        bool autoBefore = editor.autoSign;
        toggle("Auto-sign", &editor.autoSign,
               "Sign matching transactions without a click. Requires the pin, the master "
               "switch in Settings, and no critical risk flags");
        if (editor.autoSign && !editor.pin) {
            editor.autoSign = autoBefore ? false : editor.autoSign;
            editor.pin = editor.autoSign ? true : editor.pin;
            if (!editor.autoSign)
                controller.toast(Toast::Warn, "Auto-sign requires the contract pin");
        }
        {
            FieldOpts opts;
            opts.placeholder = "note to future you";
            textField("Note", editor.note, sizeof editor.note, opts);
        }
        vspace(10);

        endModalBody();
        vspace(6);

        // Pinning needs a chain the wallet can actually query.
        std::string chainTrim = trim(editor.chain);
        bool wildcardChain = chainTrim.empty() || chainTrim == "*";
        bool chainKnown = false;
        for (const auto& net : state.vault.networks)
            if (net.chainId == chainTrim) chainKnown = true;
        bool pinBlocked = editor.pin && (wildcardChain || !chainKnown);
        if (pinBlocked) {
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
            ImGui::TextWrapped(wildcardChain
                                   ? "Pinning fetches the contract's live hashes, so it "
                                     "needs a specific chain - pick one above, or turn "
                                     "the pin off for a wildcard-chain rule."
                                   : "No configured network matches this chain id, so "
                                     "the pin cannot be verified. Pick a network above.");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            vspace(4);
        }
        if (!editor.error.empty()) {
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
            ImGui::TextWrapped("%s", editor.error.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            vspace(4);
        }

        bool valid = trim(editor.contract).size() > 0 && !pinBlocked;
        if (editor.saving) {
            spinner(13.0f);
            ImGui::SameLine(0, 8);
            subtext(editor.pin ? "Fetching contract hashes and saving..." : "Saving...");
        } else {
            if (neonButton(editor.pin ? "SAVE & PIN" : "SAVE", BtnKind::Primary, {160, 42},
                           !valid)) {
                editor.saving = true;
                editor.error.clear();
                // The modal stays open: on failure the draft survives and the
                // error shows inline; only success closes it. A live signing
                // prompt underneath re-evaluates so its badges reflect the
                // new rule immediately.
                controller.saveRule(editorToRule(state), editor.pin,
                                    [&controller](bool ok, std::string error) {
                                        editor.saving = false;
                                        if (ok) {
                                            editor.open = false;
                                            controller.reevaluateSignPrompt();
                                        } else {
                                            editor.error = std::move(error);
                                        }
                                    });
            }
            ImGui::SameLine(0, 8);
            if (neonButton("CANCEL", BtnKind::Ghost, {110, 42})) {
                editor.open = false;
            }
        }
        endAdaptiveModal();
    }
}

}  // namespace

void openRuleEditor(const guard::WhitelistRule& draft) { pendingDraft = draft; }

// QA tour hygiene: steps must not inherit a previous step's open editor.
void closeRuleEditor() {
    editor = EditorState{};
    pendingDraft.reset();
}

// Drawn once per frame at app level (after the signing modal), so the rule
// editor works over ANY page - including stacked on top of a signing prompt
// ("whitelist this, then approve"). App-level also keeps the popup id out of
// the page hierarchy, so a resize that swaps the chrome cannot orphan it.
void drawRuleEditorModal(AppState& state, Controller& controller) {
    // Drafts staged by UI code (openRuleEditor) or by the controller
    // (transaction import) both open here.
    if (state.pendingRuleDraft) {
        loadEditorFromRule(*state.pendingRuleDraft, true);
        state.pendingRuleDraft.reset();
    }
    if (pendingDraft) {
        loadEditorFromRule(*pendingDraft, true);
        pendingDraft.reset();
    }
    drawEditor(state, controller);
}

void drawWhitelist(AppState& state, Controller& controller) {
    heading("Whitelist");
    subtext("Rules scoped by signer, contract, action and parameter ranges. Pinned rules "
            "watch the contract's code and ABI hashes and suspend themselves the moment "
            "either changes.");
    vspace(8);

    if (neonButton("+ NEW RULE", BtnKind::Primary, {130, 38})) {
        guard::WhitelistRule blank;
        blank.id.clear();
        const AccountRef* account = state.currentAccount();
        // Prefer something concrete: pinning (the default) needs a real
        // chain, and chain-first navigation almost always has one selected.
        blank.chainId = account            ? account->chainId
                        : !state.selectedChainId.empty() ? state.selectedChainId
                                                         : "*";
        blank.signer = account ? account->display() : "*@*";
        loadEditorFromRule(blank, true);
        editor.ruleId.clear();
    }
    ImGui::SameLine(0, 8);
    // Import: paste a transaction id, get the editor prefilled from its
    // first action.
    if (neonButton("IMPORT TX", BtnKind::Subtle, {110, 38}) ||
        qa::forceOpen("wl-import-tx"))
        ImGui::OpenPopup("##importtx");
    if (qa::active())
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({::ui::S(420.0f), 0}, {::ui::S(560.0f), FLT_MAX});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, col::vec(col::Bg));
    if (ImGui::BeginPopup("##importtx")) {
        sectionTitle("Whitelist from a transaction");
        subtext("The transaction's first action prefills the rule editor; its "
                "parameters arrive as exact-match constraints you can loosen.");
        static char txBuf[72] = {};
        FieldOpts opts;
        opts.mono = true;
        opts.placeholder = "transaction id (64 hex)";
        bool entered = textField("##txid", txBuf, sizeof txBuf, opts);
        vspace(4);
        if ((neonButton("FETCH & DRAFT", BtnKind::Primary, {150, 34}, !txBuf[0]) ||
             entered) &&
            txBuf[0]) {
            controller.importTxAsRule(trim(txBuf));
            txBuf[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, 8);
        if (neonButton("CANCEL", BtnKind::Ghost, {100, 34})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    vspace(10);

    // Stale rules first: they demand attention.
    for (const auto& rule : state.vault.rules) {
        if (rule.status != guard::RuleStatus::Stale) continue;
        ImGui::PushID(rule.id.c_str());
        if (beginCard("stale")) {
            ImVec2 min = ImGui::GetWindowPos();
            ImVec2 max = {min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y};
            glowRect(ImGui::GetWindowDrawList(), min, max, col::alpha(col::Warn, 0.55f),
                     0.35f + 0.25f * pulse(2.0f), 5.0f, 1.0f);
            badgeFilled("SUSPENDED - CONTRACT CHANGED", col::Warn);
            ImGui::SameLine();
            ImGui::PushFont(fonts().mono, kMono);
            ImGui::TextUnformatted((rule.contract + "::" + rule.action).c_str());
            ImGui::PopFont();
            subtext("The on-chain contract no longer matches what you approved. Nothing "
                    "matching this rule fast-tracks until it is re-approved.");
            if (rule.pin) {
                kvRow("Pinned code", middleEllipsis(rule.pin->codeHash, 16, 8), true);
                if (!rule.observedCodeHash.empty())
                    kvRow("Now on-chain", middleEllipsis(rule.observedCodeHash, 16, 8), true);
                kvRow("Pinned ABI", middleEllipsis(rule.pin->abiHash, 16, 8), true);
                if (!rule.observedAbiHash.empty())
                    kvRow("Now on-chain", middleEllipsis(rule.observedAbiHash, 16, 8), true);
            }
            vspace(4);
            if (neonButton("RE-APPROVE (PIN NEW HASHES)", BtnKind::Primary, {250, 36}))
                controller.reapproveRule(rule.id);
            ImGui::SameLine(0, 8);
            if (neonButton("DISABLE", BtnKind::Ghost, {100, 36}))
                controller.setRuleStatus(rule.id, guard::RuleStatus::Disabled);
        }
        endCard();
        ImGui::PopID();
        vspace(8);
    }

    // The rule table.
    if (state.vault.rules.empty()) {
        emptyState(Icon::Shield, "No rules yet",
                   "Approve a transaction and choose \"whitelist this\", or craft a rule "
                   "by hand.");
        return;
    }

    // Phones: rule cards instead of the six-column table.
    if (layout().phone()) {
        for (const auto& rule : state.vault.rules) {
            if (rule.status == guard::RuleStatus::Stale) continue;  // shown above
            ImGui::PushID(rule.id.c_str());
            if (beginCard("r")) {
                monoText(rule.contract + "::" + rule.action, col::Ice, kMono);
                ImGui::PushFont(fonts().mono, kMonoSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                ImGui::Text("%s%s", rule.signer.c_str(),
                            rule.note.empty() ? "" : ("  -  " + rule.note).c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                switch (rule.status) {
                    case guard::RuleStatus::Active:
                        if (rule.pin) badgeFilled("PINNED", col::Success);
                        else badge("ACTIVE", col::Success);
                        break;
                    case guard::RuleStatus::Stale: break;
                    case guard::RuleStatus::Disabled: badge("DISABLED", col::Slate); break;
                }
                if (rule.autoSign) {
                    ImGui::SameLine(0, 4);
                    badgeFilled("AUTO", col::Violet);
                }
                ImGui::SameLine();
                float endX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(endX - 96);
                if (iconButton("##edit", Icon::Gear, "Edit", col::Steel, 15.0f))
                    loadEditorFromRule(rule, false);
                ImGui::SameLine(0, 4);
                bool disabled = rule.status == guard::RuleStatus::Disabled;
                if (iconButton("##toggle", disabled ? Icon::CheckCircle : Icon::XCircle,
                               disabled ? "Enable" : "Disable", col::Steel, 15.0f))
                    controller.setRuleStatus(rule.id, disabled ? guard::RuleStatus::Active
                                                               : guard::RuleStatus::Disabled);
                ImGui::SameLine(0, 4);
                if (iconButton("##del", Icon::Trash, "Delete", col::Danger, 15.0f))
                    controller.removeRule(rule.id);
            }
            endCard();
            ImGui::PopID();
            vspace(6);
        }
        return;
    }

    if (beginCard("rules")) {
        if (ImGui::BeginTable("ruletable", 6,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch, 0.34f);
            ImGui::TableSetupColumn("Signer", ImGuiTableColumnFlags_WidthStretch, 0.20f);
            ImGui::TableSetupColumn("Constraints", ImGuiTableColumnFlags_WidthStretch, 0.20f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("##ops", ImGuiTableColumnFlags_WidthFixed, 108.0f);
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::TableHeadersRow();
            ImGui::PopFont();

            for (const auto& rule : state.vault.rules) {
                ImGui::PushID(rule.id.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushFont(fonts().mono, kMono);
                ImGui::TextUnformatted((rule.contract + "::" + rule.action).c_str());
                ImGui::PopFont();
                if (!rule.note.empty()) {
                    ImGui::PushFont(fonts().ui, kMonoSm);
                    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                    ImGui::TextUnformatted(rule.note.c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }

                ImGui::TableNextColumn();
                ImGui::PushFont(fonts().mono, kMonoSm);
                ImGui::TextUnformatted(rule.signer.c_str());
                ImGui::PopFont();

                ImGui::TableNextColumn();
                if (rule.params.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                    ImGui::TextUnformatted("none");
                    ImGui::PopStyleColor();
                } else {
                    int shown = 0;
                    ImGui::PushFont(fonts().mono, kMonoSm);
                    for (const auto& [path, constraint] : rule.params) {
                        if (shown++ >= 3) {
                            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                            ImGui::Text("+%zu more", rule.params.size() - 3);
                            ImGui::PopStyleColor();
                            break;
                        }
                        if (constraint.kind == guard::ConstraintKind::Any) continue;
                        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
                        ImGui::Text("%s %s", path.c_str(), constraint.describe().c_str());
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopFont();
                }

                ImGui::TableNextColumn();
                switch (rule.status) {
                    case guard::RuleStatus::Active:
                        if (rule.pin) badgeFilled("PINNED", col::Success);
                        else badge("ACTIVE", col::Success);
                        break;
                    case guard::RuleStatus::Stale: badgeFilled("SUSPENDED", col::Warn); break;
                    case guard::RuleStatus::Disabled: badge("DISABLED", col::Slate); break;
                }
                if (rule.autoSign) {
                    ImGui::SameLine(0, 4);
                    badgeFilled("AUTO", col::Violet);
                }

                ImGui::TableNextColumn();
                ImGui::PushFont(fonts().mono, kMonoSm);
                ImGui::Text("%llu", static_cast<unsigned long long>(rule.useCount));
                ImGui::PopFont();
                if (rule.lastUsedAt > 0) {
                    ImGui::PushFont(fonts().ui, kMonoSm);
                    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                    ImGui::Text("%s ago", formatAgo(rule.lastUsedAt).c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }

                ImGui::TableNextColumn();
                if (iconButton("##edit", Icon::Gear, "Edit", col::Steel, 14.0f))
                    loadEditorFromRule(rule, false);
                ImGui::SameLine(0, 2);
                bool disabled = rule.status == guard::RuleStatus::Disabled;
                if (iconButton("##toggle", disabled ? Icon::CheckCircle : Icon::XCircle,
                               disabled ? "Enable" : "Disable", col::Steel, 14.0f))
                    controller.setRuleStatus(rule.id, disabled ? guard::RuleStatus::Active
                                                               : guard::RuleStatus::Disabled);
                ImGui::SameLine(0, 2);
                if (iconButton("##del", Icon::Trash, "Delete", col::Danger, 14.0f) ||
                    qa::forceOpen("rule-del-confirm"))
                    ImGui::OpenPopup("##confirmdel");
                if (qa::active())
                    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                            ImGuiCond_Appearing, {0.5f, 0.5f});
                if (ImGui::BeginPopup("##confirmdel")) {
                    ImGui::TextUnformatted("Delete this rule?");
                    if (neonButton("DELETE", BtnKind::Danger, {90, 30})) {
                        controller.removeRule(rule.id);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (neonButton("KEEP", BtnKind::Ghost, {70, 30})) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    endCard();
}

}  // namespace tb::ui
