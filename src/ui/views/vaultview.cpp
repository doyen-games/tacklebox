// Vault page: keys and accounts. Import/generate/reveal/remove keys, link
// accounts to networks, watch-only accounts.
#include <array>
#include <cstring>
#include <map>

#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/qa.hpp"
#include "ui/ui_helpers.h"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

// Reveal-key modal state.
struct RevealState {
    bool open = false;
    std::string pub;
    char password[256] = {};
    std::string wif;      // filled after successful verification
    std::string error;
    bool busy = false;
};
RevealState reveal;

}  // namespace

// QA hook: show the reveal modal's password-check state without a click.
void openRevealModal(const std::string& pub) {
    reveal = RevealState{};
    reveal.open = true;
    reveal.pub = pub;
}

namespace {

void drawRevealModal(AppState& state, Controller& controller) {
    (void)state;
    if (!reveal.open) return;
    ImGui::OpenPopup("Reveal private key");
    if (beginAdaptiveModal("Reveal private key", 460.0f)) {
        heading("Reveal private key", 22.0f);
        monoText(middleEllipsis(reveal.pub, 20, 8), col::Steel, kMonoSm);
        vspace(6);

        if (reveal.wif.empty()) {
            subtext("Re-enter the vault password. The key will display for 30 seconds.");
            FieldOpts opts;
            opts.password = true;
            opts.placeholder = "vault password";
            opts.autoFocus = true;
            opts.error = reveal.error.empty() ? nullptr : reveal.error.c_str();
            bool entered = textField("##rpw", reveal.password, sizeof reveal.password, opts);
            vspace(6);
            if (reveal.busy) {
                spinner(12.0f);
            } else {
                if ((neonButton("REVEAL", BtnKind::Danger, {120, 38}) || entered) &&
                    reveal.password[0]) {
                    reveal.busy = true;
                    reveal.error.clear();
                    controller.revealKey(reveal.pub, reveal.password,
                                         [](std::string wif, std::string error) {
                                             reveal.busy = false;
                                             reveal.wif = std::move(wif);
                                             reveal.error = std::move(error);
                                         });
                    secureWipe(reveal.password, sizeof reveal.password);
                }
                ImGui::SameLine(0, 8);
                if (neonButton("CANCEL", BtnKind::Ghost, {100, 38})) {
                    reveal = RevealState{};
                    ImGui::CloseCurrentPopup();
                }
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
            ImGui::TextWrapped("Anyone who sees this string owns the key. No screenshots, "
                               "no cloud clipboards.");
            ImGui::PopStyleColor();
            vspace(4);
            monoText(reveal.wif, col::Danger, kMono);
            vspace(6);
            vspace(4);
            float qrW = 170.0f;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - qrW) * 0.5f);
            drawQr(reveal.wif, qrW);
            vspace(6);
            if (neonButton("COPY (auto-clears)", BtnKind::Ghost, {180, 36}))
                controller.copyToClipboard(reveal.wif, /*sensitive=*/true);
            ImGui::SameLine(0, 8);
            if (neonButton("I'VE BACKED IT UP", BtnKind::Primary, {170, 36})) {
                controller.markKeyBackedUp(reveal.pub);
                secureWipe(reveal.wif.data(), reveal.wif.size());
                reveal = RevealState{};
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0, 8);
            if (neonButton("DONE", BtnKind::Ghost, {90, 36})) {
                secureWipe(reveal.wif.data(), reveal.wif.size());
                reveal = RevealState{};
                ImGui::CloseCurrentPopup();
            }
        }
        endAdaptiveModal();
    }
}

}  // namespace

void drawVaultView(AppState& state, Controller& controller) {
    heading("Vault");
    subtext("Private keys live encrypted in one local file. Accounts bind a chain "
            "identity to a key; without a matching key an account is watch-only.");
    if (neonButton("RE-RUN SETUP GUIDE", BtnKind::Subtle))
        state.page = Page::Setup;
    vspace(10);

    // --- backup status ------------------------------------------------------
    // Lost keys - not malware - are how people actually lose funds. Nag until
    // every key is confirmed backed up and the vault has a recent export.
    {
        int unbacked = 0;
        for (const auto& key : state.vault.keys)
            if (!key.backedUp) ++unbacked;
        int64_t sinceDays = state.vault.lastBackupAt
                                ? (nowSec() - state.vault.lastBackupAt) / 86400
                                : -1;
        bool exportStale = sinceDays < 0 || sinceDays > 30;
        if (!state.vault.keys.empty() && (unbacked > 0 || exportStale)) {
            if (beginCard("backupwarn")) {
                ImGui::PushFont(fonts().uiSemi, kText);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
                ImGui::TextUnformatted("Backups incomplete - there is no recovery "
                                       "without them");
                ImGui::PopStyleColor();
                ImGui::PopFont();
                if (unbacked > 0)
                    subtext((std::to_string(unbacked) +
                             (unbacked == 1 ? " key has" : " keys have") +
                             " never been confirmed backed up. Use the eye icon on a "
                             "key, re-enter your password, write the key down, then "
                             "press I'VE BACKED IT UP.")
                                .c_str());
                if (exportStale)
                    subtext(sinceDays < 0
                                ? "The vault file has never been exported. "
                                  "Settings > Portability writes an encrypted .tbx "
                                  "copy you can store offline."
                                : ("Last vault export was " + std::to_string(sinceDays) +
                                   " days ago.")
                                      .c_str());
            }
            endCard();
            vspace(10);
        }
    }

    // --- keys ---------------------------------------------------------------
    if (beginCard("keys")) {
        sectionTitle("Keys");
        for (const auto& key : state.vault.keys) {
            ImGui::PushID(key.pub.c_str());
            drawIcon(ImGui::GetWindowDrawList(), Icon::Key,
                     {ImGui::GetCursorScreenPos().x + 10,
                      ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.6f},
                     15.0f, col::Cyan, 1.6f);
            ImGui::Dummy({24, 0});
            const bool phone = layout().phone();
            ImGui::SameLine();
            monoText(middleEllipsis(key.pub, phone ? 12 : 24, phone ? 6 : 8), col::Ice,
                     kMono);
            ImGui::SameLine();
            if (iconButton("##cp", Icon::Copy, "Copy public key", col::Slate, 13.0f))
                ImGui::SetClipboardText(key.pub.c_str());
            int linked = 0;
            for (const auto& account : state.vault.accounts)
                if (account.pubKey == key.pub) ++linked;
            // Phones split the row: identity + actions, then label + badge.
            if (!phone) {
                ImGui::SameLine();
                ImGui::PushFont(fonts().ui, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                ImGui::Text("%s%s%d account%s", key.label.c_str(),
                            key.label.empty() ? "" : "  -  ", linked,
                            linked == 1 ? "" : "s");
                ImGui::PopStyleColor();
                ImGui::PopFont();
                if (!key.backedUp) {
                    ImGui::SameLine(0, 8);
                    badge("NOT BACKED UP", col::Warn);
                }
            }
            ImGui::SameLine();
            float endX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(endX - 66);
            if (iconButton("##reveal", Icon::Eye, "Reveal private key (password required)",
                           col::Steel, 14.0f)) {
                reveal = {};
                reveal.open = true;
                reveal.pub = key.pub;
            }
            ImGui::SameLine(0, 2);
            if (iconButton("##del", Icon::Trash, "Remove from vault", col::Danger, 14.0f) ||
                qa::forceOpen("key-del-confirm"))
                ImGui::OpenPopup("##confirmkey");
            if (qa::active())
                ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                        ImGuiCond_Appearing, {0.5f, 0.5f});
            if (ImGui::BeginPopup("##confirmkey")) {
                ImGui::TextWrapped("Remove this key? Its accounts become watch-only.\n"
                                   "Without a backup the key is unrecoverable.");
                static float holdKey = 0.0f;
                if (holdButton("HOLD TO REMOVE", 1.2f, &holdKey, {200, 34})) {
                    controller.removeKey(key.pub);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (phone) {
                ImGui::Dummy({24, 0});
                ImGui::SameLine();
                ImGui::PushFont(fonts().ui, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                ImGui::Text("%s%s%d account%s", key.label.c_str(),
                            key.label.empty() ? "" : "  -  ", linked,
                            linked == 1 ? "" : "s");
                ImGui::PopStyleColor();
                ImGui::PopFont();
                if (!key.backedUp) {
                    ImGui::SameLine(0, 8);
                    badge("NOT BACKED UP", col::Warn);
                }
                vspace(4);
            }
            ImGui::PopID();
        }
        if (state.vault.keys.empty()) subtext("No keys yet.");
        vspace(8);

        static char wifBuf[128] = {};
        static char labelBuf[64] = {};
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::BeginGroup();
        {
            FieldOpts opts;
            opts.password = true;
            opts.placeholder = "5... / PVT_K1_...";
            opts.width = avail * 0.5f;
            textField("Import private key", wifBuf, sizeof wifBuf, opts);
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, 10);
        ImGui::BeginGroup();
        {
            FieldOpts opts;
            opts.placeholder = "label";
            opts.width = avail * 0.22f;
            textField("##keylabel", labelBuf, sizeof labelBuf, opts);
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, 10);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 22);
        if (neonButton("IMPORT", BtnKind::Primary, {90, 38}) && wifBuf[0]) {
            controller.importKey(wifBuf, labelBuf);
            secureWipe(wifBuf, sizeof wifBuf);
            labelBuf[0] = 0;
        }
        ImGui::SameLine(0, 6);
        if (neonButton("GENERATE NEW", BtnKind::Ghost, {130, 38}))
            controller.generateKey("generated");
    }
    endCard();
    vspace(12);

    // --- accounts -----------------------------------------------------------
    if (beginCard("accounts")) {
        sectionTitle("Accounts");
        int acctFrom = -1, acctTo = -1;
        for (size_t i = 0; i < state.vault.accounts.size(); ++i) {
            const AccountRef& account = state.vault.accounts[i];
            ImGui::PushID(static_cast<int>(i));
            const NetworkDef* network = nullptr;
            for (const auto& net : state.vault.networks)
                if (net.chainId == account.chainId) network = &net;

            bool selected = static_cast<int>(i) == state.selectedAccount;
            // Everything on this row centers on one line: the grip, the
            // name, the badges and the three controls share a centerline.
            float rowH = ImGui::GetFrameHeight();
            if (selected) {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                glowLine(ImGui::GetWindowDrawList(), {pos.x - 8, pos.y + 4},
                         {pos.x - 8, pos.y + rowH - 4}, col::Cyan, 0.6f, 3.0f);
            }
            float rowTop = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(rowTop + (rowH - ImGui::GetFrameHeight() * 0.8f) * 0.5f);
            if (int dropped = dragGrip("##accounts", static_cast<int>(i),
                                       account.display().c_str());
                dropped >= 0) {
                acctFrom = dropped;
                acctTo = static_cast<int>(i);
            }
            ImGui::SameLine(0, 8);
            ImGui::SetCursorPosY(rowTop);
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().mono, kMono);
            ImGui::TextUnformatted(account.display().c_str());
            ImGui::PopFont();
            ImGui::SameLine(0, 8);
            badge(network ? network->name.c_str() : "unknown chain",
                  network && network->testnet ? col::Warn : col::CyanDim);
            if (account.watch) {
                ImGui::SameLine(0, 4);
                badge("WATCH", col::Warn);
            }
            if (!account.group.empty()) {
                ImGui::SameLine(0, 4);
                badge(account.group.c_str(), col::Violet);
            }
            ImGui::SameLine();
            float endX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(endX - 104);
            ImGui::SetCursorPosY(rowTop + (rowH - 26.0f) * 0.5f);
            // Compact picker: the arrow box matches the USE button height.
            if (qa::forceOpen("group-pick")) qa::openCombo("##grouppick");
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6, 4});
            ImGui::SetNextItemWidth(26);
            bool groupPickOpen = ImGui::BeginCombo("##grouppick", "",
                                                   ImGuiComboFlags_NoPreview);
            ImGui::PopStyleVar();
            if (groupPickOpen) {
                if (ImGui::Selectable("ungrouped", account.group.empty()))
                    controller.setAccountGroup(account.key(), "");
                for (const auto& group : state.vault.accountGroups) {
                    ImGui::PushID(group.c_str());
                    if (ImGui::Selectable(group.c_str(), account.group == group))
                        controller.setAccountGroup(account.key(), group);
                    ::ui::HandOnHover();
                    ImGui::PopID();
                }
                ImGui::Separator();
                static char newGroupBuf[24] = {};
                ImGui::SetNextItemWidth(120);
                bool commit = ImGui::InputTextWithHint("##newgroup", "new group...",
                                                       newGroupBuf, sizeof newGroupBuf,
                                                       ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine(0, 4);
                if ((iconButton("##mkgroup", Icon::Plus, "Create group and pin here",
                                col::CyanDim, 12.0f) ||
                     commit) &&
                    newGroupBuf[0]) {
                    controller.setAccountGroup(account.key(), trim(newGroupBuf));
                    newGroupBuf[0] = 0;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndCombo();
            }
            ::ui::HandOnHover();
            tooltip("Pin this wallet to a named section of the account selector");
            ImGui::SameLine(0, 4);
            ImGui::SetCursorPosY(rowTop + (rowH - 26.0f) * 0.5f);
            if (!selected && neonButton("USE", BtnKind::Subtle, {40, 26}))
                controller.selectAccount(static_cast<int>(i));
            if (selected) ImGui::Dummy({40, 26});
            ImGui::SameLine(0, 4);
            ImGui::SetCursorPosY(rowTop + (rowH - 26.0f) * 0.5f);
            if (iconButton("##rmacct", Icon::Trash, "Remove account", col::Slate, 14.0f))
                controller.removeAccount(account);
            ImGui::PopID();
            vspace(2);
        }
        if (acctFrom >= 0 && acctTo >= 0 && acctFrom != acctTo)
            controller.moveAccount(static_cast<size_t>(acctFrom),
                                   static_cast<size_t>(acctTo));
        if (state.vault.accounts.empty()) subtext("No accounts linked yet.");

        // Pinned sections: rename inline, drag to reorder, delete to ungroup.
        if (!state.vault.accountGroups.empty()) {
            vspace(8);
            sectionTitle("Pinned sections");
            subtext("Named groups in the account selector. Pin wallets to them with "
                    "the pin picker on each account row.");
            int groupFrom = -1, groupTo = -1;
            for (size_t gi = 0; gi < state.vault.accountGroups.size(); ++gi) {
                const std::string& group = state.vault.accountGroups[gi];
                ImGui::PushID(group.c_str());
                if (int dropped =
                        dragGrip("##acctgroups", static_cast<int>(gi), group.c_str());
                    dropped >= 0) {
                    groupFrom = dropped;
                    groupTo = static_cast<int>(gi);
                }
                ImGui::SameLine(0, 6);
                static std::map<std::string, std::array<char, 24>> renameBufs;
                auto& buf = renameBufs[group];
                if (!ImGui::IsAnyItemActive())
                    std::snprintf(buf.data(), buf.size(), "%s", group.c_str());
                ImGui::SetNextItemWidth(160);
                ImGui::InputText("##rename", buf.data(), buf.size());
                if (ImGui::IsItemDeactivatedAfterEdit() && trim(buf.data()) != group)
                    controller.renameAccountGroup(group, trim(buf.data()));
                ImGui::SameLine(0, 6);
                int members = 0;
                for (const auto& account : state.vault.accounts)
                    if (account.group == group) ++members;
                ImGui::AlignTextToFramePadding();
                ImGui::PushFont(fonts().ui, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                ImGui::Text("%d wallet%s", members, members == 1 ? "" : "s");
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::SameLine(0, 8);
                if (iconButton("##rmgroup", Icon::Trash,
                               "Remove section (wallets become ungrouped)", col::Slate,
                               12.0f))
                    controller.removeAccountGroup(group);
                ImGui::PopID();
            }
            if (groupFrom >= 0 && groupTo >= 0 && groupFrom != groupTo)
                controller.moveAccountGroup(static_cast<size_t>(groupFrom),
                                            static_cast<size_t>(groupTo));
        }
        vspace(8);

        static char actorBuf[16] = {};
        static char permBuf[16] = "active";
        static int chainIdx = 0;

        // One row, four columns; every field top-aligns under an identical
        // label line, and the ADD button matches the field height exactly.
        auto fieldLabel = [](const char* text) {
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy({0, 1});
        };
        if (ImGui::BeginTable("##addacct", 4, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("net", ImGuiTableColumnFlags_WidthStretch, 0.30f);
            ImGui::TableSetupColumn("acct", ImGuiTableColumnFlags_WidthStretch, 0.34f);
            ImGui::TableSetupColumn("perm", ImGuiTableColumnFlags_WidthStretch, 0.20f);
            ImGui::TableSetupColumn("add", ImGuiTableColumnFlags_WidthFixed,
                                    ::ui::S(92.0f));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            fieldLabel("Network");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##chain",
                                  state.vault.networks.empty()
                                      ? "no networks"
                                      : state.vault.networks[static_cast<size_t>(chainIdx) %
                                                             state.vault.networks.size()]
                                            .name.c_str())) {
                for (int n = 0; n < static_cast<int>(state.vault.networks.size()); ++n)
                    if (ImGui::Selectable(state.vault.networks[n].name.c_str(),
                                          chainIdx == n))
                        chainIdx = n;
                ImGui::EndCombo();
            }
            ::ui::HandOnHover();
            ImGui::TableNextColumn();
            {
                FieldOpts opts;
                opts.mono = true;
                opts.placeholder = "accountname";
                textField("Account", actorBuf, sizeof actorBuf, opts);
            }
            ImGui::TableNextColumn();
            {
                FieldOpts opts;
                opts.mono = true;
                textField("Permission", permBuf, sizeof permBuf, opts);
            }
            ImGui::TableNextColumn();
            fieldLabel(" ");  // spacer so the button starts level with the fields
            if (neonButton("ADD", BtnKind::Primary, {-FLT_MIN, ImGui::GetFrameHeight()}) &&
                actorBuf[0] && !state.vault.networks.empty()) {
                controller.addAccount(
                    state.vault.networks[static_cast<size_t>(chainIdx) %
                                         state.vault.networks.size()]
                        .chainId,
                    toLower(trim(actorBuf)), toLower(trim(permBuf)));
                actorBuf[0] = 0;
            }
            ImGui::EndTable();
        }
        subtext("The account is checked on-chain; if a vault key appears in the chosen "
                "permission's authority it links automatically, otherwise it is added "
                "watch-only.");
    }
    endCard();

    drawRevealModal(state, controller);
}

}  // namespace tb::ui
