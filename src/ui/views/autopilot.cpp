// Autopilot: recurring transactions that only ever execute through the
// guard's auto-sign fast path. A schedule is a clock, never a fourth way to
// sign - no matching pinned auto-sign rule means the run is skipped.
#include <algorithm>
#include <cctype>
#include <cstring>

#include "app/account_util.hpp"
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
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            editor.open = false;
            ImGui::CloseCurrentPopup();
        }
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

        if (beginFieldPair("##ca")) {
            nextField();
            {
                FieldOpts f;
                f.mono = true;
                textField("Contract", editor.contract, sizeof editor.contract, f);
            }
            nextField();
            {
                FieldOpts f;
                f.mono = true;
                textField("Action", editor.action, sizeof editor.action, f);
            }
            endFieldPair();
        }

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
        if (beginFieldPair("##window")) {
            nextField();
            {
                FieldOpts f;
                f.mono = true;
                f.placeholder = "now  (or YYYY-MM-DD HH:MM:SS)";
                textField(editor.timingMode == Schedule::TimeAnchored
                              ? "Start / grid origin"
                              : "Start",
                          editor.startBuf, sizeof editor.startBuf, f);
            }
            nextField();
            {
                FieldOpts f;
                f.mono = true;
                f.placeholder = "never  (or YYYY-MM-DD HH:MM:SS)";
                textField("End", editor.endBuf, sizeof editor.endBuf, f);
            }
            endFieldPair();
        }

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

// --- Auto Stake Wizard -------------------------------------------------------
// One guided flow that arms the whole earn loop in the right order: vote
// through a proxy (rewards accrue) -> claim rewards daily -> stake the claim
// -> optionally sweep surplus to cold storage. Every stage is a pinned
// auto-sign whitelist rule plus a schedule; signing still happens ONLY
// through the guard's auto-sign fast path - the wizard has no signing power.

struct WizardStage {
    std::string title;
    guard::WhitelistRule rule;
    Schedule schedule;
    bool runNow = false;  // vote executes immediately once armed
    int status = 0;       // 0 pending, 1 pinning rule, 2 armed, 3 failed
    std::string error;
};

struct WizardState {
    bool open = false;
    int step = 0;  // 0 pipeline, 1 vote, 2 claim, 3 stake, 4 sweep, 5 review, 6 arming
    // vote
    bool voteOn = true;
    char proxy[16] = {};
    int voteDays = 7;
    // claim
    bool claimOn = true;
    char claimContract[16] = "eosio";
    char claimAction[16] = "claimgbmvote";
    char claimData[256] = "{\"owner\":\"{actor}\"}";
    char claimTime[12] = "09:00:00";
    // stake
    bool stakeOn = true;
    char stakePercent[8] = "50";
    int stakeResource = 0;  // 0 CPU, 1 NET
    char stakeTime[12] = "09:10:00";
    // sweep
    bool sweepOn = false;
    char sweepTo[16] = {};
    char sweepPercent[8] = "75";
    char sweepMemo[64] = "swept by TackleBox {date}";
    // arming
    bool flipAutoSign = true;
    std::vector<WizardStage> stages;
    bool armed = false;
};
WizardState wiz;

void wizardArmStage(Controller& controller, size_t index) {
    if (index >= wiz.stages.size()) {
        // Everything armed: the vote executes right away (through its fresh
        // pinned rule), the daily stages wait for their exact times.
        wiz.armed = true;
        for (const auto& stage : wiz.stages)
            if (stage.runNow) controller.runScheduleNow(stage.schedule.id);
        return;
    }
    WizardStage& stage = wiz.stages[index];
    stage.status = 1;
    stage.error.clear();
    Controller* ctrl = &controller;  // outlives the app loop
    controller.saveRule(stage.rule, /*pin=*/true,
                        [ctrl, index](bool ok, std::string error) {
                            if (index >= wiz.stages.size()) return;  // wizard reset
                            WizardStage& s = wiz.stages[index];
                            if (!ok) {
                                s.status = 3;
                                s.error = std::move(error);
                                return;
                            }
                            ctrl->saveSchedule(s.schedule);
                            s.status = 2;
                            wizardArmStage(*ctrl, index + 1);
                        });
}

// Build the stage list from the wizard inputs; ids are minted here ONCE so a
// retry after a failure upserts instead of duplicating.
bool wizardBuildStages(AppState& state, Controller& controller) {
    const AccountRef* account = state.currentAccount();
    const NetworkDef* net = state.currentNetwork();
    if (!account || !net) return false;
    std::string signer = account->display();
    std::string coreCode = net->coreSymbolCode();
    std::string zero = acct::formatStake("0", net->coreSymbol).value_or("0.0000 " + coreCode);

    auto baseRule = [&](const char* what) {
        guard::WhitelistRule rule;
        rule.id = uuid4();
        rule.chainId = account->chainId;
        rule.signer = signer;
        rule.autoSign = true;
        rule.createdAt = nowSec();
        rule.note = std::string("auto stake wizard: ") + what;
        return rule;
    };
    auto baseSchedule = [&](const char* label) {
        Schedule schedule;
        schedule.id = uuid4();
        schedule.label = label;
        schedule.chainId = account->chainId;
        schedule.actor = account->actor;
        schedule.permission = account->permission;
        return schedule;
    };
    auto exact = [](const json& value) {
        guard::ParamConstraint constraint;
        constraint.kind = guard::ConstraintKind::Exact;
        constraint.values = {value};
        return constraint;
    };

    wiz.stages.clear();
    if (wiz.voteOn) {
        WizardStage stage;
        stage.title = std::string("Vote via proxy ") + wiz.proxy;
        stage.rule = baseRule("keep the vote proxied");
        stage.rule.contract = "eosio";
        stage.rule.action = "voteproducer";
        stage.rule.params["proxy"] = exact(json(toLower(trim(wiz.proxy))));
        stage.rule.params["producers"] = exact(json::array());
        stage.schedule = baseSchedule("wizard: keep vote proxied");
        stage.schedule.contract = "eosio";
        stage.schedule.action = "voteproducer";
        stage.schedule.data = json{{"voter", "{actor}"},
                                   {"proxy", toLower(trim(wiz.proxy))},
                                   {"producers", json::array()}};
        stage.schedule.intervalSec = int64_t(wiz.voteDays) * 86400;
        stage.runNow = true;
        wiz.stages.push_back(std::move(stage));
    }
    if (wiz.claimOn) {
        auto tod = autopilot::parseTimeOfDay(wiz.claimTime);
        auto data = json::parse(std::string(wiz.claimData), nullptr, false);
        if (!tod || data.is_discarded() || !data.is_object()) return false;
        WizardStage stage;
        stage.title = std::string("Claim rewards daily at ") + wiz.claimTime;
        stage.rule = baseRule("claim rewards");
        stage.rule.contract = toLower(trim(wiz.claimContract));
        stage.rule.action = toLower(trim(wiz.claimAction));
        for (auto it = data.begin(); it != data.end(); ++it) {
            if (it.value().is_string() &&
                it.value().get<std::string>().find('{') != std::string::npos)
                continue;  // run-time placeholder: leave unconstrained
            stage.rule.params[it.key()] = exact(it.value());
        }
        stage.schedule = baseSchedule("wizard: claim rewards");
        stage.schedule.contract = stage.rule.contract;
        stage.schedule.action = stage.rule.action;
        stage.schedule.data = data;
        stage.schedule.timingMode = Schedule::TimeDaily;
        stage.schedule.dailySec = *tod;
        stage.schedule.intervalSec = 86400;
        wiz.stages.push_back(std::move(stage));
    }
    if (wiz.stakeOn) {
        auto tod = autopilot::parseTimeOfDay(wiz.stakeTime);
        double percent = std::atof(wiz.stakePercent);
        if (!tod || percent <= 0 || percent > 100) return false;
        const char* dynField =
            wiz.stakeResource == 0 ? "stake_cpu_quantity" : "stake_net_quantity";
        const char* zeroField =
            wiz.stakeResource == 0 ? "stake_net_quantity" : "stake_cpu_quantity";
        WizardStage stage;
        stage.title = std::string("Stake ") + trim(wiz.stakePercent) + "% to " +
                      (wiz.stakeResource == 0 ? "CPU" : "NET") + " daily at " +
                      wiz.stakeTime;
        stage.rule = baseRule("stake the claim");
        stage.rule.contract = "eosio";
        stage.rule.action = "delegatebw";
        stage.rule.params["transfer"] = exact(json(false));
        stage.rule.params[zeroField] = exact(json(zero));
        stage.schedule = baseSchedule("wizard: stake the claim");
        stage.schedule.contract = "eosio";
        stage.schedule.action = "delegatebw";
        stage.schedule.data = json{{"from", "{actor}"},
                                   {"receiver", "{actor}"},
                                   {"stake_cpu_quantity", zero},
                                   {"stake_net_quantity", zero},
                                   {"transfer", false}};
        stage.schedule.amountMode = Schedule::AmountPercent;
        stage.schedule.amountPercent = percent;
        stage.schedule.amountTokenContract = "eosio.token";
        stage.schedule.amountTokenCode = coreCode;
        stage.schedule.amountField = dynField;
        stage.schedule.timingMode = Schedule::TimeDaily;
        stage.schedule.dailySec = *tod;
        stage.schedule.intervalSec = 86400;
        wiz.stages.push_back(std::move(stage));
    }
    if (wiz.sweepOn) {
        double percent = std::atof(wiz.sweepPercent);
        std::string to = toLower(trim(wiz.sweepTo));
        if (to.empty() || percent <= 0 || percent > 100) return false;
        WizardStage stage;
        stage.title = "Sweep " + trim(wiz.sweepPercent) + "% to " + to + " weekly";
        stage.rule = baseRule("sweep to cold storage");
        stage.rule.contract = "eosio.token";
        stage.rule.action = "transfer";
        stage.rule.params["to"] = exact(json(to));
        stage.schedule = baseSchedule("wizard: sweep to cold storage");
        stage.schedule.contract = "eosio.token";
        stage.schedule.action = "transfer";
        stage.schedule.data = json{{"from", "{actor}"},
                                   {"to", to},
                                   {"quantity", zero},
                                   {"memo", wiz.sweepMemo}};
        stage.schedule.amountMode = Schedule::AmountPercent;
        stage.schedule.amountPercent = percent;
        stage.schedule.amountTokenContract = "eosio.token";
        stage.schedule.amountTokenCode = coreCode;
        stage.schedule.amountField = "quantity";
        stage.schedule.intervalSec = 7 * 86400;
        wiz.stages.push_back(std::move(stage));
    }
    (void)controller;
    return !wiz.stages.empty();
}

void drawAutoStakeWizard(AppState& state, Controller& controller) {
    if (wiz.open) ImGui::OpenPopup("Auto Stake Wizard");
    if (!beginAdaptiveModal("Auto Stake Wizard", 640.0f)) return;
    if (!wiz.open) {
        ImGui::CloseCurrentPopup();
        endAdaptiveModal();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) wiz.open = false;

    static const char* kStepNames[] = {"PIPELINE", "VOTE",   "CLAIM",
                                       "STAKE",    "SWEEP",  "REVIEW", "ARM"};
    heading("Auto Stake Wizard", 24.0f);
    ImGui::PushFont(fonts().uiSemi, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::CyanDim));
    ImGui::Text("step %d of 6  -  %s", wiz.step + 1, kStepNames[wiz.step]);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    vspace(6);
    beginModalBody("##wizbody", ImGui::GetFrameHeight() * 2.4f + 60.0f);

    const AccountRef* account = state.currentAccount();
    const NetworkDef* net = state.currentNetwork();
    std::string coreCode = net ? net->coreSymbolCode() : "EOS";

    switch (wiz.step) {
        case 0: {
            subtext("Arms the whole earn loop in the correct order. Each stage is a "
                    "schedule bound to a pinned auto-sign whitelist rule - the guard "
                    "still checks every run.");
            vspace(6);
            if (account) kvRow("Account", account->display(), true);
            if (net) kvRow("Chain", net->name);
            vspace(6);
            sectionTitle("The pipeline");
            subtext("1. Vote through a proxy - rewards start accruing (runs "
                    "immediately, then re-votes on an interval).");
            subtext("2. Claim rewards - every day at an exact time.");
            subtext("3. Stake the claim - a percent of the fresh liquid balance, "
                    "shortly after the claim.");
            subtext("4. Sweep surplus to cold storage - optional, weekly.");
            break;
        }
        case 1: {
            toggle("Include the vote stage", &wiz.voteOn,
                   "Re-votes through the proxy so the vote never decays");
            if (wiz.voteOn) {
                // Prefill from the live vote once.
                if (!wiz.proxy[0] && account) {
                    AccountData& data = state.accountData[state.accountKey(*account)];
                    if (data.loaded && data.snap.raw.contains("voter_info") &&
                        data.snap.raw["voter_info"].is_object())
                        std::snprintf(wiz.proxy, sizeof wiz.proxy, "%s",
                                      data.snap.raw["voter_info"]
                                          .value("proxy", std::string())
                                          .c_str());
                }
                FieldOpts opts;
                opts.mono = true;
                opts.placeholder = "proxy account";
                textField("Proxy", wiz.proxy, sizeof wiz.proxy, opts);
                ImGui::AlignTextToFramePadding();
                ImGui::PushFont(fonts().uiSemi, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
                ImGui::TextUnformatted("Re-vote every");
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::SameLine(0, 8);
                ImGui::SetNextItemWidth(::ui::S(110.0f));
                ImGui::InputInt("##votedays", &wiz.voteDays);
                if (wiz.voteDays < 1) wiz.voteDays = 1;
                ImGui::SameLine(0, 6);
                subtext("days (the first vote fires as soon as the pipeline arms)");
            }
            break;
        }
        case 2: {
            toggle("Include the claim stage", &wiz.claimOn,
                   "Claims voter rewards on a daily clock");
            if (wiz.claimOn) {
                if (beginFieldPair("##claimrow")) {
                    nextField();
                    {
                        FieldOpts opts;
                        opts.mono = true;
                        textField("Contract", wiz.claimContract,
                                  sizeof wiz.claimContract, opts);
                    }
                    nextField();
                    {
                        FieldOpts opts;
                        opts.mono = true;
                        textField("Action", wiz.claimAction, sizeof wiz.claimAction,
                                  opts);
                    }
                    endFieldPair();
                }
                {
                    FieldOpts opts;
                    opts.mono = true;
                    opts.hint = "string values may use {actor}";
                    textField("Data", wiz.claimData, sizeof wiz.claimData, opts);
                }
                {
                    FieldOpts opts;
                    opts.mono = true;
                    opts.width = ::ui::S(120.0f);
                    opts.hint = "local time, HH:MM:SS";
                    textField("Daily at", wiz.claimTime, sizeof wiz.claimTime, opts);
                }
                subtext("WAX voters: eosio::claimgbmvote. Adjust for your chain's "
                        "reward action.");
            }
            break;
        }
        case 3: {
            toggle("Include the stake stage", &wiz.stakeOn,
                   "Stakes a percent of the fresh liquid balance every day");
            if (wiz.stakeOn) {
                {
                    FieldOpts opts;
                    opts.mono = true;
                    opts.width = ::ui::S(110.0f);
                    opts.hint = "percent (0-100]";
                    textField("Stake", wiz.stakePercent, sizeof wiz.stakePercent, opts);
                }
                ImGui::AlignTextToFramePadding();
                ImGui::PushFont(fonts().uiSemi, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
                ImGui::TextUnformatted(("% of liquid " + coreCode + " into").c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::SameLine(0, 8);
                ImGui::SetNextItemWidth(::ui::S(110.0f));
                const char* resources[] = {"CPU", "NET"};
                if (qa::forceOpen("wiz-stake-res")) qa::openCombo("##stakeres");
                ImGui::Combo("##stakeres", &wiz.stakeResource, resources, 2);
                ::ui::HandOnHover();
                {
                    FieldOpts opts;
                    opts.mono = true;
                    opts.width = ::ui::S(120.0f);
                    opts.hint = "local time, HH:MM:SS";
                    textField("Daily at", wiz.stakeTime, sizeof wiz.stakeTime, opts);
                }
                subtext("Runs just after the claim so the freshly claimed rewards are "
                        "part of the balance.");
            }
            break;
        }
        case 4: {
            toggle("Include the sweep stage (optional)", &wiz.sweepOn,
                   "Weekly transfer of surplus liquid tokens to another wallet");
            if (wiz.sweepOn) {
                FieldOpts opts;
                opts.mono = true;
                opts.placeholder = "cold wallet account";
                textField("To", wiz.sweepTo, sizeof wiz.sweepTo, opts);
                opts.placeholder = "percent (0-100]";
                opts.width = ::ui::S(110.0f);
                textField("Sweep", wiz.sweepPercent, sizeof wiz.sweepPercent, opts);
                FieldOpts memoOpts;
                memoOpts.mono = true;
                textField("Memo", wiz.sweepMemo, sizeof wiz.sweepMemo, memoOpts);
            }
            break;
        }
        case 5: {
            sectionTitle("Review");
            int order = 0;
            auto line = [&](bool on, const std::string& text) {
                if (!on) return;
                ++order;
                ImGui::PushFont(fonts().ui, kText);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Ice));
                ImGui::Text("%d.", order);
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::SameLine(0, 8);
                ImGui::PushFont(fonts().ui, kText);
                ImGui::TextWrapped("%s", text.c_str());
                ImGui::PopFont();
            };
            line(wiz.voteOn, "Vote via proxy " + std::string(wiz.proxy) +
                                 " - immediately, then every " +
                                 std::to_string(wiz.voteDays) + " days");
            line(wiz.claimOn, std::string(wiz.claimContract) + "::" + wiz.claimAction +
                                  " - daily at " + wiz.claimTime);
            line(wiz.stakeOn, "Stake " + std::string(wiz.stakePercent) + "% of liquid " +
                                  coreCode + " to " +
                                  (wiz.stakeResource == 0 ? "CPU" : "NET") +
                                  " - daily at " + wiz.stakeTime);
            line(wiz.sweepOn, "Sweep " + std::string(wiz.sweepPercent) + "% to " +
                                  wiz.sweepTo + " - weekly");
            vspace(6);
            subtext("Arming creates one pinned auto-sign whitelist rule per stage "
                    "(fetching the live contract hashes) plus its schedule.");
            if (!state.vault.security.allowAutoSign)
                toggle("Enable the auto-sign master switch", &wiz.flipAutoSign,
                       "Off, every run would be recorded as blocked. This flips "
                       "Settings > Security policy > allow auto-sign");
            break;
        }
        case 6: {
            sectionTitle("Arming the pipeline");
            for (const auto& stage : wiz.stages) {
                switch (stage.status) {
                    case 1: spinner(10.0f); break;
                    case 2: statusDot(col::Success); break;
                    case 3: statusDot(col::Danger); break;
                    default: statusDot(col::Slate); break;
                }
                ImGui::SameLine(0, 8);
                ImGui::PushFont(fonts().ui, kText);
                ImGui::TextUnformatted(stage.title.c_str());
                ImGui::PopFont();
                if (!stage.error.empty()) {
                    ImGui::PushFont(fonts().ui, kTextSm);
                    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
                    ImGui::TextWrapped("   %s", stage.error.c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
            }
            if (wiz.armed) {
                vspace(6);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Success));
                ImGui::TextWrapped("Pipeline armed. The vote fired immediately; the "
                                   "daily stages run at their exact times.");
                ImGui::PopStyleColor();
            }
            break;
        }
    }

    endModalBody();
    vspace(6);

    // Footer: BACK / NEXT (context-sensitive), CLOSE on the arming screen.
    if (wiz.step == 6) {
        bool failed = false;
        size_t failedAt = 0;
        for (size_t i = 0; i < wiz.stages.size(); ++i)
            if (wiz.stages[i].status == 3) {
                failed = true;
                failedAt = i;
            }
        if (failed && neonButton("RETRY", BtnKind::Primary, {110, 40})) {
            wizardArmStage(controller, failedAt);
        }
        if (failed) ImGui::SameLine(0, 8);
        if (neonButton(wiz.armed ? "DONE" : "CLOSE", BtnKind::Ghost, {110, 40}))
            wiz.open = false;
    } else {
        if (neonButton("BACK", BtnKind::Ghost, {90, 40}, wiz.step == 0)) --wiz.step;
        ImGui::SameLine(0, 8);
        bool last = wiz.step == 5;
        if (neonButton(last ? "ARM PIPELINE" : "NEXT", BtnKind::Primary,
                       {last ? 150.0f : 110.0f, 40})) {
            // Per-step validation before advancing.
            bool ok = true;
            if (wiz.step == 1 && wiz.voteOn && !trim(wiz.proxy).size()) {
                controller.toast(Toast::Error, "Name the proxy account (or exclude "
                                               "the vote stage)");
                ok = false;
            }
            if (wiz.step == 2 && wiz.claimOn &&
                !autopilot::parseTimeOfDay(wiz.claimTime)) {
                controller.toast(Toast::Error, "Claim time must be HH:MM:SS");
                ok = false;
            }
            if (wiz.step == 3 && wiz.stakeOn) {
                double percent = std::atof(wiz.stakePercent);
                if (percent <= 0 || percent > 100 ||
                    !autopilot::parseTimeOfDay(wiz.stakeTime)) {
                    controller.toast(Toast::Error,
                                     "Stake needs a percent in (0,100] and an "
                                     "HH:MM:SS time");
                    ok = false;
                }
            }
            if (wiz.step == 4 && wiz.sweepOn && !trim(wiz.sweepTo).size()) {
                controller.toast(Toast::Error, "Name the sweep recipient (or turn "
                                               "the sweep off)");
                ok = false;
            }
            if (ok && last) {
                if (!wiz.voteOn && !wiz.claimOn && !wiz.stakeOn && !wiz.sweepOn) {
                    controller.toast(Toast::Error, "Every stage is excluded - "
                                                   "nothing to arm");
                } else if (!wizardBuildStages(state, controller)) {
                    controller.toast(Toast::Error,
                                     "Check the stage inputs - something did not "
                                     "parse");
                } else {
                    if (wiz.flipAutoSign && !state.vault.security.allowAutoSign) {
                        SecurityPrefs prefs = state.vault.security;
                        prefs.allowAutoSign = true;
                        controller.updateSecurity(prefs);
                    }
                    wiz.step = 6;
                    wizardArmStage(controller, 0);
                }
            } else if (ok) {
                ++wiz.step;
            }
        }
    }
    endAdaptiveModal();
}

}  // namespace

// QA hooks: the tour opens/closes these without a click.
void openScheduleEditor() {
    editor = EditorState{};
    editor.open = true;
}

void closeAutopilotModals() {
    editor = EditorState{};
    wiz = WizardState{};
}

// Entry points for the Auto Stake Wizard (autopilot page button + QA tour).
void openAutoStakeWizard(int step) {
    wiz = WizardState{};
    wiz.open = true;
    wiz.step = step < 0 ? 0 : (step > 5 ? 5 : step);
    if (step >= 4) {  // review demos read nicer with content
        std::snprintf(wiz.proxy, sizeof wiz.proxy, "greymassvote");
        wiz.sweepOn = true;
        std::snprintf(wiz.sweepTo, sizeof wiz.sweepTo, "coldwallet.x");
    }
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
        if (neonButton("AUTO STAKE WIZARD", BtnKind::Primary, {-FLT_MIN, 0}))
            openAutoStakeWizard(0);
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
    drawAutoStakeWizard(state, controller);
}

}  // namespace tb::ui
