// Contract explorer: load any deployed contract's ABI, build actions from
// generated forms, browse tables. The ESR lane lives here too.
#include <cstring>
#include <map>

#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/qa.hpp"
#include "ui/ui_helpers.h"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

// Per-action form input storage: field path -> typed-in text.
std::map<std::string, std::string> formValues;
std::string formAction;  // which action the form belongs to

const dwarfkit::ABI::Struct* findStruct(const dwarfkit::ABI& abi, const std::string& name) {
    for (const auto& s : abi.structs)
        if (s.name == name) return &s;
    return nullptr;
}

// Resolve one level of typedefs so hints show the real type.
std::string resolveAlias(const dwarfkit::ABI& abi, const std::string& type) {
    for (const auto& td : abi.types)
        if (td.new_type_name == type) return td.type;
    return type;
}

bool typeIsBool(const std::string& t) { return t == "bool"; }
bool typeIsNumeric(const std::string& t) {
    return t.rfind("uint", 0) == 0 || t.rfind("int", 0) == 0 || t.rfind("float", 0) == 0 ||
           t == "varuint32" || t == "varint32";
}

// Convert one typed field text into action-data json.
dwarfkit::json fieldToJson(const std::string& type, const std::string& text) {
    std::string t = type;
    // Arrays/optionals/etc fall through as raw JSON if the user typed JSON.
    if (!text.empty() && (text.front() == '[' || text.front() == '{')) {
        auto parsed = dwarfkit::json::parse(text, nullptr, false);
        if (!parsed.is_discarded()) return parsed;
    }
    if (typeIsBool(t)) return text == "true" || text == "1";
    if (typeIsNumeric(t)) {
        // Big integers travel as strings in ABI JSON; let the serializer parse.
        if (t == "float32" || t == "float64") {
            try {
                return std::stod(text);
            } catch (...) {
                return text;
            }
        }
        try {
            if (t.rfind("uint", 0) == 0 || t == "varuint32") {
                unsigned long long v = std::stoull(text);
                if (v <= 0xFFFFFFFFull) return static_cast<uint64_t>(v);
                return text;  // 64-bit range: string form is canonical
            }
            long long v = std::stoll(text);
            if (v >= INT32_MIN && v <= INT32_MAX) return static_cast<int64_t>(v);
            return text;
        } catch (...) {
            return text;
        }
    }
    return text;
}

void drawActionForm(AppState& state, Controller& controller, const dwarfkit::ABI& abi,
                    const dwarfkit::ABI::Action& action) {
    const auto* structDef = findStruct(abi, action.type);
    std::string actionName = dwarfkit::Name(action.name).toString();
    if (formAction != actionName) {
        formAction = actionName;
        formValues.clear();
    }

    ImGui::PushFont(fonts().uiSemi, kTextLg);
    ImGui::TextUnformatted(actionName.c_str());
    ImGui::PopFont();
    // Bookmark: pinned actions surface in the saved-actions dropdown.
    {
        const NetworkDef* net = state.currentNetwork();
        std::string chainId = net ? net->chainId : "";
        SavedAction key{chainId, state.contracts.account, actionName};
        bool saved = false;
        for (const auto& s : state.vault.savedActions)
            if (s.chainId == key.chainId && s.contract == key.contract &&
                s.action == key.action)
                saved = true;
        ImGui::SameLine(0, 8);
        if (iconButton("##savea", Icon::Pin,
                       saved ? "Remove from saved actions" : "Save this action",
                       saved ? col::Cyan : col::Slate, 13.0f)) {
            if (saved)
                controller.removeActionBookmark(key);
            else
                controller.saveActionBookmark(key);
        }
    }
    if (!structDef) {
        subtext("No parameter struct found in the ABI for this action.");
    } else {
        // Include base-struct fields first (rare but legal).
        std::vector<dwarfkit::ABI::Field> fields;
        std::string base = structDef->base;
        std::vector<const dwarfkit::ABI::Struct*> chain{structDef};
        while (!base.empty()) {
            const auto* baseStruct = findStruct(abi, base);
            if (!baseStruct) break;
            chain.insert(chain.begin(), baseStruct);
            base = baseStruct->base;
        }
        for (const auto* s : chain)
            fields.insert(fields.end(), s->fields.begin(), s->fields.end());

        for (const auto& field : fields) {
            std::string resolved = resolveAlias(abi, field.type);
            std::string& value = formValues[field.name];
            char buf[512];
            std::snprintf(buf, sizeof buf, "%s", value.c_str());
            FieldOpts opts;
            opts.mono = true;
            std::string hint = resolved;
            if (resolved != field.type) hint += "  (alias of " + field.type + ")";
            opts.placeholder = hint.c_str();
            std::string label = field.name;
            if (textField(label.c_str(), buf, sizeof buf, opts)) { /* enter: fallthrough */ }
            value = buf;
        }
    }

    vspace(8);
    const AccountRef* account = state.currentAccount();
    bool watch = !account || account->watch;
    auto collectData = [&]() {
        dwarfkit::json data = dwarfkit::json::object();
        if (structDef)
            for (const auto& field : structDef->fields)
                data[field.name] = fieldToJson(resolveAlias(abi, field.type),
                                               formValues[field.name]);
        return data;
    };
    if (state.busyContract) {
        spinner(13.0f);
    } else {
        if (neonButton("EXECUTE", BtnKind::Primary, {200, 42}, watch))
            controller.runContractAction(state.contracts.account, actionName, collectData());
        ImGui::SameLine(0, 8);
        if (neonButton("TO MSIG", BtnKind::Ghost, {110, 42}, !account)) {
            controller.stageMsigAction(
                {{"account", state.contracts.account},
                 {"name", actionName},
                 {"authorization",
                  dwarfkit::json::array({{{"actor", account->actor},
                                          {"permission", account->permission}}})},
                 {"data", collectData()}});
        }
    }
    if (watch) subtext("Watch-only account: execution disabled (staging to msig works).");
}

void drawEsrLane(AppState& state, Controller& controller) {
    if (beginCard("esr")) {
        sectionTitle("Signing request (ESR)");
        subtext("Paste an esr:// payload from a dapp. Transactions run through the guard "
                "and open the signing review; a login (identity) request creates a live "
                "link session instead - the dapp can then push requests directly.");
        static char esr[4096] = {};
        FieldOpts opts;
        opts.mono = true;
        opts.placeholder = "esr://gmNgZ...";
        textField("##esr", esr, sizeof esr, opts);
        if (state.busyEsr) {
            spinner(13.0f);
        } else {
            if (neonButton("DECODE & REVIEW", BtnKind::Ghost, {200, 38})) {
                std::string uri = trim(esr);
                if (uri.rfind("esr:", 0) == 0 || uri.rfind("esr-anchor:", 0) == 0)
                    controller.signEsr(uri);
                else
                    controller.toast(Toast::Error, "That does not look like an esr: payload");
            }
            ImGui::SameLine(0, 8);
            if (neonButton("CONNECT AS LOGIN", BtnKind::Subtle, {170, 38})) {
                std::string uri = trim(esr);
                if (uri.rfind("esr:", 0) == 0 || uri.rfind("esr-anchor:", 0) == 0)
                    controller.linkLogin(uri);
                else
                    controller.toast(Toast::Error, "That does not look like an esr: payload");
            }
        }
    }
    endCard();
}

void drawDeployTab(AppState& state, Controller& controller) {
    DeployViewState& dv = state.deploy;
    const AccountRef* account = state.currentAccount();
    if (beginCard("deploy")) {
        sectionTitle("Deploy to the active account");
        subtext(account
                    ? ("setcode/setabi on " + account->display() +
                       ". Both files are optional individually - deploy just an ABI or "
                       "just code. This is flagged CRITICAL by the risk engine and "
                       "requires hold-to-sign. Any whitelist pins others hold on this "
                       "contract will suspend.")
                          .c_str()
                    : "Select an account first.");
        vspace(4);

        static char wasmBuf[512] = {};
        static char abiBuf[512] = {};
        if (!dv.wasmPath.empty() && !wasmBuf[0])
            std::snprintf(wasmBuf, sizeof wasmBuf, "%s", dv.wasmPath.c_str());
        if (!dv.abiPath.empty() && !abiBuf[0])
            std::snprintf(abiBuf, sizeof abiBuf, "%s", dv.abiPath.c_str());

        FieldOpts opts;
        opts.mono = true;
        opts.placeholder = "path to contract.wasm (or drop the file on the window)";
        textField("WASM file", wasmBuf, sizeof wasmBuf, opts);
        opts.placeholder = "path to contract.abi";
        textField("ABI file", abiBuf, sizeof abiBuf, opts);

        if (neonButton("READ FILES", BtnKind::Ghost, {130, 36}))
            controller.previewDeploy(trim(wasmBuf), trim(abiBuf));

        if (!dv.wasmHashPreview.empty()) {
            kvRow("Local code hash", dv.wasmHashPreview, true, true);
            kvRow("Size", std::to_string(dv.wasmBytes) + " bytes", true);
            subtext("After deployment the on-chain code hash must equal this value - "
                    "verify it on the Contracts page afterwards.");
        }
        vspace(6);
        if (dv.busy) {
            spinner(13.0f);
        } else if (neonButton("DEPLOY", BtnKind::Danger, {160, 42},
                              !account || account->watch ||
                                  (dv.wasmPath.empty() && dv.abiPath.empty()))) {
            controller.deployContract();
        }
    }
    endCard();
}

}  // namespace

void drawContracts(AppState& state, Controller& controller) {
    heading("Contracts");
    subtext("Inspect any deployed contract: actions, tables, and the exact code/ABI "
            "hashes the whitelist can pin. Or deploy your own.");
    vspace(8);

    static int mode = 0;
    const char* modes[] = {"EXPLORE", "DEPLOY"};
    for (int i = 0; i < 2; ++i) {
        if (neonButton(modes[i], mode == i ? BtnKind::Primary : BtnKind::Subtle, {110, 32}))
            mode = i;
        if (i < 1) ImGui::SameLine(0, 6);
    }
    vspace(8);
    if (mode == 1) {
        drawDeployTab(state, controller);
        return;
    }

    static char contractBuf[64] = {};
    {
        float avail = ImGui::GetContentRegionAvail().x;
        FieldOpts opts;
        opts.mono = true;
        opts.placeholder = "contract account (eosio.token, atomicassets, ...)";
        opts.width = avail - 130.0f;
        bool entered = textField("##contract", contractBuf, sizeof contractBuf, opts);
        ImGui::SameLine(0, 8);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1);
        if ((neonButton("LOAD", BtnKind::Primary, {110, 38}) || entered) && contractBuf[0])
            controller.loadContract(trim(contractBuf));
    }

    // Saved contracts + saved actions: per-chain dropdowns for one-click
    // recall; the pin icons (contract card / action form) feed them.
    {
        const NetworkDef* net = state.currentNetwork();
        std::string chainId = net ? net->chainId : "";
        int savedContracts = 0, savedActions = 0;
        for (const auto& saved : state.vault.savedContracts)
            if (saved.chainId == chainId) ++savedContracts;
        for (const auto& saved : state.vault.savedActions)
            if (saved.chainId == chainId) ++savedActions;
        if (savedContracts > 0 || savedActions > 0) {
            if (savedContracts > 0) {
                ImGui::SetNextItemWidth(::ui::S(220.0f));
                if (qa::forceOpen("saved-contracts")) qa::openCombo("##savedc");
                if (ImGui::BeginCombo("##savedc", "saved contracts...")) {
                    for (const auto& saved : state.vault.savedContracts) {
                        if (saved.chainId != chainId) continue;
                        ImGui::PushID(saved.account.c_str());
                        if (ImGui::Selectable(saved.account.c_str(), false)) {
                            std::snprintf(contractBuf, sizeof contractBuf, "%s",
                                          saved.account.c_str());
                            controller.loadContract(saved.account);
                        }
                        ::ui::HandOnHover();
                        ImGui::SameLine();
                        if (iconButton("##rmsc", Icon::Trash, "Remove bookmark",
                                       col::Slate, 11.0f))
                            controller.removeContractBookmark(chainId, saved.account);
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ::ui::HandOnHover();
            }
            if (savedActions > 0) {
                if (savedContracts > 0) ImGui::SameLine(0, 8);
                ImGui::SetNextItemWidth(::ui::S(240.0f));
                if (qa::forceOpen("saved-actions")) qa::openCombo("##saveda");
                if (ImGui::BeginCombo("##saveda", "saved actions...")) {
                    for (const auto& saved : state.vault.savedActions) {
                        if (saved.chainId != chainId) continue;
                        std::string label = saved.contract + "::" + saved.action;
                        ImGui::PushID(label.c_str());
                        if (ImGui::Selectable(label.c_str(), false)) {
                            std::snprintf(contractBuf, sizeof contractBuf, "%s",
                                          saved.contract.c_str());
                            state.contracts.jumpAction = saved.action;
                            controller.loadContract(saved.contract);
                        }
                        ::ui::HandOnHover();
                        ImGui::SameLine();
                        if (iconButton("##rmsa", Icon::Trash, "Remove bookmark",
                                       col::Slate, 11.0f))
                            controller.removeActionBookmark(saved);
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ::ui::HandOnHover();
            }
            vspace(2);
        }
    }
    vspace(8);

    ContractsViewState& cv = state.contracts;
    if (cv.loading) {
        spinner(14.0f);
        return;
    }
    if (!cv.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextWrapped("%s", cv.error.c_str());
        ImGui::PopStyleColor();
    }
    if (!cv.abi) {
        drawEsrLane(state, controller);
        return;
    }

    // Contract identity strip.
    if (beginCard("cid")) {
        ImGui::PushFont(fonts().uiSemi, kTextLg);
        ImGui::TextUnformatted(cv.account.c_str());
        ImGui::PopFont();
        // Bookmark toggle: the pin keeps this contract on the chip row above.
        {
            const NetworkDef* net = state.currentNetwork();
            std::string chainId = net ? net->chainId : "";
            bool saved = false;
            for (const auto& s : state.vault.savedContracts)
                if (s.chainId == chainId && s.account == cv.account) saved = true;
            ImGui::SameLine();
            float pinX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(pinX - 26);
            if (iconButton("##savec", Icon::Pin,
                           saved ? "Remove from saved contracts" : "Save this contract",
                           saved ? col::Cyan : col::Slate, 14.0f)) {
                if (saved)
                    controller.removeContractBookmark(chainId, cv.account);
                else
                    controller.saveContractBookmark({chainId, cv.account, ""});
            }
        }
        kvRow("Code hash", cv.codeHash.empty() ? "-" : cv.codeHash, true, true);
        kvRow("ABI hash", cv.abiHash.empty() ? "-" : cv.abiHash, true, true);
        subtext("A whitelist rule pinned to this contract locks onto these exact hashes; "
                "any redeploy suspends the rule.");
    }
    endCard();
    vspace(10);

    static int tab = 0;
    if (!cv.jumpAction.empty()) tab = 0;  // a saved-action pick lands here
    if (qa::wantsOpen("table-combo")) tab = 1;  // the combo lives in TABLES
    const char* tabs[] = {"ACTIONS", "TABLES"};
    for (int i = 0; i < 2; ++i) {
        bool active = tab == i;
        if (neonButton(tabs[i], active ? BtnKind::Primary : BtnKind::Subtle, {110, 32}))
            tab = i;
        if (i == 0) ImGui::SameLine(0, 8);
    }
    vspace(8);

    if (tab == 0) {
        // Two columns: action list, then the generated form.
        float listW = 240.0f;
        static std::string selected;
        if (!cv.jumpAction.empty()) {
            selected = cv.jumpAction;
            cv.jumpAction.clear();
        }
        if (beginCard("actlist", listW)) {
            for (const auto& action : cv.abi->actions) {
                std::string name = dwarfkit::Name(action.name).toString();
                bool isSelected = selected == name;
                ImGui::PushID(name.c_str());
                if (ImGui::Selectable("##a", isSelected, 0, {0, 26})) selected = name;
                ImVec2 rmin = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(fonts().mono, kMono, {rmin.x + 6, rmin.y + 5},
                                                    isSelected ? col::Cyan : col::Ice,
                                                    name.c_str());
                ImGui::PopID();
            }
            if (cv.abi->actions.empty()) subtext("No actions in this ABI.");
        }
        endCard();
        ImGui::SameLine(0, 12);
        if (beginCard("actform")) {
            const dwarfkit::ABI::Action* chosen = nullptr;
            for (const auto& action : cv.abi->actions)
                if (dwarfkit::Name(action.name).toString() == selected) chosen = &action;
            if (!chosen)
                subtext("Select an action to build a transaction from its ABI.");
            else
                drawActionForm(state, controller, *cv.abi, *chosen);
        }
        endCard();
    } else {
        if (beginCard("tables")) {
            static char scopeBuf[64] = {};
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(200);
            static std::string tableSel;
            if (qa::forceOpen("table-combo")) qa::openCombo("##table");
        if (ImGui::BeginCombo("##table", tableSel.empty() ? "table..." : tableSel.c_str())) {
                for (const auto& table : cv.abi->tables) {
                    std::string name = dwarfkit::Name(table.name).toString();
                    if (ImGui::Selectable(name.c_str(), tableSel == name)) tableSel = name;
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine(0, 8);
            FieldOpts scOpts;
            scOpts.mono = true;
            scOpts.placeholder = "scope (default: contract)";
            scOpts.width = avail - 200 - 130 - 24;
            textField("##scope", scopeBuf, sizeof scopeBuf, scOpts);
            ImGui::SameLine(0, 8);
            if (neonButton("FETCH", BtnKind::Ghost, {110, 38}) && !tableSel.empty())
                controller.loadTableRows(cv.account, tableSel, trim(scopeBuf));

            vspace(8);
            if (cv.tableLoading) {
                spinner(13.0f);
            } else if (!cv.tableError.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
                ImGui::TextWrapped("%s", cv.tableError.c_str());
                ImGui::PopStyleColor();
            } else if (!cv.tableRows.is_null()) {
                // Pin this exact query to the dashboard.
                static char pinLabel[48] = {};
                static char pinField[64] = {};
                FieldOpts pinOpts;
                pinOpts.width = 180;
                pinOpts.placeholder = "label";
                textField("##pinlabel", pinLabel, sizeof pinLabel, pinOpts);
                ImGui::SameLine(0, 6);
                pinOpts.mono = true;
                pinOpts.placeholder = "field.path (optional)";
                textField("##pinfield", pinField, sizeof pinField, pinOpts);
                ImGui::SameLine(0, 6);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1);
                if (neonButton("PIN TO DASHBOARD", BtnKind::Ghost, {170, 38}) &&
                    !tableSel.empty()) {
                    const AccountRef* account = state.currentAccount();
                    PinnedQuery pin;
                    pin.id = uuid4();
                    pin.chainId = account ? account->chainId : "";
                    pin.label = pinLabel;
                    pin.contract = cv.account;
                    pin.table = tableSel;
                    pin.scope = trim(scopeBuf);
                    pin.fieldPath = trim(pinField);
                    controller.savePinnedQuery(pin);
                    pinLabel[0] = 0;
                    pinField[0] = 0;
                }
                jsonTree(cv.tableRows, "tablerows");
            }
        }
        endCard();
    }
    vspace(10);
    drawEsrLane(state, controller);
}

}  // namespace tb::ui
