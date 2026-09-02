// Main shell in three chromes:
//   desktop - full sidebar + top bar (the classic layout)
//   tablet  - icon navigation rail + top bar
//   phone   - compact top bar + bottom tab bar with a "more" sheet
#include <cstring>

#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/fx.hpp"
#include "ui/layout.hpp"
#include "ui/qa.hpp"
#include "ui/ui_helpers.h"
#include "ui/widgets.hpp"
#include "tb_version.h"

namespace tb::ui {

namespace {

struct NavItem {
    Page page;
    Icon icon;
    const char* label;
};

const NavItem kNav[] = {
    {Page::Dashboard, Icon::Pulse, "Dashboard"},
    {Page::Explore, Icon::Globe, "Explore"},
    {Page::Transfer, Icon::Send, "Transfer"},
    {Page::Assets, Icon::QrCode, "Assets"},
    {Page::Contracts, Icon::Grid, "Contracts"},
    {Page::Resources, Icon::Pulse, "Resources"},
    {Page::Governance, Icon::CheckCircle, "Governance"},
    {Page::Msig, Icon::Copy, "Multisig"},
    {Page::Autopilot, Icon::Bolt, "Autopilot"},
    {Page::Whitelist, Icon::Shield, "Whitelist"},
    {Page::Vault, Icon::Key, "Vault"},
    {Page::CreateAccount, Icon::Plus, "Create"},
    {Page::History, Icon::Clock, "History"},
    {Page::Settings, Icon::Gear, "Settings"},
};

// The four destinations that earn a spot on the phone's bottom bar.
const Page kPhonePrimary[] = {Page::Dashboard, Page::Transfer, Page::Explore, Page::Assets};

const NavItem* navFor(Page page) {
    for (const auto& item : kNav)
        if (item.page == page) return &item;
    return &kNav[0];
}

bool pageHasAlert(const AppState& state, Page page) {
    if (page == Page::Whitelist) {
        for (const auto& r : state.vault.rules)
            if (r.status == guard::RuleStatus::Stale) return true;
    }
    if (page == Page::Autopilot) {
        for (const auto& s : state.vault.schedules)
            if (s.enabled && !s.lastResult.empty() && s.lastResult.rfind("signed", 0) != 0)
                return true;
    }
    return false;
}

void alertPip(ImDrawList* dl, ImVec2 center) {
    dl->AddCircleFilled(center, 4.0f, col::Warn);
    dl->AddCircle(center, 6.5f, col::alpha(col::Warn, 0.3f + 0.3f * pulse(1.6f)));
}

// --- desktop sidebar ---------------------------------------------------------

void drawSidebar(AppState& state, Controller& controller, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    ImVec2 barMax{origin.x + width, origin.y + height};
    dl->AddRectFilled(origin, barMax, col::rgba(0x060B14));
    dl->AddLine({barMax.x, origin.y}, barMax, col::Hairline, 1.0f);

    float x = origin.x + 20;
    drawAnchorMark(dl, {x + 15, origin.y + 34}, 26.0f, col::Cyan, 0.5f);
    ImGui::PushFont(fonts().uiBold, 20.0f);
    dl->AddText({x + 40, origin.y + 22}, col::Ice, "TACKLEBOX");
    ImGui::PopFont();

    const float rowH = 36.0f;
    ImGui::SetCursorScreenPos({origin.x, origin.y + 68});
    ImGui::PushFont(fonts().uiSemi, kText);
    for (const auto& item : kNav) {
        bool active = state.page == item.page;
        ImGui::PushID(item.label);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton("##nav", {width, rowH});
        ::ui::HandOnHover();
        bool hovered = ImGui::IsItemHovered();
        ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();

        if (active) {
            dl->AddRectFilledMultiColor(rmin, rmax, col::alpha(col::Cyan, 0.14f),
                                        col::alpha(col::Cyan, 0.0f), col::alpha(col::Cyan, 0.0f),
                                        col::alpha(col::Cyan, 0.14f));
            glowLine(dl, {rmin.x + 1.5f, rmin.y + 6}, {rmin.x + 1.5f, rmax.y - 6}, col::Cyan,
                     0.8f, 3.0f);
        } else if (hovered) {
            dl->AddRectFilled(rmin, rmax, col::alpha(col::HairHi, 0.12f));
        }
        ImU32 fg = active ? col::Ice : hovered ? col::Steel : col::alpha(col::Steel, 0.75f);
        drawIcon(dl, item.icon, {pos.x + 30, pos.y + rowH * 0.5f}, 16.0f,
                 active ? col::Cyan : fg, 1.7f);
        dl->AddText({pos.x + 52, pos.y + rowH * 0.5f - 10}, fg, item.label);
        if (pageHasAlert(state, item.page))
            alertPip(dl, {pos.x + width - 26, pos.y + rowH * 0.5f});
        if (clicked) {
            state.page = item.page;
            controller.noteActivity();
        }
        ImGui::PopID();
    }
    ImGui::PopFont();

    ImGui::SetCursorScreenPos({origin.x + 14, origin.y + height - 52});
    if (neonButton("LOCK VAULT", BtnKind::Ghost, {width - 28, 36})) controller.lockVault();
    ImGui::PushFont(fonts().mono, kMonoSm);
    dl->AddText({origin.x + 20, origin.y + height - 74}, col::alpha(col::Slate, 0.8f),
                "v" TB_VERSION "  ctrl+shift+L locks");
    ImGui::PopFont();
}

// --- tablet rail -------------------------------------------------------------

void drawRail(AppState& state, Controller& controller, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    dl->AddRectFilled(origin, {origin.x + width, origin.y + height}, col::rgba(0x060B14));
    dl->AddLine({origin.x + width, origin.y}, {origin.x + width, origin.y + height},
                col::Hairline, 1.0f);

    drawAnchorMark(dl, {origin.x + width * 0.5f, origin.y + 30}, 24.0f, col::Cyan, 0.5f);

    const float rowH = layout().hit();
    ImGui::SetCursorScreenPos({origin.x, origin.y + 60});
    for (const auto& item : kNav) {
        bool active = state.page == item.page;
        ImGui::PushID(item.label);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton("##nav", {width, rowH});
        ::ui::HandOnHover();
        bool hovered = ImGui::IsItemHovered();
        if (active)
            dl->AddRectFilled({pos.x + 10, pos.y + 4}, {pos.x + width - 10, pos.y + rowH - 4},
                              col::alpha(col::Cyan, 0.14f), 8.0f);
        else if (hovered)
            dl->AddRectFilled({pos.x + 10, pos.y + 4}, {pos.x + width - 10, pos.y + rowH - 4},
                              col::alpha(col::HairHi, 0.15f), 8.0f);
        drawIcon(dl, item.icon, {pos.x + width * 0.5f, pos.y + rowH * 0.5f}, 18.0f,
                 active ? col::Cyan : col::alpha(col::Steel, hovered ? 1.0f : 0.8f), 1.8f);
        if (pageHasAlert(state, item.page))
            alertPip(dl, {pos.x + width - 14, pos.y + 12});
        if (hovered) tooltip(item.label);
        if (clicked) {
            state.page = item.page;
            controller.noteActivity();
        }
        ImGui::PopID();
    }

    // Lock at the bottom of the rail.
    ImGui::SetCursorScreenPos({origin.x + (width - 40) * 0.5f, origin.y + height - 52});
    if (iconButton("##raillock", Icon::Lock, "Lock vault", col::Steel, 18.0f))
        controller.lockVault();
}

// --- phone bottom bar --------------------------------------------------------

bool g_moreOpen = false;

void drawBottomBar(AppState& state, Controller& controller, float barH) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    float top = origin.y + winSize.y - barH - layout().safeBottom;
    ImVec2 barMin{origin.x, top};
    ImVec2 barMax{origin.x + winSize.x, origin.y + winSize.y};
    dl->AddRectFilled(barMin, barMax, col::rgba(0x070D17, 0xFA));
    dl->AddLine(barMin, {barMax.x, barMin.y}, col::Hairline, 1.0f);

    const int slots = 5;  // 4 primaries + MORE
    float slotW = winSize.x / static_cast<float>(slots);
    bool moreActive = g_moreOpen;
    bool primaryActive = false;
    for (Page p : kPhonePrimary)
        if (state.page == p) primaryActive = true;

    ImGui::PushFont(fonts().uiSemi, 12.5f);
    for (int i = 0; i < slots; ++i) {
        bool isMore = i == slots - 1;
        const NavItem* item = isMore ? nullptr : navFor(kPhonePrimary[i]);
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos({origin.x + slotW * static_cast<float>(i), barMin.y});
        bool clicked = ImGui::InvisibleButton("##tab", {slotW, barH});
        ::ui::HandOnHover();
        ImVec2 c{origin.x + slotW * (static_cast<float>(i) + 0.5f), barMin.y + barH * 0.42f};

        bool active = isMore ? (moreActive || !primaryActive) : state.page == item->page;
        ImU32 fg = active ? col::Cyan : col::alpha(col::Steel, 0.8f);
        drawIcon(dl, isMore ? Icon::Grid : item->icon, {c.x, c.y - 4}, 19.0f, fg, 1.8f);
        const char* label = isMore ? "More" : item->label;
        ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText({c.x - ts.x * 0.5f, barMin.y + barH - 18}, fg, label);
        if (active)
            dl->AddRectFilled({c.x - 14, barMin.y + 2}, {c.x + 14, barMin.y + 4},
                              col::alpha(col::Cyan, 0.9f), 2.0f);
        // Alert pip on More when any hidden page alerts.
        if (isMore) {
            for (const auto& nav : kNav) {
                bool hidden = true;
                for (Page pp : kPhonePrimary)
                    if (nav.page == pp) hidden = false;
                if (hidden && pageHasAlert(state, nav.page)) {
                    alertPip(dl, {c.x + 16, c.y - 10});
                    break;
                }
            }
        }
        if (clicked) {
            controller.noteActivity();
            if (isMore) {
                g_moreOpen = true;
            } else {
                state.page = item->page;
                g_moreOpen = false;
            }
        }
        ImGui::PopID();
    }
    ImGui::PopFont();
}

void drawMoreSheet(AppState& state, Controller& controller) {
    if (!g_moreOpen) return;
    ImGui::OpenPopup("##moresheet");
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y * 0.28f});
    ImGui::SetNextWindowSize({vp->WorkSize.x, vp->WorkSize.y * 0.72f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 18));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, col::vec(col::rgba(0x0A121F, 0xFC)));
    if (ImGui::BeginPopupModal("##moresheet", nullptr,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoMove)) {
        // Grab handle.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wpos = ImGui::GetWindowPos();
        float ww = ImGui::GetWindowSize().x;
        dl->AddRectFilled({wpos.x + ww * 0.5f - 22, wpos.y + 8},
                          {wpos.x + ww * 0.5f + 22, wpos.y + 12},
                          col::alpha(col::Steel, 0.5f), 2.0f);
        vspace(10);

        int columns = 3;
        float cellW = (ImGui::GetContentRegionAvail().x -
                       static_cast<float>(columns - 1) * 10.0f) /
                      static_cast<float>(columns);
        int i = 0;
        for (const auto& item : kNav) {
            bool primary = false;
            for (Page p : kPhonePrimary)
                if (item.page == p) primary = true;
            if (primary) continue;
            if (i % columns != 0) ImGui::SameLine(0, 10);
            ++i;
            ImGui::PushID(item.label);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::InvisibleButton("##cell", {cellW, 76});
            ::ui::HandOnHover();
            bool active = state.page == item.page;
            ImU32 border = active ? col::alpha(col::Cyan, 0.7f) : col::Hairline;
            dl->AddRectFilled(pos, {pos.x + cellW, pos.y + 76},
                              col::alpha(col::PanelHi, active ? 1.0f : 0.6f), 8.0f);
            dl->AddRect(pos, {pos.x + cellW, pos.y + 76}, border, 8.0f, 1.0f);
            drawIcon(dl, item.icon, {pos.x + cellW * 0.5f, pos.y + 30}, 20.0f,
                     active ? col::Cyan : col::Steel, 1.8f);
            ImGui::PushFont(fonts().uiSemi, 13.0f);
            ImVec2 ts = ImGui::CalcTextSize(item.label);
            dl->AddText({pos.x + (cellW - ts.x) * 0.5f, pos.y + 52},
                        active ? col::Ice : col::Steel, item.label);
            ImGui::PopFont();
            if (pageHasAlert(state, item.page)) alertPip(dl, {pos.x + cellW - 12, pos.y + 12});
            if (clicked) {
                state.page = item.page;
                g_moreOpen = false;
                controller.noteActivity();
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        vspace(12);
        if (neonButton("LOCK VAULT", BtnKind::Ghost, {ImGui::GetContentRegionAvail().x, 42})) {
            g_moreOpen = false;
            ImGui::CloseCurrentPopup();
            controller.lockVault();
        }
        // Tap outside (above the sheet) closes it.
        if (ImGui::IsMouseClicked(0) &&
            ImGui::GetMousePos().y < ImGui::GetWindowPos().y) {
            g_moreOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// --- top bar -----------------------------------------------------------------

// One boxed row inside a switcher popup: a hairline outline at rest, a
// filled highlight box on hover, an accent border when selected. Title on
// the left, an optional right-aligned tag, both vertically centered.
bool menuRow(const char* id, const std::string& title, const std::string& right,
             ImU32 rightColor, bool selected) {
    float h = layout().hit() - 2.0f;
    // The Selectable is interaction-only; the box below paints the states.
    ImGui::PushStyleColor(ImGuiCol_Header, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, {0, 0, 0, 0});
    bool clicked = ImGui::Selectable(id, selected, 0, {0, h});
    ImGui::PopStyleColor(3);
    ::ui::HandOnHover();
    bool hovered = ImGui::IsItemHovered();
    ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
    ImDrawList* pdl = ImGui::GetWindowDrawList();
    if (hovered)
        pdl->AddRectFilled(rmin, rmax, col::alpha(col::PanelHi, 0.95f), 5.0f);
    else if (selected)
        pdl->AddRectFilled(rmin, rmax, col::CyanFaint, 5.0f);
    ImU32 border = selected ? col::alpha(col::Cyan, hovered ? 0.85f : 0.55f)
                   : hovered ? col::HairHi
                             : col::Hairline;
    pdl->AddRect(rmin, rmax, border, 5.0f, 1.0f);

    ImGui::PushFont(fonts().uiSemi, kText);
    pdl->AddText({rmin.x + 12, rmin.y + (h - kText) * 0.5f},
                 selected ? col::Cyan : col::Ice, title.c_str());
    ImGui::PopFont();
    if (!right.empty()) {
        ImGui::PushFont(fonts().mono, kMonoSm);
        float rw = ImGui::CalcTextSize(right.c_str()).x;
        pdl->AddText({rmax.x - 12 - rw, rmin.y + (h - kMonoSm) * 0.5f}, rightColor,
                     right.c_str());
        ImGui::PopFont();
    }
    return clicked;
}

void drawTopbar(AppState& state, Controller& controller, float leftInset, float barH,
                bool compact) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    ImVec2 min{origin.x + leftInset, origin.y + layout().safeTop};
    ImVec2 max{origin.x + ImGui::GetWindowSize().x, min.y + barH};
    dl->AddLine({min.x, max.y}, max, col::Hairline, 1.0f);

    const AccountRef* account = state.currentAccount();
    const NetworkDef* network = state.currentNetwork();

    // Chain selector, LEFT of the account switcher: pick the chain the whole
    // UI looks at; the account list filters to it.
    float cursorX = min.x + (compact ? 12 : 24);
    {
        std::string chainName = network ? network->name : "pick a chain";
        float fontSize = compact ? kText : kTextLg;
        ImGui::PushFont(fonts().uiSemi, fontSize);
        ImVec2 nameSize = ImGui::CalcTextSize(chainName.c_str());
        ImGui::PopFont();
        float boxW = nameSize.x + 46.0f;

        ImGui::SetCursorScreenPos({cursorX, min.y + (barH - 40) * 0.5f});
        ImGui::PushID("chainswitch");
        bool clicked = ImGui::InvisibleButton("##btn", {boxW, 40});
        ::ui::HandOnHover();
        ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        if (ImGui::IsItemHovered())
            dl->AddRectFilled(rmin, rmax, col::alpha(col::HairHi, 0.15f), 4.0f);
        ImU32 chainCol = !network ? col::Steel : network->testnet ? col::Warn : col::Cyan;
        drawIcon(dl, Icon::Globe, {rmin.x + 12, (rmin.y + rmax.y) * 0.5f}, 14.0f, chainCol,
                 1.7f);
        ImGui::PushFont(fonts().uiSemi, fontSize);
        dl->AddText({rmin.x + 24, rmin.y + (40 - fontSize) * 0.5f}, chainCol,
                    chainName.c_str());
        ImGui::PopFont();
        drawIcon(dl, Icon::ChevronDown, {rmax.x - 10, (rmin.y + rmax.y) * 0.5f}, 10.0f,
                 col::Steel, 1.6f);
        if (clicked || qa::forceOpen("chain-menu")) ImGui::OpenPopup("##chains");

        ImGui::SetNextWindowPos({rmin.x, rmax.y + 4});
        ImGui::SetNextWindowSizeConstraints({260, 0}, {380, 420});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 10});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8, 6});
        if (ImGui::BeginPopup("##chains")) {
            for (const auto& net : state.vault.networks) {
                ImGui::PushID(net.chainId.c_str());
                bool selected = network && network->chainId == net.chainId;
                int count = 0;
                for (const auto& a : state.vault.accounts)
                    if (a.chainId == net.chainId) ++count;
                std::string sub = std::to_string(count) + " acct" +
                                  (net.testnet ? "  TESTNET" : "");
                if (menuRow("##chain", net.name, sub,
                            net.testnet ? col::Warn : col::Slate, selected)) {
                    controller.selectChain(net.chainId);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::Separator();
            if (menuRow("##addchain", "+ Add chain...", "", col::Slate, false)) {
                state.page = Page::Settings;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopID();
        cursorX = rmax.x + (compact ? 6.0f : 12.0f);
    }

    // Account switcher (accounts on the selected chain only).
    ImGui::SetCursorScreenPos({cursorX, min.y + (barH - 40) * 0.5f});
    {
        std::vector<int> onChain = state.accountsOnChain();
        std::string label = account ? account->display()
                            : onChain.empty() ? "no account on this chain"
                                              : "pick an account";
        float fontSize = compact ? kText : kTextLg;
        ImGui::PushFont(fonts().uiSemi, fontSize);
        ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
        ImGui::PopFont();
        float boxW = labelSize.x + 40.0f;

        ImGui::PushID("acctswitch");
        bool clicked = ImGui::InvisibleButton("##btn", {boxW, 40});
        ::ui::HandOnHover();
        ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        bool hovered = ImGui::IsItemHovered();
        if (hovered) dl->AddRectFilled(rmin, rmax, col::alpha(col::HairHi, 0.15f), 4.0f);

        ImGui::PushFont(fonts().uiSemi, fontSize);
        dl->AddText({rmin.x + 8, rmin.y + (40 - fontSize) * 0.5f},
                    account ? col::Ice : col::Steel, label.c_str());
        ImGui::PopFont();
        drawIcon(dl, Icon::ChevronDown, {rmax.x - 16, (rmin.y + rmax.y) * 0.5f}, 11.0f,
                 col::Steel, 1.6f);
        if (clicked || qa::forceOpen("account-menu")) ImGui::OpenPopup("##accounts");

        ImGui::SetNextWindowPos({rmin.x, rmax.y + 4});
        ImGui::SetNextWindowSizeConstraints({300, 0}, {420, 420});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 10});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8, 6});
        if (ImGui::BeginPopup("##accounts")) {
            if (onChain.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
                ImGui::TextUnformatted("No accounts on this chain - add one in Vault");
                ImGui::PopStyleColor();
            }
            auto drawRow = [&](int index) {
                const AccountRef& a = state.vault.accounts[static_cast<size_t>(index)];
                ImGui::PushID(index);
                bool selected = index == state.selectedAccount;
                if (menuRow("##row", a.display(), a.watch ? "WATCH-ONLY" : "", col::Warn,
                            selected)) {
                    controller.selectAccount(index);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            };
            auto drawHeader = [&](const char* text) {
                ImGui::Dummy({0, 3});
                ImGui::PushFont(fonts().uiSemi, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::CyanDim));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4);
                ImGui::TextUnformatted(text);
                ImGui::PopStyleColor();
                ImGui::PopFont();
            };
            // Pinned sections first (in their saved order), then the rest.
            bool anyGrouped = false;
            for (const std::string& group : state.vault.accountGroups) {
                bool headerDrawn = false;
                for (int index : onChain) {
                    const AccountRef& a = state.vault.accounts[static_cast<size_t>(index)];
                    if (a.group != group) continue;
                    if (!headerDrawn) {
                        drawHeader(group.c_str());
                        headerDrawn = true;
                        anyGrouped = true;
                    }
                    drawRow(index);
                }
            }
            bool otherHeader = false;
            for (int index : onChain) {
                const AccountRef& a = state.vault.accounts[static_cast<size_t>(index)];
                if (!a.group.empty()) continue;
                if (anyGrouped && !otherHeader) {
                    drawHeader("OTHER");
                    otherHeader = true;
                }
                drawRow(index);
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopID();
    }

    // Right side: pipeline status, busy spinner, endpoint health.
    float rightX = max.x - (compact ? 12 : 24);
    if (network) {
        std::string endpoint = network->activeEndpoint();
        std::string display = endpoint;
        if (auto pos = display.find("://"); pos != std::string::npos)
            display = display.substr(pos + 3);
        ImU32 dot = col::Slate;
        auto it = state.health.find(network->chainId);
        if (it != state.health.end())
            for (const auto& h : it->second)
                if (h.type == NodeType::Rpc && h.url == endpoint)
                    dot = h.ok && h.chainIdMatches ? col::Success : col::Danger;
        if (!compact) {
            ImGui::PushFont(fonts().mono, kMonoSm);
            ImVec2 epSize = ImGui::CalcTextSize(display.c_str());
            float x = rightX - epSize.x;
            dl->AddText({x, min.y + 24}, col::Steel, display.c_str());
            ImGui::PopFont();
            dl->AddCircleFilled({x - 12, min.y + 31}, 4.0f, dot);
            rightX = x - 30;
        } else {
            dl->AddCircleFilled({rightX - 6, min.y + barH * 0.5f}, 4.0f, dot);
            rightX -= 24;
        }
    }
    if (state.workerPending > 0 || !state.pipelineStatus.empty()) {
        ImGui::SetCursorScreenPos({rightX - 22, min.y + (barH - 18) * 0.5f});
        spinner(9.0f, col::Cyan);
        if (!compact && !state.pipelineStatus.empty()) {
            ImGui::PushFont(fonts().ui, kTextSm);
            ImVec2 stSize = ImGui::CalcTextSize(state.pipelineStatus.c_str());
            dl->AddText({rightX - 34 - stSize.x, min.y + 24}, col::Cyan,
                        state.pipelineStatus.c_str());
            ImGui::PopFont();
        }
    }
}

}  // namespace

void drawShell(AppState& state, Controller& controller) {
    const Layout& lay = layout();
    ImVec2 winSize = ImGui::GetWindowSize();
    ImVec2 origin = ImGui::GetWindowPos();

    float navW = lay.desktop() ? 216.0f : lay.tablet() ? 68.0f : 0.0f;
    float topbarH = lay.phone() ? 52.0f : 62.0f;
    float bottomH = lay.phone() ? 64.0f : 0.0f;
    float contentTop = lay.safeTop + topbarH;
    float contentBottom = bottomH + lay.safeBottom;

    backdrop(ImGui::GetWindowDrawList(), {origin.x + navW, origin.y + contentTop},
             {origin.x + winSize.x, origin.y + winSize.y - contentBottom}, ImGui::GetTime());

    if (lay.desktop())
        drawSidebar(state, controller, navW, winSize.y);
    else if (lay.tablet())
        drawRail(state, controller, navW, winSize.y);

    drawTopbar(state, controller, navW, topbarH, lay.phone());

    // Routed content.
    ImGui::SetCursorScreenPos({origin.x + navW, origin.y + contentTop});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, lay.pagePadding());
    ImGui::BeginChild("##content",
                      {winSize.x - navW, winSize.y - contentTop - contentBottom},
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    // QA tour steps pin the page at a scroll offset for deep sections; the
    // shared content child otherwise keeps its scroll across steps, so every
    // step without an explicit offset starts at the top.
    if (qa::active())
        ImGui::SetScrollY(qa::pageScrollY() >= 0.0f ? qa::pageScrollY() : 0.0f);

    // Constrain content width for readability on wide screens.
    float avail = ImGui::GetContentRegionAvail().x;
    float contentW = avail > 1080.0f ? 1080.0f : avail;
    float inset = (avail - contentW) * 0.5f;
    if (inset > 0) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inset);
        ImGui::BeginChild("##inner", {contentW, 0}, ImGuiChildFlags_AutoResizeY);
    }

    switch (state.page) {
        case Page::Dashboard: drawDashboard(state, controller); break;
        case Page::Explore: drawExplore(state, controller); break;
        case Page::Assets: drawAssets(state, controller); break;
        case Page::Transfer: drawTransfer(state, controller); break;
        case Page::Contracts: drawContracts(state, controller); break;
        case Page::Resources: drawResources(state, controller); break;
        case Page::Governance: drawGovernance(state, controller); break;
        case Page::Msig: drawMsig(state, controller); break;
        case Page::Autopilot: drawAutopilot(state, controller); break;
        case Page::Whitelist: drawWhitelist(state, controller); break;
        case Page::Vault: drawVaultView(state, controller); break;
        case Page::CreateAccount: drawCreateAccount(state, controller); break;
        case Page::History: drawHistory(state, controller); break;
        case Page::Settings: drawSettings(state, controller); break;
        case Page::Setup: drawSetup(state, controller); break;
    }

    if (inset > 0) ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if (lay.phone()) {
        drawBottomBar(state, controller, bottomH);
        drawMoreSheet(state, controller);
    }
}

}  // namespace tb::ui
