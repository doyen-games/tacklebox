#include "ui/app_ui.hpp"

#include "core/log.hpp"
#include "core/util.hpp"
#include "ui/fx.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace tb::ui {

static void drawDiagnostics(AppState& state) {
    if (!state.showDiagnostics) return;
    ImGui::SetNextWindowSize({640, 320}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.96f);
    if (ImGui::Begin("Diagnostics", &state.showDiagnostics,
                     ImGuiWindowFlags_NoCollapse)) {
        ImGui::PushFont(fonts().mono, kMonoSm);
        for (const auto& entry : Log::tail(300)) {
            ImU32 color = entry.level == LogLevel::Error  ? col::Danger
                          : entry.level == LogLevel::Warn ? col::Warn
                                                          : col::Steel;
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(color));
            ImGui::TextWrapped("%s  %s", formatLocal(entry.timeMs / 1000).c_str(),
                               entry.message.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
        ImGui::PopFont();
    }
    ImGui::End();
}

void drawApp(AppState& state, Controller& controller) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

    if (!state.vaultExists)
        drawOnboarding(state, controller);
    else if (!state.unlocked)
        drawUnlock(state, controller);
    else
        drawShell(state, controller);

    ImGui::End();

    drawSignModal(state, controller);
    drawPluginPrompt(state, controller);
    drawDiagnostics(state);
    drawToasts(state);

    if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) state.showDiagnostics = !state.showDiagnostics;
    // Panic key: instant lock.
    if (state.unlocked && ImGui::IsKeyPressed(ImGuiKey_L, false) &&
        ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift) {
        controller.lockVault();
        controller.toast(Toast::Info, "Vault locked");
    }
}

}  // namespace tb::ui
