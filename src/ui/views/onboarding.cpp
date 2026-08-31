#include <algorithm>
#include <cstring>

#include "ui/app_ui.hpp"
#include "ui/fx.hpp"
#include "ui/widgets.hpp"

namespace tb::ui {

// Rough entropy score 0..4 for the strength meter.
static int passwordScore(const char* pw) {
    size_t len = std::strlen(pw);
    if (len == 0) return 0;
    bool lower = false, upper = false, digit = false, symbol = false;
    for (const char* p = pw; *p; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (std::islower(c)) lower = true;
        else if (std::isupper(c)) upper = true;
        else if (std::isdigit(c)) digit = true;
        else symbol = true;
    }
    int classes = lower + upper + digit + symbol;
    if (len < 8) return 1;
    if (len >= 16 && classes >= 3) return 4;
    if (len >= 12 && classes >= 3) return 3;
    if (len >= 10 && classes >= 2) return 3;
    return 2;
}

void drawOnboarding(AppState& state, Controller& controller) {
    ImVec2 min = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    ImVec2 max{min.x + size.x, min.y + size.y};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    backdrop(dl, min, max, ImGui::GetTime());

    float centerX = min.x + size.x * 0.5f;
    float top = min.y + size.y * 0.14f;

    drawAnchorMark(dl, {centerX, top}, 46.0f, col::Cyan, 0.9f);

    float cardW = std::min(460.0f, size.x - 36.0f);
    ImGui::SetCursorScreenPos({centerX - cardW * 0.5f, top + 60.0f});
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW);

    ImGui::PushFont(fonts().uiBold, 28.0f);
    ImGui::TextUnformatted("Forge your vault");
    ImGui::PopFont();
    ImGui::PushFont(fonts().ui, kText);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
    ImGui::TextWrapped(
        "TackleBox keeps everything sensitive - private keys, networks, whitelist "
        "rules and the signing log - inside one encrypted file on this machine.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    vspace(14);

    static char password[256] = {};
    static char confirm[256] = {};

    FieldOpts pwOpts;
    pwOpts.password = true;
    pwOpts.placeholder = "at least 8 characters";
    pwOpts.width = cardW;
    textField("Vault password", password, sizeof password, pwOpts);

    // Strength meter.
    int score = passwordScore(password);
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float segW = (cardW - 18.0f) / 4.0f;
        static const ImU32 colors[] = {col::Danger, col::Warn, col::rgba(0xC8E24B),
                                       col::Success};
        for (int i = 0; i < 4; ++i) {
            ImU32 c = i < score ? colors[score - 1] : col::rgba(0x11202F);
            dl->AddRectFilled({pos.x + i * (segW + 6.0f), pos.y},
                              {pos.x + i * (segW + 6.0f) + segW, pos.y + 4.0f}, c, 2.0f);
        }
        ImGui::Dummy({cardW, 10});
    }

    FieldOpts cfOpts;
    cfOpts.password = true;
    cfOpts.placeholder = "repeat the password";
    cfOpts.width = cardW;
    bool mismatch = confirm[0] && std::strcmp(password, confirm) != 0;
    cfOpts.error = mismatch ? "passwords do not match" : nullptr;
    bool entered = textField("Confirm password", confirm, sizeof confirm, cfOpts);

    vspace(4);
    ImGui::PushFont(fonts().ui, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
    ImGui::TextWrapped(
        "There is no recovery. If this password is lost, the vault and every key "
        "inside it are gone. Write it down somewhere safe.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    vspace(10);

    bool ready = std::strlen(password) >= 8 && std::strcmp(password, confirm) == 0;
    if (state.busyUnlock) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cardW * 0.5f - 12);
        spinner(12.0f);
    } else if (neonButton("CREATE VAULT", BtnKind::Primary, {cardW, 46}, !ready) ||
               (entered && ready)) {
        controller.createVault(password);
        secureWipe(password, sizeof password);
        secureWipe(confirm, sizeof confirm);
    }
    if (!state.unlockError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextWrapped("%s", state.unlockError.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
}

}  // namespace tb::ui
