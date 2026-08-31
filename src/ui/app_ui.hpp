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

// Whitelist editor entry point: other views (the signing modal) can stage a
// prefilled draft; the Whitelist page opens its editor on next draw.
void openRuleEditor(const guard::WhitelistRule& draft);

}  // namespace tb::ui
