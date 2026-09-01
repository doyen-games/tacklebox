// The signing review: every transaction that is not auto-signed stops here.
// Shows decoded actions, guard verdicts, contract integrity, risk flags, and
// takes the human's decision back to the waiting worker.
#include <cstring>

#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/fx.hpp"
#include "ui/layout.hpp"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

ImU32 overallColor(guard::VerdictLevel level) {
    switch (level) {
        case guard::VerdictLevel::TrustedAuto: return col::Violet;
        case guard::VerdictLevel::Trusted: return col::Success;
        case guard::VerdictLevel::StalePin: return col::Warn;
        case guard::VerdictLevel::ConstraintFail: return col::Danger;
        case guard::VerdictLevel::Unlisted: return col::Steel;
    }
    return col::Steel;
}

const char* overallCopy(guard::VerdictLevel level) {
    switch (level) {
        case guard::VerdictLevel::TrustedAuto:
        case guard::VerdictLevel::Trusted:
            return "Every action matches an active whitelist rule.";
        case guard::VerdictLevel::StalePin:
            return "A whitelisted contract has CHANGED on-chain since you approved it. "
                   "Treat this transaction as brand new.";
        case guard::VerdictLevel::ConstraintFail:
            return "An action matches a rule's shape but breaks its limits.";
        case guard::VerdictLevel::Unlisted:
            return "No whitelist rule covers this transaction. Review every field.";
    }
    return "";
}

void drawActionCard(const SignPrompt::ActionView& action, size_t index, bool& wantRule) {
    ImGui::PushID(static_cast<int>(index));
    if (beginCard("act")) {
        // Header: contract::action, verdict chip right-aligned - or on its own
        // row when the title leaves no room (narrow layouts).
        ImGui::PushFont(fonts().mono, kTextLg);
        ImGui::TextUnformatted((action.contract + "::" + action.action).c_str());
        ImGui::PopFont();
        const char* chipText = action.verdict.level == guard::VerdictLevel::TrustedAuto
                                   ? "AUTO-TRUSTED"
                               : action.verdict.level == guard::VerdictLevel::Trusted
                                   ? "TRUSTED"
                               : action.verdict.level == guard::VerdictLevel::StalePin
                                   ? "CONTRACT CHANGED"
                               : action.verdict.level == guard::VerdictLevel::ConstraintFail
                                   ? "OUT OF BOUNDS"
                                   : "UNLISTED";
        ImGui::PushFont(fonts().uiSemi, kMonoSm);
        float chipW = ImGui::CalcTextSize(chipText).x + 16.0f;
        ImGui::PopFont();
        float remaining = ImGui::GetContentRegionAvail().x;
        ImGui::SameLine();
        float inLine = ImGui::GetContentRegionAvail().x;
        if (inLine >= chipW + 8.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inLine - chipW);
        } else {
            ImGui::NewLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + remaining - chipW);
        }
        verdictChip(action.verdict.level);

        if (!action.authorization.empty()) {
            ImGui::PushFont(fonts().ui, kMonoSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
            ImGui::Text("auth: %s", action.authorization.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        if (!action.verdict.detail.empty()) {
            ImU32 detailColor = action.verdict.level == guard::VerdictLevel::StalePin
                                    ? col::Warn
                                    : action.verdict.level ==
                                              guard::VerdictLevel::ConstraintFail
                                          ? col::Danger
                                          : col::Steel;
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(detailColor));
            ImGui::TextWrapped("%s", action.verdict.detail.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        vspace(4);

        // Decoded parameters. Values wrap inside the card, never overflow it.
        if (action.data.is_object() && !action.data.empty()) {
            float labelCol = layout().phone() ? 110.0f : 140.0f;
            for (auto it = action.data.begin(); it != action.data.end(); ++it) {
                ImGui::PushFont(fonts().ui, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
                ImGui::TextUnformatted(it.key().c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::SameLine(labelCol);
                const auto& value = it.value();
                if (value.is_string()) {
                    const std::string& s = value.get_ref<const std::string&>();
                    bool isAsset = guard::parseAsset(s).has_value();
                    ImGui::PushFont(fonts().mono, kMono);
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          col::vec(isAsset ? col::Cyan : col::Ice));
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextWrapped("%s", s.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                } else if (value.is_object() || value.is_array()) {
                    jsonTree(value, it.key().c_str());
                } else {
                    monoText(value.dump(), col::Cyan, kMono);
                }
            }
        } else if (!action.data.is_null()) {
            jsonTree(action.data, "data");
        }

        if (action.verdict.level == guard::VerdictLevel::Unlisted ||
            action.verdict.level == guard::VerdictLevel::ConstraintFail) {
            vspace(2);
            if (neonButton("DRAFT WHITELIST RULE FROM THIS", BtnKind::Subtle, {240, 28}))
                wantRule = true;
        }
    }
    endCard();
    ImGui::PopID();
    vspace(8);
}

}  // namespace

void drawSignModal(AppState& state, Controller& controller) {
    auto prompt = state.signPrompt;
    if (prompt) ImGui::OpenPopup("##signreq");

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::PushStyleColor(ImGuiCol_PopupBg, col::vec(col::rgba(0x080F1B, 0xFC)));
    if (!beginAdaptiveModal("##signreq", 660.0f)) {
        ImGui::PopStyleColor();
        return;
    }
    // Resolved outside the modal (async password verify): shut the popup down.
    if (!prompt) {
        ImGui::CloseCurrentPopup();
        endAdaptiveModal();
        ImGui::PopStyleColor();
        return;
    }

    ImU32 accent = overallColor(prompt->overall);

    // Header.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        drawIcon(dl, Icon::Shield, {pos.x + 14, pos.y + 16}, 26.0f, accent, 1.9f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40);
        ImGui::PushFont(fonts().uiBold, 24.0f);
        ImGui::TextUnformatted("SIGNATURE REQUEST");
        ImGui::PopFont();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40);
        ImGui::PushFont(fonts().mono, kMonoSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
        ImGui::Text("%s  on  %s", prompt->signer.c_str(),
                    prompt->chainName.empty() ? middleEllipsis(prompt->chainId, 10, 6).c_str()
                                              : prompt->chainName.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    // Dapp-link provenance: who pushed this, with a one-click way out.
    if (!prompt->linkSessionId.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40);
        badgeFilled("VIA DAPP LINK", col::Violet);
        ImGui::SameLine(0, 8);
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(fonts().mono, kMonoSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
        ImGui::TextUnformatted(prompt->linkAppName.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine(0, 10);
        if (neonButton("UNLINK DAPP", BtnKind::Subtle, {120, 26})) {
            controller.removeLinkSessionById(prompt->linkSessionId);
            controller.resolveSignPrompt(false);
        }
    }
    vspace(6);

    // Verdict banner.
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float bw = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushFont(fonts().uiMedium, kText);
        ImVec2 textSize = ImGui::CalcTextSize(overallCopy(prompt->overall), nullptr, false,
                                              bw - 28);
        float bh = textSize.y + 20;
        dl->AddRectFilled(pos, {pos.x + bw, pos.y + bh}, col::alpha(accent, 0.10f), 4.0f);
        dl->AddRectFilled(pos, {pos.x + 3, pos.y + bh}, accent, 2.0f);
        dl->AddText(nullptr, 0, {pos.x + 14, pos.y + 10}, col::Ice,
                    overallCopy(prompt->overall), nullptr, bw - 28);
        ImGui::PopFont();
        ImGui::Dummy({bw, bh + 6});
    }

    // Contract integrity line.
    {
        ImU32 c = prompt->hashesVerified ? col::Success : col::Warn;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        drawIcon(ImGui::GetWindowDrawList(),
                 prompt->hashesVerified ? Icon::CheckCircle : Icon::Warning,
                 {pos.x + 8, pos.y + ImGui::GetTextLineHeight() * 0.55f}, 13.0f, c, 1.6f);
        ImGui::Dummy({20, 0});
        ImGui::SameLine();
        ImGui::PushFont(fonts().ui, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(c));
        ImGui::TextUnformatted(prompt->hashesVerified
                                   ? "Contract code + ABI hashes fetched live from chain"
                                   : "Could not verify contract hashes against the chain");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    // Risk flags.
    if (!prompt->risks.empty()) {
        vspace(2);
        for (const auto& flag : prompt->risks) riskLine(flag);
    }
    vspace(8);

    // Actions scroll in a bounded region so the decision row NEVER leaves the
    // screen: the budget is what remains of the viewport after the chrome
    // above and a measured reserve for countdown + password + buttons below.
    bool wantRuleFromAction = false;
    size_t ruleActionIndex = 0;
    {
        bool needPasswordReserve = state.vault.security.requirePasswordPerSign;
        float footerReserve = ImGui::GetFrameHeight() * 1.4f +
                              ImGui::GetTextLineHeightWithSpacing() * 2.5f +
                              (needPasswordReserve ? ImGui::GetFrameHeight() * 2.8f : 0.0f) +
                              ImGui::GetStyle().ItemSpacing.y * 4.0f;
        float chromeAbove = ImGui::GetCursorPosY();
        float margins = layout().phone() ? layout().safeTop + layout().safeBottom
                                         : 60.0f + ImGui::GetStyle().WindowPadding.y * 2.0f;
        float cap = vp->WorkSize.y - margins - chromeAbove - footerReserve;
        float minH = ImGui::GetTextLineHeightWithSpacing() * 6.0f;
        if (cap < minH) cap = minH;
        ImGui::SetNextWindowSizeConstraints({0, 0}, {FLT_MAX, cap});
        ImGui::BeginChild("##actions", {0, 0},
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened);
        for (size_t i = 0; i < prompt->actions.size(); ++i) {
            bool want = false;
            drawActionCard(prompt->actions[i], i, want);
            if (want) {
                wantRuleFromAction = true;
                ruleActionIndex = i;
            }
        }
        ImGui::EndChild();
    }

    // Expiration countdown.
    {
        int64_t remainMs = prompt->expiresAtMs - nowMs();
        float frac = static_cast<float>(remainMs) / (120.0f * 1000.0f);
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float bw = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, {pos.x + bw, pos.y + 3}, col::rgba(0x11202F), 1.5f);
        ImU32 barCol = remainMs < 20000 ? col::Danger : col::CyanDim;
        dl->AddRectFilled(pos, {pos.x + bw * frac, pos.y + 3}, barCol, 1.5f);
        ImGui::Dummy({bw, 8});
        ImGui::PushFont(fonts().mono, kMonoSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
        if (remainMs > 0)
            ImGui::Text("expires in %llds", static_cast<long long>(remainMs / 1000));
        else
            ImGui::TextUnformatted("transaction expired - reject and retry");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    vspace(6);

    // Per-sign password, if the vault demands it.
    static char signPw[256] = {};
    static std::string signPwError;
    static bool verifying = false;
    bool needPassword = state.vault.security.requirePasswordPerSign;
    if (needPassword) {
        FieldOpts opts;
        opts.password = true;
        opts.placeholder = "vault password to sign";
        opts.error = signPwError.empty() ? nullptr : signPwError.c_str();
        textField("##signpw", signPw, sizeof signPw, opts);
        vspace(4);
    }

    // Decision row.
    bool expired = prompt->expiresAtMs - nowMs() <= 0;
    bool hasCritical = false;
    for (const auto& flag : prompt->risks)
        if (flag.severity == guard::RiskSeverity::Critical) hasCritical = true;
    bool dangerPath = (hasCritical && state.vault.security.blockOnCriticalRisk) ||
                      prompt->overall == guard::VerdictLevel::StalePin;

    auto approve = [&] {
        if (wantRuleFromAction) { /* handled below, before resolution */ }
        controller.resolveSignPrompt(true);
        secureWipe(signPw, sizeof signPw);
        signPwError.clear();
        ImGui::CloseCurrentPopup();
    };

    auto tryApprove = [&] {
        if (!needPassword) {
            approve();
            return;
        }
        if (!signPw[0]) {
            signPwError = "password required";
            return;
        }
        verifying = true;
        signPwError.clear();
        std::string pw = signPw;
        controller.verifyPassword(pw, [&state, &controller](bool ok) {
            verifying = false;
            if (ok) {
                controller.resolveSignPrompt(true);
            } else {
                signPwError = "wrong password";
            }
            (void)state;
        });
        secureWipe(pw.data(), pw.size());
        secureWipe(signPw, sizeof signPw);
    };

    if (verifying) {
        spinner(13.0f);
        ImGui::SameLine(0, 8);
        subtext("verifying...");
    } else {
        // Decision row: equal-stretch columns per the UI canon, the approve
        // side weighted double so the primary action reads as primary.
        float rowH = ImGui::GetFrameHeight() * 1.4f;
        if (ImGui::BeginTable("##decide", 2, ImGuiTableFlags_SizingStretchProp |
                                                 ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn("##reject", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("##approve", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (neonButton("REJECT", BtnKind::Ghost,
                           {ImGui::GetContentRegionAvail().x, rowH})) {
                controller.resolveSignPrompt(false);
                secureWipe(signPw, sizeof signPw);
                signPwError.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::TableNextColumn();
            float approveW = ImGui::GetContentRegionAvail().x;
            if (dangerPath) {
                static float hold = 0.0f;
                if (holdButton(prompt->overall == guard::VerdictLevel::StalePin
                                   ? "HOLD TO SIGN ANYWAY (CONTRACT CHANGED)"
                                   : "HOLD TO SIGN (CRITICAL RISK)",
                               1.5f, &hold, {approveW, rowH}) &&
                    !expired)
                    tryApprove();
            } else {
                if (neonButton("SIGN", BtnKind::Primary, {approveW, rowH}, expired))
                    tryApprove();
            }
            ImGui::EndTable();
        }
    }

    // Stage a whitelist draft for after the modal closes.
    if (wantRuleFromAction) {
        auto draft = controller.draftRuleFromAction(prompt->actions[ruleActionIndex],
                                                    prompt->chainId, prompt->signer);
        openRuleEditor(draft);
        state.page = Page::Whitelist;
        controller.toast(Toast::Info,
                         "Rule drafted from the action - finish it after this decision");
    }

    endAdaptiveModal();
    ImGui::PopStyleColor();
}

void drawPluginPrompt(AppState& state, Controller& controller) {
    auto prompt = state.pluginPrompt;
    if (!prompt) return;
    ImGui::OpenPopup("##pluginprompt");
    if (beginAdaptiveModal("##pluginprompt", 480.0f)) {
        heading(prompt->title.empty() ? "Transaction service" : prompt->title.c_str(), 22.0f);
        if (!prompt->body.empty()) {
            ImGui::PushFont(fonts().ui, kText);
            ImGui::TextWrapped("%s", prompt->body.c_str());
            ImGui::PopFont();
        }
        for (const auto& line : prompt->lines) {
            ImGui::PushFont(fonts().mono, kMonoSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
            ImGui::TextWrapped("%s", line.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        vspace(10);
        if (neonButton("ACCEPT", BtnKind::Primary, {140, 40})) {
            controller.resolvePluginPrompt(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, 8);
        if (neonButton("DECLINE", BtnKind::Ghost, {120, 40})) {
            controller.resolvePluginPrompt(false);
            ImGui::CloseCurrentPopup();
        }
        endAdaptiveModal();
    }
}

}  // namespace tb::ui
