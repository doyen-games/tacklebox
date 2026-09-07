// Root of the UI: routes between onboarding / unlock / the main shell and
// hosts the global overlays (signing modal, plugin prompt, toasts,
// diagnostics).
#pragma once

#include "app/controller.hpp"
#include "app/state.hpp"

namespace tb::ui {

void drawApp(AppState& state, Controller& controller);

// Views (one translation unit each).
void drawOnboarding(AppState& state, Controller& controller);
void drawUnlock(AppState& state, Controller& controller);
void drawShell(AppState& state, Controller& controller);
void drawDashboard(AppState& state, Controller& controller);
void drawExplore(AppState& state, Controller& controller);
void drawAssets(AppState& state, Controller& controller);
void drawResources(AppState& state, Controller& controller);
void drawGovernance(AppState& state, Controller& controller);
void drawMsig(AppState& state, Controller& controller);
void drawAutopilot(AppState& state, Controller& controller);
void drawSetup(AppState& state, Controller& controller);
void drawTransfer(AppState& state, Controller& controller);
void drawContracts(AppState& state, Controller& controller);
void drawWhitelist(AppState& state, Controller& controller);
void drawVaultView(AppState& state, Controller& controller);
void drawCreateAccount(AppState& state, Controller& controller);
void drawHistory(AppState& state, Controller& controller);
void drawSettings(AppState& state, Controller& controller);

// Global modals (drawn last, above everything).
void drawSignModal(AppState& state, Controller& controller);
void drawPluginPrompt(AppState& state, Controller& controller);

// Whitelist editor entry point: any view (whitelist page, signing modal,
// history) can stage a prefilled draft; the app-level modal opens next draw.
void openRuleEditor(const guard::WhitelistRule& draft);
// The rule editor modal itself; drawApp draws it once per frame, above the
// signing modal, so rules can be created mid-prompt.
void drawRuleEditorModal(AppState& state, Controller& controller);
void closeRuleEditor();  // discard any open draft (QA step isolation)

// Autopilot schedule editor entry point (QA tour).
void openScheduleEditor();

// Auto Stake Wizard entry point (autopilot page + QA tour). step clamps to
// [0, 5]; passing 4+ seeds demo values so review screenshots read well.
void openAutoStakeWizard(int step);
void closeAutopilotModals();  // QA step isolation (schedule editor + wizard)

// Msig template editor entry point (QA tour).
void openMsigTemplateEditor();

// Contact editor entry points (transfer page owns the modal).
void openContactEditor(const Contact& prefill, bool isNew);
void drawContactEditorModal(Controller& controller);

// Reveal-key modal entry point (QA tour; shows the password-check state).
void openRevealModal(const std::string& pub);

}  // namespace tb::ui
