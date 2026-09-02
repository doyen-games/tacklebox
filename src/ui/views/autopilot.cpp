// Autopilot: recurring transactions that only ever execute through the
// guard's auto-sign fast path. A schedule is a clock, never a fourth way to
// sign - no matching pinned auto-sign rule means the run is skipped.
#include <algorithm>
#include <cctype>
#include <cstring>

#include "app/autopilot_util.hpp"
#include "core/util.hpp"
#include "guard/rules.hpp"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/qa.hpp"
#include "ui/ui_helpers.h"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

struct EditorState {
    bool open = false;
    std::string id;
    char label[64] = {};
    char contract[16] = {};
    char action[16] = {};
    char dataBuf[2048] = {};
    int intervalValue = 24;
    int intervalUnit = 2;  // 0 minutes, 1 hours, 2 days
    bool runMissed = true;
    // timing
    int timingMode = 0;            // Schedule::TimingMode
    char dailyBuf[12] = "09:00:00";
    char startBuf[24] = {};        // "" = now
    char endBuf[24] = {};          // "" = never
    // dynamic amount
    int amountMode = 0;            // Schedule::AmountFixed / AmountPercent
    char percentBuf[8] = "10";
    char tokenContract[16] = "eosio.token";
    char tokenCode[8] = {};
    char amountField[32] = "quantity";
    char reserveBuf[32] = {};
};
EditorState editor;

int64_t intervalSeconds(const EditorState& e) {
    int64_t unit = e.intervalUnit == 0 ? 60 : e.intervalUnit == 1 ? 3600 : 86400;
    int64_t v = e.intervalValue < 1 ? 1 : e.intervalValue;
    return v * unit;
}

std::string humanInterval(int64_t sec) {
    char buf[32];
    if (sec % 86400 == 0)
        std::snprintf(buf, sizeof buf, "%lldd", static_cast<long long>(sec / 86400));
    else if (sec % 3600 == 0)
        std::snprintf(buf, sizeof buf, "%lldh", static_cast<long long>(sec / 3600));
    else
        std::snprintf(buf, sizeof buf, "%lldm", static_cast<long long>(sec / 60));
    return buf;
}

// Does an ACTIVE pinned auto-sign rule plausibly cover this schedule?
bool hasAutoRule(const AppState& state, const Schedule& schedule) {
    for (const auto& rule : state.vault.rules) {
        if (rule.status != guard::RuleStatus::Active || !rule.autoSign || !rule.pin) continue;
        if (rule.matchesIdentity(schedule.chainId, schedule.actor, schedule.permission,
                                 schedule.contract, schedule.action))
            return true;
    }
    return false;
}

void drawEditor(AppState& state, Controller& controller) {
    if (!editor.open) return;
    ImGui::OpenPopup("Schedule editor");
    if (beginAdaptiveModal("Schedule editor", 620.0f)) {
        heading(editor.id.empty() ? "New schedule" : "Edit schedule", 24.0f);
        subtext("Runs while TackleBox is open and the vault unlocked. Signing happens "
                "ONLY through a pinned auto-sign whitelist rule matching this exact "
                "action - otherwise the run is recorded as blocked.");
        vspace(6);
        // The form scrolls in a bounded child so SAVE/CANCEL never leave the
        // screen (same treatment as the rule editor).
        beginModalBody("##schedbody", ImGui::GetFrameHeight() * 2.4f + 60.0f);

        FieldOpts opts;
        opts.placeholder = "what future-you should see";
        textField("Label", editor.label, sizeof editor.label, opts);

        float half = (ImGui::GetContentRegionAvail().x - 10) * 0.5f;
        ImGui::BeginGroup();
        {
            FieldOpts f;
            f.mono = true;
            f.width = half;
            textField("Contract", editor.contract, sizeof editor.contract, f);
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, 10);
        ImGui::BeginGroup();
        {
            FieldOpts f;
            f.mono = true;
            f.width = half;
            textField("Action", editor.action, sizeof editor.action, f);
        }
        ImGui::EndGroup();

        FieldOpts dataOpts;
        dataOpts.mono = true;
        dataOpts.placeholder = "{\"voter\":\"alice\",\"producers\":[...]}";
        dataOpts.hint = "action parameters as JSON; string values may use {actor} "
                        "{amount} {balance} {date} {time}";
        textField("Data", editor.dataBuf, sizeof editor.dataBuf, dataOpts);

        // Dynamic amount: X fixed (data as-is) or Y% of the live balance.
        ImGui::PushFont(fonts().uiSemi, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
        ImGui::TextUnformatted("Amount");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        const char* modes[] = {"fixed (use data values as-is)",
                               "percent of available balance at run time"};
        ImGui::Combo("##amode", &editor.amountMode, modes, 2);
        if (editor.amountMode == 1) {
            if (ImGui::BeginTable("##dyn", layout().phone() ? 1 : 3,
                                  ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                FieldOpts f;
                f.mono = true;
                f.hint = "percent (0-100]";
                textField("Percent", editor.percentBuf, sizeof editor.percentBuf, f);
                ImGui::TableNextColumn();
                f.hint = "token contract";
                textField("Contract", editor.tokenContract, sizeof editor.tokenContract, f);
                ImGui::TableNextColumn();
                f.hint = "symbol code, e.g. WAX";
                textField("Token", editor.tokenCode, sizeof editor.tokenCode, f);
                ImGui::TableNextColumn();
                f.hint = "data field to fill";
                textField("Field", editor.amountField, sizeof editor.amountField, f);
                ImGui::TableNextColumn();
                f.hint = "keep untouched, e.g. 1.0000 WAX";
                textField("Reserve", editor.reserveBuf, sizeof editor.reserveBuf, f);
                ImGui::EndTable();
            }
            subtext("The amount is recomputed from the fresh liquid balance every run "
                    "(floored, reserve subtracted). Pair it with an auto-sign rule "
                    "whose Range caps the field - or lock the destination with an "
                    "Exact 'to' and cap nothing.");
        }

        // Timing: how the clock advances, then the interval or exact time,
        // then the optional start/end window.
        ImGui::PushFont(fonts().uiSemi, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
        ImGui::TextUnformatted("Timing");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
        const char* timings[] = {"interval, counted from the last run",
                                 "fixed grid: start time + interval (no drift)",
                                 "daily at an exact time"};
        if (qa::forceOpen("sched-timing")) qa::openCombo("##timing");
        ImGui::Combo("##timing", &editor.timingMode, timings, 3);
        ::ui::HandOnHover();

        if (editor.timingMode == Schedule::TimeDaily) {
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
            ImGui::TextUnformatted("At");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::SameLine(0, 8);
            ImGui::SetNextItemWidth(110);
            ImGui::PushFont(fonts().mono, kMonoSm);
            ImGui::InputTextWithHint("##daily", "HH:MM:SS", editor.dailyBuf,
                                     sizeof editor.dailyBuf);
            ImGui::PopFont();
            ImGui::SameLine(0, 8);
            subtext("local time, every day");
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
            ImGui::TextUnformatted("Every");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::SameLine(0, 8);
            ImGui::SetNextItemWidth(110);
            ImGui::InputInt("##ival", &editor.intervalValue);
            ImGui::SameLine(0, 6);
            ImGui::SetNextItemWidth(110);
            const char* units[] = {"minutes", "hours", "days"};
            ImGui::Combo("##iunit", &editor.intervalUnit, units, 3);
            if (editor.timingMode == Schedule::TimeAnchored)
                subtext("Runs land exactly on start, start + interval, start + 2x... "
                        "missed points roll forward to the next grid point.");
        }

        // Start / end window (every mode). Empty start = now; empty end =
        // runs forever.
        float halfw = (ImGui::GetContentRegionAvail().x - 10) * 0.5f;
        ImGui::BeginGroup();
        {
            FieldOpts f;
            f.mono = true;
            f.width = halfw;
            f.placeholder = "now  (or YYYY-MM-DD HH:MM:SS)";
            textField(editor.timingMode == Schedule::TimeAnchored ? "Start / grid origin"
                                                                  : "Start",
                      editor.startBuf, sizeof editor.startBuf, f);
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, 10);
        ImGui::BeginGroup();
        {
            FieldOpts f;
            f.mono = true;
            f.width = halfw;
            f.placeholder = "never  (or YYYY-MM-DD HH:MM:SS)";
            textField("End", editor.endBuf, sizeof editor.endBuf, f);
        }
        ImGui::EndGroup();

        toggle("Run missed executions on unlock", &editor.runMissed,
               "If a run was due while the wallet was closed or locked, fire it on the "
               "next unlock");
        endModalBody();
        vspace(8);

        const AccountRef* account = state.currentAccount();
        if (neonButton("SAVE", BtnKind::Primary, {130, 40},
                       !account || !editor.contract[0] || !editor.action[0])) {
            auto data = json::parse(std::string(editor.dataBuf), nullptr, false);
            if (editor.dataBuf[0] && data.is_discarded()) {
                controller.toast(Toast::Error, "Data is not valid JSON");
            } else {
                Schedule schedule;
                schedule.id = editor.id.empty() ? uuid4() : editor.id;
                schedule.label = editor.label[0] ? editor.label
                                                 : std::string(editor.contract) + "::" +
                                                       editor.action;
                schedule.chainId = account->chainId;
                schedule.actor = account->actor;
                schedule.permission = account->permission;
                schedule.contract = toLower(trim(editor.contract));
                schedule.action = toLower(trim(editor.action));
                schedule.data = data.is_discarded() ? json::object() : data;
                schedule.intervalSec = intervalSeconds(editor);
                schedule.runMissedOnUnlock = editor.runMissed;
                schedule.amountMode = editor.amountMode == 1 ? Schedule::AmountPercent
                                                             : Schedule::AmountFixed;
                bool amountOk = true;
                if (schedule.amountMode == Schedule::AmountPercent) {
                    schedule.amountPercent = std::atof(editor.percentBuf);
                    schedule.amountTokenContract = toLower(trim(editor.tokenContract));
                    std::string code = trim(editor.tokenCode);
                    for (auto& c : code) c = static_cast<char>(std::toupper(
                                             static_cast<unsigned char>(c)));
                    schedule.amountTokenCode = code;
                    schedule.amountField = trim(editor.amountField);
                    schedule.amountReserve = trim(editor.reserveBuf);
                    if (schedule.amountPercent <= 0 || schedule.amountPercent > 100 ||
                        schedule.amountTokenCode.empty()) {
                        controller.toast(Toast::Error,
                                         "Percent mode needs a percent in (0,100] and a "
                                         "token code");
                        amountOk = false;
                    }
                }
                // Timing: exact times and window bounds must parse before
                // anything saves; a bad field keeps the editor open.
                schedule.timingMode = editor.timingMode;
                if (amountOk && editor.timingMode == Schedule::TimeDaily) {
                    auto tod = autopilot::parseTimeOfDay(editor.dailyBuf);
                    if (!tod) {
                        controller.toast(Toast::Error, "Daily time: " + tod.error().message);
                        amountOk = false;
                    } else {
                        schedule.dailySec = *tod;
                        schedule.intervalSec = 86400;  // one grid day
                    }
                }
                if (amountOk && editor.startBuf[0]) {
                    auto at = autopilot::parseDateTimeLocal(editor.startBuf);
                    if (!at) {
                        controller.toast(Toast::Error, "Start: " + at.error().message);
                        amountOk = false;
                    } else {
                        schedule.startAt = *at;
                    }
                }
                if (amountOk && editor.endBuf[0]) {
                    auto at = autopilot::parseDateTimeLocal(editor.endBuf);
                    if (!at) {
                        controller.toast(Toast::Error, "End: " + at.error().message);
                        amountOk = false;
                    } else {
                        schedule.endAt = *at;
                    }
                }
                if (amountOk && schedule.endAt > 0 &&
                    schedule.endAt <= std::max(schedule.startAt, nowSec())) {
                    controller.toast(Toast::Error,
                                     "The end time must be after the start (and not "
                                     "already in the past)");
                    amountOk = false;
                }
                if (!amountOk) {
                    // Leave the editor open with the inputs intact.
                } else {
                // Preserve run history on edit.
                for (const auto& existing : state.vault.schedules)
                    if (existing.id == schedule.id) {
                        schedule.lastRunAt = existing.lastRunAt;
                        schedule.nextRunAt = existing.nextRunAt;
                        schedule.lastResult = existing.lastResult;
                        schedule.enabled = existing.enabled;
                    }
                controller.saveSchedule(schedule);
                editor.open = false;
                ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::SameLine(0, 8);
        if (neonButton("CANCEL", BtnKind::Ghost, {110, 40})) {
            editor.open = false;
            ImGui::CloseCurrentPopup();
        }
        endAdaptiveModal();
    }
}

}  // namespace

// QA hook: the tour opens the editor without a click.
void openScheduleEditor() {
    editor = EditorState{};
    editor.open = true;
}

void drawAutopilot(AppState& state, Controller& controller) {
    heading("Autopilot");
    subtext("Recurring transactions on a timer: re-vote producers, claim rewards, top "
            "up rentals. A schedule can only ever sign through a pinned auto-sign "
            "whitelist rule - the timer has no signing power of its own.");
    vspace(8);

    if (!state.vault.security.allowAutoSign) {
        if (beginCard("apwarn")) {
            badgeFilled("AUTO-SIGN MASTER SWITCH IS OFF", col::Warn);
            subtext("Schedules will tick but every run will be blocked. Enable "
                    "auto-sign rules in Settings > Security policy to arm them.");
            if (neonButton("OPEN SETTINGS", BtnKind::Ghost, {150, 34}))
                state.page = Page::Settings;
        }
        endCard();
        vspace(8);
    }

    // Quick creates: the flows people actually run on a timer.
    const NetworkDef* net = state.currentNetwork();
    std::string coreCode = net ? net->coreSymbolCode() : "EOS";
    if (ImGui::BeginTable("##presets", layout().phone() ? 2 : 4,
                          ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if (neonButton("+ NEW SCHEDULE", BtnKind::Primary, {-FLT_MIN, 0})) {
            editor = EditorState{};
            editor.open = true;
        }
        ImGui::TableNextColumn();
        if (neonButton("AUTO-TRANSFER", BtnKind::Ghost, {-FLT_MIN, 0})) {
            editor = EditorState{};
            editor.open = true;
            std::snprintf(editor.label, sizeof editor.label, "stack to cold wallet");
            std::snprintf(editor.contract, sizeof editor.contract, "eosio.token");
            std::snprintf(editor.action, sizeof editor.action, "transfer");
            std::snprintf(editor.dataBuf, sizeof editor.dataBuf,
                          "{\"from\":\"{actor}\",\"to\":\"\",\"quantity\":\"\","
                          "\"memo\":\"stacked by TackleBox {date}\"}");
            editor.amountMode = 1;
            std::snprintf(editor.tokenCode, sizeof editor.tokenCode, "%s",
                          coreCode.c_str());
        }
        ImGui::TableNextColumn();
        if (neonButton("AUTO-STAKE", BtnKind::Ghost, {-FLT_MIN, 0})) {
            editor = EditorState{};
            editor.open = true;
            std::snprintf(editor.label, sizeof editor.label, "auto-stake CPU");
            std::snprintf(editor.contract, sizeof editor.contract, "eosio");
            std::snprintf(editor.action, sizeof editor.action, "delegatebw");
            std::snprintf(editor.dataBuf, sizeof editor.dataBuf,
                          "{\"from\":\"{actor}\",\"receiver\":\"{actor}\","
                          "\"stake_net_quantity\":\"0.%04d %s\","
                          "\"stake_cpu_quantity\":\"\",\"transfer\":false}",
                          0, coreCode.c_str());
            editor.amountMode = 1;
            std::snprintf(editor.tokenCode, sizeof editor.tokenCode, "%s",
                          coreCode.c_str());
            std::snprintf(editor.amountField, sizeof editor.amountField,
                          "stake_cpu_quantity");
        }
        ImGui::TableNextColumn();
        if (neonButton("AUTO-PROXY", BtnKind::Ghost, {-FLT_MIN, 0})) {
            editor = EditorState{};
            editor.open = true;
            std::snprintf(editor.label, sizeof editor.label, "keep vote proxied");
            std::snprintf(editor.contract, sizeof editor.contract, "eosio");
            std::snprintf(editor.action, sizeof editor.action, "voteproducer");
            std::snprintf(editor.dataBuf, sizeof editor.dataBuf,
                          "{\"voter\":\"{actor}\",\"proxy\":\"\",\"producers\":[]}");
            editor.intervalValue = 7;
            editor.intervalUnit = 2;
        }
        ImGui::EndTable();
    }
    vspace(10);

    if (state.vault.schedules.empty()) {
        emptyState(Icon::Clock, "Nothing on autopilot",
                   "Create a schedule here, or use SCHEDULE THIS VOTE on the Governance "
                   "page.");
    }

    int schedFrom = -1, schedTo = -1;
    for (size_t si = 0; si < state.vault.schedules.size(); ++si) {
        const Schedule& schedule = state.vault.schedules[si];
        ImGui::PushID(schedule.id.c_str());
        if (beginCard("sched")) {
            int dropped =
                dragGrip("##schedules", static_cast<int>(si), schedule.label.c_str());
            if (dropped >= 0) {
                schedFrom = dropped;
                schedTo = static_cast<int>(si);
            }
            ImGui::SameLine(0, 8);
            ImGui::PushFont(fonts().uiSemi, kTextLg);
            ImGui::TextUnformatted(schedule.label.c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            if (schedule.enabled)
                badgeFilled("ARMED", col::Violet);
            else
                badge("PAUSED", col::Slate);
            ImGui::SameLine();
            if (hasAutoRule(state, schedule))
                badgeFilled("RULE OK", col::Success);
            else
                badgeFilled("NO AUTO-SIGN RULE", col::Warn);

            monoText(schedule.contract + "::" + schedule.action + "  @" + schedule.actor,
                     col::Steel, kMonoSm);
            std::string when;
            switch (schedule.timingMode) {
                case Schedule::TimeDaily:
                    when = "daily at " + autopilot::formatTimeOfDay(
                                             schedule.dailySec < 0 ? 0 : schedule.dailySec);
                    break;
                case Schedule::TimeAnchored:
                    when = "grid every " + humanInterval(schedule.intervalSec);
                    break;
                default:
                    when = "every " + humanInterval(schedule.intervalSec);
            }
            when += schedule.nextRunAt > nowSec()
                        ? "  |  next in " + formatAgo(2 * nowSec() - schedule.nextRunAt)
                        : "  |  due now";
            if (schedule.startAt > nowSec())
                when += "  |  starts " + autopilot::formatDateTimeLocal(schedule.startAt);
            if (schedule.endAt > 0)
                when += "  |  ends " + autopilot::formatDateTimeLocal(schedule.endAt);
            if (schedule.lastRunAt)
                when += "  |  last " + formatAgo(schedule.lastRunAt) + " ago";
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
            ImGui::TextWrapped("%s", when.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            if (!schedule.lastResult.empty()) {
                bool ok = schedule.lastResult.rfind("signed", 0) == 0;
                ImGui::PushFont(fonts().mono, kMonoSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(ok ? col::Success : col::Warn));
                ImGui::TextWrapped("last result: %s", schedule.lastResult.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            vspace(4);

            if (neonButton("RUN NOW", BtnKind::Ghost, {100, 32},
                           state.schedulesInFlight.count(schedule.id) > 0))
                controller.runScheduleNow(schedule.id);
            ImGui::SameLine(0, 6);
            if (neonButton(schedule.enabled ? "PAUSE" : "ARM", BtnKind::Subtle, {80, 32})) {
                Schedule updated = schedule;
                updated.enabled = !updated.enabled;
                controller.saveSchedule(updated);
            }
            ImGui::SameLine(0, 6);
            if (neonButton("EDIT", BtnKind::Subtle, {70, 32})) {
                editor = EditorState{};
                editor.open = true;
                editor.id = schedule.id;
                std::snprintf(editor.label, sizeof editor.label, "%s",
                              schedule.label.c_str());
                std::snprintf(editor.contract, sizeof editor.contract, "%s",
                              schedule.contract.c_str());
                std::snprintf(editor.action, sizeof editor.action, "%s",
                              schedule.action.c_str());
                std::snprintf(editor.dataBuf, sizeof editor.dataBuf, "%s",
                              schedule.data.dump().c_str());
                if (schedule.intervalSec % 86400 == 0) {
                    editor.intervalUnit = 2;
                    editor.intervalValue = static_cast<int>(schedule.intervalSec / 86400);
                } else if (schedule.intervalSec % 3600 == 0) {
                    editor.intervalUnit = 1;
                    editor.intervalValue = static_cast<int>(schedule.intervalSec / 3600);
                } else {
                    editor.intervalUnit = 0;
                    editor.intervalValue = static_cast<int>(schedule.intervalSec / 60);
                }
                editor.runMissed = schedule.runMissedOnUnlock;
                editor.timingMode = schedule.timingMode;
                if (schedule.dailySec >= 0)
                    std::snprintf(editor.dailyBuf, sizeof editor.dailyBuf, "%s",
                                  autopilot::formatTimeOfDay(schedule.dailySec).c_str());
                if (schedule.startAt > 0)
                    std::snprintf(editor.startBuf, sizeof editor.startBuf, "%s",
                                  autopilot::formatDateTimeLocal(schedule.startAt).c_str());
                if (schedule.endAt > 0)
                    std::snprintf(editor.endBuf, sizeof editor.endBuf, "%s",
                                  autopilot::formatDateTimeLocal(schedule.endAt).c_str());
                editor.amountMode = schedule.amountMode == Schedule::AmountPercent ? 1 : 0;
                std::snprintf(editor.percentBuf, sizeof editor.percentBuf, "%g",
                              schedule.amountPercent > 0 ? schedule.amountPercent : 10.0);
                if (!schedule.amountTokenContract.empty())
                    std::snprintf(editor.tokenContract, sizeof editor.tokenContract, "%s",
                                  schedule.amountTokenContract.c_str());
                std::snprintf(editor.tokenCode, sizeof editor.tokenCode, "%s",
                              schedule.amountTokenCode.c_str());
                if (!schedule.amountField.empty())
                    std::snprintf(editor.amountField, sizeof editor.amountField, "%s",
                                  schedule.amountField.c_str());
                std::snprintf(editor.reserveBuf, sizeof editor.reserveBuf, "%s",
                              schedule.amountReserve.c_str());
            }
            ImGui::SameLine(0, 6);
            if (iconButton("##delsched", Icon::Trash, "Delete schedule", col::Danger, 14.0f))
                controller.removeSchedule(schedule.id);

            // The rule handoff: draft the exact auto-sign rule this needs.
            if (!hasAutoRule(state, schedule)) {
                ImGui::SameLine(0, 12);
                if (neonButton("DRAFT THE RULE", BtnKind::Primary, {140, 32})) {
                    guard::WhitelistRule rule;
                    rule.id = uuid4();
                    rule.chainId = schedule.chainId;
                    rule.signer = schedule.actor + "@" + schedule.permission;
                    rule.contract = schedule.contract;
                    rule.action = schedule.action;
                    rule.autoSign = true;
                    rule.note = "autopilot: " + schedule.label;
                    rule.createdAt = nowSec();
                    std::string dynamicField =
                        schedule.amountMode == Schedule::AmountPercent
                            ? (schedule.amountField.empty() ? "quantity"
                                                            : schedule.amountField)
                            : "";
                    if (schedule.data.is_object())
                        for (auto it = schedule.data.begin(); it != schedule.data.end(); ++it) {
                            // Run-time values (dynamic amounts, {placeholders})
                            // cannot be pinned Exact - leave them Any; the user
                            // can tighten to a Range in the editor.
                            if (it.key() == dynamicField) continue;
                            if (it.value().is_string() &&
                                it.value().get<std::string>().find('{') !=
                                    std::string::npos)
                                continue;
                            guard::ParamConstraint constraint;
                            constraint.kind = guard::ConstraintKind::Exact;
                            constraint.values = {it.value()};
                            rule.params[it.key()] = constraint;
                        }
                    openRuleEditor(rule);
                    state.page = Page::Whitelist;
                }
            }
        }
        endCard();
        // The whole card is a drop zone for schedule reordering.
        if (int dropped = acceptDropOnLastItem("##schedules"); dropped >= 0) {
            schedFrom = dropped;
            schedTo = static_cast<int>(si);
        }
        ImGui::PopID();
        vspace(8);
    }
    if (schedFrom >= 0 && schedTo >= 0 && schedFrom != schedTo)
        controller.moveSchedule(static_cast<size_t>(schedFrom),
                                static_cast<size_t>(schedTo));

    drawEditor(state, controller);
}

}  // namespace tb::ui
