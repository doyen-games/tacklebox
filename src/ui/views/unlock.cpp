#include <algorithm>
#include <cstring>

#include "ui/app_ui.hpp"
#include "ui/fx.hpp"
#include "ui/widgets.hpp"

namespace tb::ui {

// Shared wordmark for unlock/onboarding.
static void wordmark(float centerX, float y) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawAnchorMark(dl, {centerX, y}, 46.0f, col::Cyan, 0.9f);

    const char* name = "TACKLEBOX";
    ImGui::PushFont(fonts().uiBold, 34.0f);
    // Manual letter-spacing: draw per glyph.
    float spacing = 7.0f;
    float total = 0;
    for (const char* p = name; *p; ++p) {
        char one[2] = {*p, 0};
        total += ImGui::CalcTextSize(one).x + (*(p + 1) ? spacing : 0);
    }
    float x = centerX - total * 0.5f;
    float textY = y + 42.0f;
    for (const char* p = name; *p; ++p) {
        char one[2] = {*p, 0};
        dl->AddText({x, textY}, col::Ice, one);
        x += ImGui::CalcTextSize(one).x + spacing;
    }
    ImGui::PopFont();

    ImGui::PushFont(fonts().uiMedium, kTextSm);
    const char* tag = "NAVIGATE THE OPEN SEA";
    ImVec2 tagSize = ImGui::CalcTextSize(tag);
    dl->AddText({centerX - tagSize.x * 0.5f, textY + 44.0f}, col::alpha(col::Steel, 0.9f), tag);
    ImGui::PopFont();
}

void drawUnlock(AppState& state, Controller& controller) {
    ImVec2 min = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    ImVec2 max{min.x + size.x, min.y + size.y};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    backdrop(dl, min, max, ImGui::GetTime());
    scanline(dl, min, max, ImGui::GetTime(), col::Cyan);

    float centerX = min.x + size.x * 0.5f;
    float top = min.y + size.y * 0.24f;
    wordmark(centerX, top);

    // Autopilot standby: the UI is locked while schedules keep running. The
    // note sits below the tagline (wordmark bottom = top + 42 + 44 + line),
    // and the password card shifts down with it.
    float cardTop = top + 130.0f;
    if (state.standbyLocked) {
        ImGui::PushFont(fonts().uiSemi, kTextSm);
        const char* note = "AUTOPILOT STANDBY - SCHEDULES STILL RUNNING";
        ImVec2 noteSize = ImGui::CalcTextSize(note);
        float noteY = top + 86.0f + ImGui::GetTextLineHeight() + 14.0f;
        dl->AddText({centerX - noteSize.x * 0.5f, noteY},
                    col::alpha(col::Warn, 0.95f), note);
        cardTop = noteY + ImGui::GetTextLineHeight() + 26.0f;
        ImGui::PopFont();
    }

    // Card with the password prompt.
    float cardW = std::min(380.0f, size.x - 36.0f);
    static char password[256] = {};
    static float shake = 0.0f;
    shake = std::max(0.0f, shake - ImGui::GetIO().DeltaTime * 3.0f);
    float shakeOff = shake > 0 ? std::sin(shake * 40.0f) * 6.0f * shake : 0.0f;
    ImGui::SetCursorScreenPos({centerX - cardW * 0.5f + shakeOff, cardTop});

    ImGui::BeginGroup();
    ImGui::PushItemWidth(cardW);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW);

    FieldOpts opts;
    opts.password = true;
    opts.placeholder = "vault password";
    opts.width = cardW;
    opts.autoFocus = true;
    opts.error = state.unlockError.empty() ? nullptr : state.unlockError.c_str();
    bool entered = textField("##unlockpw", password, sizeof password, opts);

    vspace(6);
    bool submit = false;
    if (state.busyUnlock) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cardW * 0.5f - 12);
        spinner(12.0f);
    } else {
        submit = neonButton("UNLOCK", BtnKind::Primary, {cardW, 44});
    }
    if ((entered || submit) && !state.busyUnlock && password[0]) {
        controller.unlockVault(password);
        secureWipe(password, sizeof password);
    }
    // Trigger the shake on a fresh error.
    static std::string lastError;
    if (state.unlockError != lastError) {
        lastError = state.unlockError;
        if (!lastError.empty()) shake = 1.0f;
    }

    vspace(10);
    ImGui::PushFont(fonts().ui, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
    ImGui::TextWrapped(
        "Keys, networks and whitelist rules stay sealed until the vault is open. "
        "Auto-lock re-seals after inactivity. To restore from another machine, "
        "drop a .tbx vault file anywhere on this window.");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::PopTextWrapPos();
    ImGui::PopItemWidth();
    ImGui::EndGroup();
}

}  // namespace tb::ui
