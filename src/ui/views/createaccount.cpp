// Create account: mint owner/active keys (default), shape both authorities
// (extra keys, account@permission entries, @eosio.code, thresholds), buy RAM,
// optionally delegate CPU/NET - then the pipeline ports the new account
// straight into this wallet and selects it.
#include <cstdlib>
#include <cstring>

#include "app/account_util.hpp"
#include "chain/netreg.hpp"
#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/ui_helpers.h"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

// Draft state survives page switches; it resets after a successful creation
// (the controller navigates away) via the RESET button or a name change.
struct PermDraft {
    int mode = 0;  // 0 = generate a fresh key, 1 = vault key, 2 = pasted key
    int vaultKey = -1;
    char pasted[80] = {};
    int threshold = 1;
    std::vector<acct::AccountEntry> accounts;
    bool advanced = false;
};

char g_name[16] = {};
PermDraft g_owner, g_active;
char g_ram[12] = "4096";
char g_cpu[24] = {};
char g_net[24] = {};
bool g_transfer = false;
std::string g_nameStatus;   // availability probe result
bool g_checking = false;

void resetDrafts() {
    g_owner = PermDraft{};
    g_active = PermDraft{};
    std::snprintf(g_ram, sizeof g_ram, "4096");
    g_cpu[0] = g_net[0] = 0;
    g_transfer = false;
    g_nameStatus.clear();
}

// One permission's editor: key source + advanced authority table.
void drawPermEditor(AppState& state, const char* title, PermDraft& draft,
                    const char* hint) {
    const bool phone = layout().phone();
    ImGui::PushID(title);
    sectionTitle(title);
    subtext(hint);

    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(fonts().uiSemi, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::CyanDim));
    ImGui::TextUnformatted("Key");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine(::ui::S(86.0f));
    ImGui::SetNextItemWidth(::ui::S(phone ? 186.0f : 230.0f));
    const char* modes[] = {"generate a fresh key (default)", "use a vault key",
                           "paste a public key"};
    ImGui::Combo("##mode", &draft.mode, modes, 3);
    ::ui::HandOnHover();

    if (draft.mode == 1) {
        ImGui::Dummy({::ui::S(78.0f), 0});
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        const auto& keys = state.vault.keys;
        std::string current =
            draft.vaultKey >= 0 && draft.vaultKey < static_cast<int>(keys.size())
                ? keys[static_cast<size_t>(draft.vaultKey)].label + "  " +
                      middleEllipsis(keys[static_cast<size_t>(draft.vaultKey)].pub, 12, 6)
                : "pick a key...";
        if (ImGui::BeginCombo("##vkey", current.c_str())) {
            for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
                ImGui::PushID(i);
                std::string label = keys[static_cast<size_t>(i)].label + "  " +
                                    middleEllipsis(keys[static_cast<size_t>(i)].pub, 12, 6);
                if (ImGui::Selectable(label.c_str(), draft.vaultKey == i))
                    draft.vaultKey = i;
                ::ui::HandOnHover();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ::ui::HandOnHover();
    } else if (draft.mode == 2) {
        ImGui::Dummy({::ui::S(78.0f), 0});
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::PushFont(fonts().mono, kMonoSm);
        ImGui::InputTextWithHint("##pasted", "PUB_K1_... or EOS...", draft.pasted,
                                 sizeof draft.pasted);
        ImGui::PopFont();
    }

    // Advanced: extra account@permission authorities + threshold.
    ImGui::Dummy({::ui::S(78.0f), 0});
    ImGui::SameLine();
    if (ImGui::Checkbox("advanced authorities##adv", &draft.advanced)) {}
    ::ui::HandOnHover();
    if (draft.advanced) {
        if (ImGui::BeginTable("##auths", 4, ImGuiTableFlags_SizingFixedFit |
                                                ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn("pad", ImGuiTableColumnFlags_WidthFixed,
                                    ::ui::S(78.0f));
            ImGui::TableSetupColumn("who", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("weight", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("rm", ImGuiTableColumnFlags_WidthFixed);
            for (size_t i = 0; i < draft.accounts.size(); ++i) {
                acct::AccountEntry& entry = draft.accounts[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                monoText(entry.actor + "@" + entry.permission, col::Ice, kMonoSm);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(::ui::S(64.0f));
                ImGui::InputInt("##w", &entry.weight, 0);
                if (entry.weight < 1) entry.weight = 1;
                ImGui::TableNextColumn();
                if (iconButton("##rm", Icon::Trash, "Remove authority", col::Slate, 12.0f))
                    draft.accounts.erase(draft.accounts.begin() +
                                         static_cast<ptrdiff_t>(i--));
                ImGui::PopID();
            }
            // Add row.
            static char actorBuf[16] = {}, permBuf[16] = {};
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            float half = ImGui::GetContentRegionAvail().x * 0.5f;
            ImGui::SetNextItemWidth(half);
            ImGui::PushFont(fonts().mono, kMonoSm);
            ImGui::InputTextWithHint("##aactor", "account", actorBuf, sizeof actorBuf);
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##aperm", "permission (active)", permBuf,
                                     sizeof permBuf);
            ImGui::PopFont();
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            if (iconButton("##addauth", Icon::Plus, "Add authority", col::CyanDim, 13.0f) &&
                actorBuf[0]) {
                draft.accounts.push_back(
                    {toLower(trim(actorBuf)),
                     permBuf[0] ? toLower(trim(permBuf)) : std::string("active"), 1});
                actorBuf[0] = permBuf[0] = 0;
            }
            ImGui::EndTable();
        }
        // Quick-add: let the account's own contract act through this
        // permission (required for deployed contracts with inline actions).
        ImGui::Dummy({::ui::S(78.0f), 0});
        ImGui::SameLine();
        if (neonButton("+ @EOSIO.CODE", BtnKind::Subtle, {::ui::S(130.0f), 26})) {
            std::string self = toLower(trim(g_name));
            bool present = false;
            for (const auto& entry : draft.accounts)
                if (entry.actor == self && entry.permission == "eosio.code") present = true;
            if (self.empty()) {
                // Needs the name; the hint below explains.
            } else if (!present) {
                draft.accounts.push_back({self, "eosio.code", 1});
            }
        }
        tooltip("Adds <new account>@eosio.code so a contract deployed on the\n"
                "account can authorize its own inline actions. Type the account\n"
                "name first.");
        ImGui::SameLine(0, 10);
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(fonts().ui, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
        ImGui::TextUnformatted("threshold");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine(0, 6);
        ImGui::SetNextItemWidth(::ui::S(64.0f));
        ImGui::InputInt("##thr", &draft.threshold, 0);
        if (draft.threshold < 1) draft.threshold = 1;
    }
    ::ui::VSpace(0.5f);
    ImGui::PopID();
}

// Assemble the controller spec from a permission draft.
bool fillAuth(const AppState& state, const PermDraft& draft, acct::AuthorityDraft& out,
              bool& generate, std::string& error) {
    out.threshold = draft.threshold;
    out.accounts = draft.accounts;
    generate = draft.mode == 0;
    if (draft.mode == 1) {
        if (draft.vaultKey < 0 ||
            draft.vaultKey >= static_cast<int>(state.vault.keys.size())) {
            error = "pick a vault key";
            return false;
        }
        out.keys.push_back({state.vault.keys[static_cast<size_t>(draft.vaultKey)].pub, 1});
    } else if (draft.mode == 2) {
        std::string pasted = trim(draft.pasted);
        if (pasted.empty()) {
            error = "paste a public key";
            return false;
        }
        out.keys.push_back({pasted, 1});
    }
    return true;
}

}  // namespace

void drawCreateAccount(AppState& state, Controller& controller) {
    const AccountRef* creator = state.currentAccount();
    const NetworkDef* network = state.currentNetwork();
    heading("Create account");
    subtext("Register a brand-new on-chain account: TackleBox mints its owner and "
            "active keys into this vault by default, the selected account pays for "
            "RAM and stake, and the finished account is added to this wallet "
            "automatically.");
    vspace(8);

    if (!creator || !network) {
        emptyState(Icon::Plus, "No paying account",
                   "Select the account that will pay for the creation first.");
        return;
    }

    float colW = pairWidth();

    // --- identity ----------------------------------------------------------
    if (beginCard("identity")) {
        sectionTitle("New account");
        kvRow("Creator (pays)", creator->actor + "@" + creator->permission, true);
        kvRow("Chain", network->name);
        ::ui::VSpace(0.3f);
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(fonts().uiSemi, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::CyanDim));
        ImGui::TextUnformatted("Name");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine(::ui::S(86.0f));
        ImGui::SetNextItemWidth(::ui::S(layout().phone() ? 140.0f : 200.0f));
        ImGui::PushFont(fonts().mono, kText);
        if (ImGui::InputTextWithHint("##name", "myaccount123", g_name, sizeof g_name))
            g_nameStatus.clear();
        ImGui::PopFont();
        ImGui::SameLine(0, 6);
        if (neonButton("CHECK", BtnKind::Subtle, {::ui::S(76.0f), 30}) && g_name[0]) {
            g_checking = true;
            g_nameStatus.clear();
            controller.checkAccountName(toLower(trim(g_name)),
                                        [](bool exists, std::string error) {
                                            g_checking = false;
                                            g_nameStatus = !error.empty()
                                                               ? "check failed: " + error
                                                               : exists ? "taken" : "available";
                                        });
        }
        if (g_checking) {
            ImGui::SameLine(0, 8);
            spinner(10.0f);
        }

        bool premium = false;
        std::string nameError = acct::validateAccountName(toLower(trim(g_name)), &premium);
        ImGui::Dummy({::ui::S(78.0f), 0});
        ImGui::SameLine();
        ImGui::PushFont(fonts().ui, kTextSm);
        if (g_name[0] && !nameError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
            ImGui::TextWrapped("%s", nameError.c_str());
            ImGui::PopStyleColor();
        } else if (premium) {
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
            ImGui::TextWrapped("short / dotted names need a name bid or the suffix "
                               "owner as creator");
            ImGui::PopStyleColor();
        } else if (!g_nameStatus.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  col::vec(g_nameStatus == "available" ? col::Success
                                                                       : col::Warn));
            ImGui::TextUnformatted(g_nameStatus.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
            ImGui::TextUnformatted("12 characters: a-z and 1-5");
            ImGui::PopStyleColor();
        }
        ImGui::PopFont();
    }
    endCard();
    vspace(10);

    // --- authorities -------------------------------------------------------
    if (beginCard("owner", colW)) {
        drawPermEditor(state, "Owner authority", g_owner,
                       "The master permission: it can rewrite every other permission. "
                       "Keep its key cold if you can.");
    }
    endCard();
    maybeSameLine();
    if (beginCard("active", colW)) {
        drawPermEditor(state, "Active authority", g_active,
                       "The everyday permission: transfers, votes, contract actions. "
                       "This wallet signs with it after creation.");
    }
    endCard();
    vspace(10);

    // --- resources ---------------------------------------------------------
    if (beginCard("resources")) {
        sectionTitle("Resources for the new account");
        if (ImGui::BeginTable("##res", 4, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("l1", ImGuiTableColumnFlags_WidthFixed,
                                    ::ui::S(86.0f));
            ImGui::TableSetupColumn("f1", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("l2", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("f2", ImGuiTableColumnFlags_WidthFixed);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::CyanDim));
            ImGui::TextUnformatted("RAM");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(::ui::S(110.0f));
            ImGui::PushFont(fonts().mono, kMonoSm);
            ImGui::InputTextWithHint("##ram", "bytes", g_ram, sizeof g_ram);
            ImGui::PopFont();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
            ImGui::TextUnformatted(layout().phone()
                                       ? "bytes"
                                       : "bytes (min 1024; 4096 fits most accounts)");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::TableNextColumn();

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::CyanDim));
            ImGui::TextUnformatted("Stake");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(::ui::S(110.0f));
            ImGui::PushFont(fonts().mono, kMonoSm);
            ImGui::InputTextWithHint("##cpu", "CPU (0)", g_cpu, sizeof g_cpu);
            ImGui::PopFont();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(::ui::S(110.0f));
            ImGui::PushFont(fonts().mono, kMonoSm);
            ImGui::InputTextWithHint("##net", "NET (0)", g_net, sizeof g_net);
            ImGui::PopFont();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
            ImGui::TextUnformatted(network->coreSymbolCode().c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::EndTable();
        }
        toggle("Gift the stake to the new account", &g_transfer,
               "delegatebw transfer flag: the tokens become the new account's own "
               "stake instead of remaining delegated from the creator");
    }
    endCard();
    vspace(12);

    // --- submit ------------------------------------------------------------
    std::string blocker;
    std::string name = toLower(trim(g_name));
    if (acct::validateAccountName(name) != "") blocker = "enter a valid name";
    if (creator->watch) blocker = "the paying account is watch-only";
    acct::AuthorityDraft ownerAuth, activeAuth;
    Controller::NewAccountSpec spec;
    if (blocker.empty()) {
        std::string error;
        if (!fillAuth(state, g_owner, ownerAuth, spec.generateOwnerKey, error))
            blocker = "owner: " + error;
        else if (!fillAuth(state, g_active, activeAuth, spec.generateActiveKey, error))
            blocker = "active: " + error;
    }
    if (state.busyCreateAccount) {
        spinner(13.0f);
        ImGui::SameLine(0, 10);
        subtext("Creating the account...");
    } else {
        if (!blocker.empty() && g_name[0]) {
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
            ImGui::TextUnformatted(blocker.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        if (neonButton("CREATE ACCOUNT", BtnKind::Primary, {::ui::S(190.0f), 42},
                       !blocker.empty())) {
            spec.name = name;
            spec.owner = ownerAuth;
            spec.active = activeAuth;
            spec.ramBytes = std::atoll(g_ram);
            spec.cpuStake = trim(g_cpu);
            spec.netStake = trim(g_net);
            spec.transferStake = g_transfer;
            controller.createAccount(spec);
        }
        ImGui::SameLine(0, 10);
        if (neonButton("RESET", BtnKind::Ghost, {::ui::S(90.0f), 42})) {
            g_name[0] = 0;
            resetDrafts();
        }
    }
}

}  // namespace tb::ui
