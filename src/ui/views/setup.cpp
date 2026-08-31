// First-run guide: pick chains -> add keys (Anchor migration path) -> discover
// accounts. Reachable any time from the Vault page; skippable at every step.
#include <cstring>
#include <set>

#include "chain/netreg.hpp"
#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/ui_helpers.h"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

int g_step = 0;  // 0 chains, 1 keys, 2 discover

void stepDots(int current) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float spacing = ImGui::GetTextLineHeight();
    float total = spacing * 2.0f * 2.0f;
    float x = pos.x + (w - total) * 0.5f;
    for (int i = 0; i < 3; ++i) {
        ImU32 c = i == current ? col::Cyan
                  : i < current ? col::alpha(col::Cyan, 0.45f)
                                : col::alpha(col::Steel, 0.4f);
        dl->AddCircleFilled({x + spacing * 2.0f * static_cast<float>(i),
                             pos.y + spacing * 0.5f},
                            spacing * 0.22f, c);
    }
    ::ui::VSpace(1.5f);
}

void drawChainStep(AppState& state, Controller& controller) {
    heading("Choose your chains", 24.0f);
    subtext("Enable the networks this vault will work with. Everything here can be "
            "changed later in Settings, including fully custom chains.");
    ::ui::VSpace(0.5f);

    // Selection state seeded from the vault (or the defaults on first visit).
    static std::set<std::string> picked;
    static bool seeded = false;
    if (!seeded) {
        seeded = true;
        if (state.vault.networks.empty())
            for (const auto& net : defaultNetworks()) picked.insert(net.chainId);
        else
            for (const auto& net : state.vault.networks) picked.insert(net.chainId);
    }

    auto presets = allPresets();
    if (ImGui::BeginTable("##chains", layout().phone() ? 1 : 2,
                          ImGuiTableFlags_SizingStretchSame)) {
        int i = 0;
        for (auto& preset : presets) {
            ImGui::TableNextColumn();
            ImGui::PushID(i++);
            bool on = picked.count(preset.chainId) > 0;
            if (ImGui::Checkbox("##pick", &on)) {
                if (on)
                    picked.insert(preset.chainId);
                else
                    picked.erase(preset.chainId);
                controller.noteActivity();
            }
            ::ui::HandOnHover();
            ImGui::SameLine();
            ImGui::TextUnformatted(preset.name.c_str());
            if (preset.testnet) {
                ImGui::SameLine();
                badge("TESTNET", col::Warn);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    subtext("Need a chain that is not listed? Finish setup, then Settings > Add a "
            "custom chain probes any endpoint and verifies its chain id.");
    ::ui::VSpace(1);

    if (::ui::BeginButtonRow("##chainnav", 2)) {
        ImGui::TableNextColumn();
        if (neonButton("SKIP SETUP", BtnKind::Subtle, {-FLT_MIN, 0}))
            state.page = Page::Dashboard;
        ImGui::TableNextColumn();
        if (neonButton("CONTINUE", BtnKind::Primary, {-FLT_MIN, 0}, picked.empty())) {
            std::vector<NetworkDef> enable;
            for (const auto& preset : presets)
                if (picked.count(preset.chainId)) enable.push_back(preset);
            controller.enableNetworks(enable);
            g_step = 1;
        }
        ::ui::EndButtonRow();
    }
}

void drawKeyStep(AppState& state, Controller& controller) {
    heading("Bring your keys", 24.0f);
    subtext("Moving from Anchor: open Anchor > Manage Wallets, export each private "
            "key, and paste them below (one per line). TackleBox cannot read "
            "Anchor's encrypted database directly - keys travel via Anchor's own "
            "export. Fresh start? Generate a new key instead.");
    ::ui::VSpace(0.5f);

    static char keysBuf[4096] = {};
    ImGui::PushFont(fonts().mono, kMonoSm);
    ImGui::InputTextMultiline("##bulkkeys", keysBuf, sizeof keysBuf,
                              {-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 6},
                              ImGuiInputTextFlags_Password);
    ImGui::PopFont();
    ::ui::VSpace(0.5f);

    if (::ui::BeginButtonRow("##keyops", 2)) {
        ImGui::TableNextColumn();
        if (neonButton("IMPORT PASTED KEYS", BtnKind::Primary, {-FLT_MIN, 0},
                       !keysBuf[0])) {
            controller.importKeysBulk(keysBuf);
            secureWipe(keysBuf, sizeof keysBuf);
        }
        ImGui::TableNextColumn();
        if (neonButton("GENERATE A NEW KEY", BtnKind::Ghost, {-FLT_MIN, 0}))
            controller.generateKey("first key");
        ::ui::EndButtonRow();
    }

    char count[64];
    std::snprintf(count, sizeof count, "%zu key(s) in the vault", state.vault.keys.size());
    subtext(count);
    ::ui::VSpace(1);

    if (::ui::BeginButtonRow("##keynav", 2)) {
        ImGui::TableNextColumn();
        if (neonButton("BACK", BtnKind::Subtle, {-FLT_MIN, 0})) g_step = 0;
        ImGui::TableNextColumn();
        if (neonButton("CONTINUE", BtnKind::Primary, {-FLT_MIN, 0},
                       state.vault.keys.empty()))
            g_step = 2;
        ::ui::EndButtonRow();
    }
}

void drawDiscoverStep(AppState& state, Controller& controller) {
    heading("Find your accounts", 24.0f);
    subtext("TackleBox asks every enabled chain which accounts your keys control "
            "(get_accounts_by_authorizers) and links them automatically.");
    ::ui::VSpace(0.5f);

    if (state.discovery.running) {
        spinner(13.0f);
        ImGui::SameLine();
        subtext("scanning chains...");
    } else if (neonButton("RUN DISCOVERY", BtnKind::Primary)) {
        controller.discoverAccounts();
    }
    for (const auto& line : state.discovery.log) {
        ImGui::PushFont(fonts().mono, kMonoSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    char count[64];
    std::snprintf(count, sizeof count, "%zu account(s) linked", state.vault.accounts.size());
    subtext(count);
    subtext("Some endpoints do not serve get_accounts_by_authorizers; add those "
            "accounts by name on the Vault page instead.");
    ::ui::VSpace(1);

    if (::ui::BeginButtonRow("##discnav", 2)) {
        ImGui::TableNextColumn();
        if (neonButton("BACK", BtnKind::Subtle, {-FLT_MIN, 0})) g_step = 1;
        ImGui::TableNextColumn();
        if (neonButton("FINISH", BtnKind::Primary, {-FLT_MIN, 0},
                       state.discovery.running)) {
            g_step = 0;
            state.page = Page::Dashboard;
            controller.toast(Toast::Success, "Welcome aboard - the tackle box is packed");
        }
        ::ui::EndButtonRow();
    }
}

}  // namespace

void drawSetup(AppState& state, Controller& controller) {
    float avail = ImGui::GetContentRegionAvail().x;
    float cardW = avail < ::ui::S(560.0f) ? avail : ::ui::S(560.0f);
    ::ui::CenterNext(cardW);
    if (beginCard("setupwizard", cardW, true)) {
        stepDots(g_step);
        switch (g_step) {
            case 0: drawChainStep(state, controller); break;
            case 1: drawKeyStep(state, controller); break;
            case 2: drawDiscoverStep(state, controller); break;
        }
    }
    endCard();
}

}  // namespace tb::ui
