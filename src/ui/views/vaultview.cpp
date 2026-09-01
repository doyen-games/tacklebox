// Vault page: keys and accounts. Import/generate/reveal/remove keys, link
// accounts to networks, watch-only accounts.
#include <cstring>

#include "core/util.hpp"
#include "ui/app_ui.hpp"
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
            if (neonButton("COPY (auto-clears)", BtnKind::Ghost, {180, 36}))
                controller.copyToClipboard(reveal.wif, /*sensitive=*/true);
            ImGui::SameLine(0, 8);
            if (neonButton("DONE", BtnKind::Primary, {100, 36})) {
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
            ImGui::SameLine();
            monoText(middleEllipsis(key.pub, 24, 8), col::Ice, kMono);
            ImGui::SameLine();
            if (iconButton("##cp", Icon::Copy, "Copy public key", col::Slate, 13.0f))
                ImGui::SetClipboardText(key.pub.c_str());
            ImGui::SameLine();
            int linked = 0;
            for (const auto& account : state.vault.accounts)
                if (account.pubKey == key.pub) ++linked;
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
            ImGui::Text("%s%s%d account%s", key.label.c_str(), key.label.empty() ? "" : "  -  ",
                        linked, linked == 1 ? "" : "s");
            ImGui::PopStyleColor();
            ImGui::PopFont();
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
            if (iconButton("##del", Icon::Trash, "Remove from vault", col::Danger, 14.0f))
                ImGui::OpenPopup("##confirmkey");
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
            if (selected) {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                glowLine(ImGui::GetWindowDrawList(), {pos.x - 8, pos.y + 2},
                         {pos.x - 8, pos.y + 20}, col::Cyan, 0.6f, 3.0f);
            }
            if (int dropped = dragGrip("##accounts", static_cast<int>(i),
                                       account.display().c_str());
                dropped >= 0) {
                acctFrom = dropped;
                acctTo = static_cast<int>(i);
            }
            ImGui::SameLine(0, 6);
            ImGui::PushFont(fonts().mono, kMono);
            ImGui::TextUnformatted(account.display().c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            badge(network ? network->name.c_str() : "unknown chain",
                  network && network->testnet ? col::Warn : col::CyanDim);
            if (account.watch) {
                ImGui::SameLine(0, 4);
                badge("WATCH", col::Warn);
            }
            ImGui::SameLine();
            float endX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(endX - 66);
            if (!selected && neonButton("USE", BtnKind::Subtle, {36, 26}))
                controller.selectAccount(static_cast<int>(i));
            if (selected) ImGui::Dummy({36, 26});
            ImGui::SameLine(0, 2);
            if (iconButton("##rmacct", Icon::Trash, "Remove account", col::Slate, 14.0f))
                controller.removeAccount(account);
            ImGui::PopID();
        }
        if (acctFrom >= 0 && acctTo >= 0 && acctFrom != acctTo)
            controller.moveAccount(static_cast<size_t>(acctFrom),
                                   static_cast<size_t>(acctTo));
        if (state.vault.accounts.empty()) subtext("No accounts linked yet.");
        vspace(8);

        static char actorBuf[16] = {};
        static char permBuf[16] = "active";
        static int chainIdx = 0;
        float avail = ImGui::GetContentRegionAvail().x;

        ImGui::BeginGroup();
        ImGui::PushFont(fonts().uiSemi, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
        ImGui::TextUnformatted("Network");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SetNextItemWidth(avail * 0.28f);
        if (ImGui::BeginCombo("##chain",
                              state.vault.networks.empty()
                                  ? "no networks"
                                  : state.vault.networks[static_cast<size_t>(chainIdx) %
                                                         state.vault.networks.size()]
                                        .name.c_str())) {
            for (int n = 0; n < static_cast<int>(state.vault.networks.size()); ++n)
                if (ImGui::Selectable(state.vault.networks[n].name.c_str(), chainIdx == n))
                    chainIdx = n;
            ImGui::EndCombo();
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, 10);
        ImGui::BeginGroup();
        {
            FieldOpts opts;
            opts.mono = true;
            opts.placeholder = "accountname";
            opts.width = avail * 0.28f;
            textField("Account", actorBuf, sizeof actorBuf, opts);
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, 10);
        ImGui::BeginGroup();
        {
            FieldOpts opts;
            opts.mono = true;
            opts.width = avail * 0.16f;
            textField("Permission", permBuf, sizeof permBuf, opts);
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, 10);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 22);
        if (neonButton("ADD", BtnKind::Primary, {80, 38}) && actorBuf[0] &&
            !state.vault.networks.empty()) {
            controller.addAccount(
                state.vault.networks[static_cast<size_t>(chainIdx) % state.vault.networks.size()]
                    .chainId,
                toLower(trim(actorBuf)), toLower(trim(permBuf)));
            actorBuf[0] = 0;
        }
        subtext("The account is checked on-chain; if a vault key appears in the chosen "
                "permission's authority it links automatically, otherwise it is added "
                "watch-only.");
    }
    endCard();

    drawRevealModal(state, controller);
}

}  // namespace tb::ui
