// Settings: networks & endpoints, security policy, appearance, vault password.
#include <array>
#include <cstring>
#include <map>

#include <algorithm>

#include <SDL3/SDL.h>

#include "chain/netreg.hpp"
#include "core/autostart.hpp"
#include "core/paths.hpp"
#include "core/util.hpp"
#include "tb_version.h"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/ui_helpers.h"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

// One node type's pool editor: a policy header, then a table of prioritized
// nodes (enable / reorder / nickname / URL / health / remove) whose last row
// adds a new node. Edits commit by upserting the whole NetworkDef.
void drawEndpointPool(AppState& state, Controller& controller, const NetworkDef& net,
                      NodeType type, const char* hint) {
    const EndpointList& list = net.list(type);
    const bool phone = layout().phone();
    ImGui::PushID(static_cast<int>(type));

    // Header: type name + selection policy, combos aligned across pools.
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(fonts().uiSemi, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::CyanDim));
    ImGui::TextUnformatted(nodeTypeName(type));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine(::ui::S(86.0f));
    int mode = list.mode;
    ImGui::SetNextItemWidth(::ui::S(150.0f));
    const char* modes[] = {"priority", "round-robin", "auto"};
    if (ImGui::Combo("##mode", &mode, modes, 3)) {
        NetworkDef updated = net;
        updated.list(type).mode = mode;
        controller.addNetwork(updated);
    }
    ::ui::HandOnHover();
    tooltip("priority: always the best enabled node.\n"
            "round-robin: rotate every request.\n"
            "auto: priority normally, round-robin under load.");

    // Auto policy: the user's IF sentence, on its own line so it fits phones.
    if (list.mode == static_cast<int>(SelectMode::Auto)) {
        if (ImGui::BeginTable("##if", 5, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (!phone) {
                ImGui::Dummy({::ui::S(78.0f), 0});
                ImGui::SameLine(0, 0);
            }
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
            ImGui::TextUnformatted("round-robin IF >");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::TableNextColumn();
            int threshold = list.autoThresholdQueries;
            ImGui::SetNextItemWidth(::ui::S(phone ? 56.0f : 64.0f));
            ImGui::InputInt("##thr", &threshold, 0);
            if (ImGui::IsItemDeactivatedAfterEdit() && threshold > 0) {
                NetworkDef updated = net;
                updated.list(type).autoThresholdQueries = threshold;
                controller.addNetwork(updated);
            }
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
            ImGui::TextUnformatted("queries in");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::TableNextColumn();
            int window = list.autoWindowSec;
            ImGui::SetNextItemWidth(::ui::S(phone ? 56.0f : 64.0f));
            ImGui::InputInt("##win", &window, 0);
            if (ImGui::IsItemDeactivatedAfterEdit() && window >= 5) {
                NetworkDef updated = net;
                updated.list(type).autoWindowSec = window;
                controller.addNetwork(updated);
            }
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
            if (auto svc = controller.currentService();
                svc && svc->net().chainId == net.chainId)
                ImGui::Text("s   (recent: %d)", svc->recentQueries(type));
            else
                ImGui::TextUnformatted("s");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::EndTable();
        }
    }

    // Nodes, best priority first; the last row adds a node.
    std::vector<size_t> order;
    for (size_t i = 0; i < list.nodes.size(); ++i) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return list.nodes[a].priority < list.nodes[b].priority;
    });
    // Phones fold nickname+URL into one stacked cell and skip the health
    // column; desktop/tablet keep the wide 6-column grid.
    const int columns = phone ? 4 : 6;
    ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX;
    if (!ImGui::BeginTable("##nodes", columns, tableFlags)) {
        ImGui::PopID();
        return;
    }
    ImGui::TableSetupColumn("en", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("ord", ImGuiTableColumnFlags_WidthFixed);
    if (phone) {
        ImGui::TableSetupColumn("node", ImGuiTableColumnFlags_WidthStretch);
    } else {
        ImGui::TableSetupColumn("nick", ImGuiTableColumnFlags_WidthFixed, ::ui::S(130.0f));
        ImGui::TableSetupColumn("url", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("health", ImGuiTableColumnFlags_WidthFixed, ::ui::S(96.0f));
    }
    ImGui::TableSetupColumn("rm", ImGuiTableColumnFlags_WidthFixed);

    const auto healthIt = state.health.find(net.chainId);
    for (size_t row = 0; row < order.size(); ++row) {
        const Endpoint& node = list.nodes[order[row]];
        ImGui::PushID(static_cast<int>(order[row]));
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        bool enabled = node.enabled;
        if (ImGui::Checkbox("##en", &enabled)) {
            NetworkDef updated = net;
            updated.list(type).nodes[order[row]].enabled = enabled;
            controller.addNetwork(updated);
        }
        ::ui::HandOnHover();
        tooltip(enabled ? "Enabled" : "Disabled");

        // Reorder: drag the grip onto another row, or nudge with the
        // chevrons; both rewrite every node's priority as the visual order.
        ImGui::TableNextColumn();
        auto commitOrder = [&](const std::vector<size_t>& moved) {
            NetworkDef updated = net;
            for (size_t p = 0; p < moved.size(); ++p)
                updated.list(type).nodes[moved[p]].priority = static_cast<int>(p);
            controller.addNetwork(updated);
        };
        auto reorder = [&](int delta) {
            std::vector<size_t> moved = order;
            size_t target = row + static_cast<size_t>(delta);
            std::swap(moved[row], moved[target]);
            commitOrder(moved);
        };
        std::string poolList =
            "##pool" + net.chainId + std::to_string(static_cast<int>(type));
        int droppedRow = dragGrip(poolList.c_str(), static_cast<int>(row), node.url.c_str());
        if (droppedRow >= 0 && droppedRow != static_cast<int>(row) &&
            droppedRow < static_cast<int>(order.size())) {
            std::vector<size_t> moved = order;
            size_t item = moved[static_cast<size_t>(droppedRow)];
            moved.erase(moved.begin() + droppedRow);
            moved.insert(moved.begin() + static_cast<ptrdiff_t>(row), item);
            commitOrder(moved);
        }
        ImGui::SameLine(0, 2);
        ImGui::BeginDisabled(row == 0);
        if (iconButton("##up", Icon::ChevronUp, "Raise priority", col::Slate, 12.0f))
            reorder(-1);
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
        ImGui::BeginDisabled(row + 1 >= order.size());
        if (iconButton("##down", Icon::ChevronDown, "Lower priority", col::Slate, 12.0f))
            reorder(+1);
        ImGui::EndDisabled();

        // Nickname, inline-editable; commits when the field loses focus. On
        // phones the URL stacks under it in the same cell.
        ImGui::TableNextColumn();
        static std::map<std::string, std::array<char, 40>> nickBufs;
        std::string nickKey = net.chainId + std::to_string(static_cast<int>(type)) +
                              std::to_string(order[row]);
        auto& nickBuf = nickBufs[nickKey];
        if (!ImGui::IsAnyItemActive())
            std::snprintf(nickBuf.data(), nickBuf.size(), "%s", node.nickname.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::PushFont(fonts().ui, kTextSm);
        ImGui::InputTextWithHint("##nick", "nickname", nickBuf.data(), nickBuf.size());
        ImGui::PopFont();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            NetworkDef updated = net;
            updated.list(type).nodes[order[row]].nickname = nickBuf.data();
            controller.addNetwork(updated);
        }

        const ImU32 urlColor = node.enabled ? (row == 0 ? col::Ice : col::Steel)
                                            : col::Slate;
        if (!phone) ImGui::TableNextColumn();
        if (!phone) ImGui::AlignTextToFramePadding();
        monoText(node.url, urlColor, kMonoSm);

        if (!phone) {
            ImGui::TableNextColumn();
            if (healthIt != state.health.end())
                for (const auto& h : healthIt->second)
                    if (h.type == type && h.url == node.url) {
                        ImGui::AlignTextToFramePadding();
                        ImGui::PushFont(fonts().mono, kMonoSm);
                        if (h.ok && h.chainIdMatches) {
                            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Success));
                            ImGui::Text("%d ms", h.latencyMs);
                            ImGui::PopStyleColor();
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
                            ImGui::TextUnformatted("down");
                            ImGui::PopStyleColor();
                            if (!h.error.empty()) tooltip(h.error.c_str());
                        }
                        ImGui::PopFont();
                    }
        }

        ImGui::TableNextColumn();
        if (iconButton("##rm", Icon::Trash, "Remove node", col::Slate, 12.0f)) {
            NetworkDef updated = net;
            auto& nodes = updated.list(type).nodes;
            nodes.erase(nodes.begin() + static_cast<ptrdiff_t>(order[row]));
            controller.addNetwork(updated);
        }
        ImGui::PopID();
    }

    // Add row: plus commits, as does Enter in the URL field.
    {
        static std::map<std::string, std::array<char, 160>> urlBufs;
        static std::map<std::string, std::array<char, 40>> addNickBufs;
        std::string key = net.chainId + std::to_string(static_cast<int>(type));
        auto& urlBuf = urlBufs[key];
        auto& nickBuf = addNickBufs[key];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        bool commit = iconButton("##add", Icon::Plus, "Add node", col::CyanDim, 13.0f);
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::PushFont(fonts().ui, kTextSm);
        ImGui::InputTextWithHint("##addnick", "nickname", nickBuf.data(), nickBuf.size());
        ImGui::PopFont();
        if (!phone) ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::PushFont(fonts().mono, kMonoSm);
        commit |= ImGui::InputTextWithHint("##addurl", hint, urlBuf.data(), urlBuf.size(),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopFont();
        if (commit && urlBuf[0]) {
            std::string url = trim(urlBuf.data());
            std::string why;
            if (!endpointAllowed(url, &why)) {
                controller.toast(Toast::Error, why);
            } else {
                NetworkDef updated = net;
                auto& nodes = updated.list(type).nodes;
                nodes.push_back({url, nickBuf.data(),
                                 static_cast<int>(nodes.size()), true});
                controller.addNetwork(updated);
                urlBuf[0] = 0;
                nickBuf[0] = 0;
            }
        }
    }
    ImGui::EndTable();
    ::ui::VSpace(0.5f);
    ImGui::PopID();
}

void drawNetworks(AppState& state, Controller& controller) {
    if (!beginCard("networks")) {
        endCard();
        return;
    }
    sectionTitle("Networks & endpoints");
    subtext("Per chain, per node type: RPC (nodeos), Atomic Assets (NFTs), Hyperion "
            "(history) and Light API (all-token balances). Priority mode uses the best "
            "enabled node; round-robin splits requests across them; auto switches to "
            "round-robin only past your query threshold. Everything lives inside the "
            "encrypted vault, and RPC chain identity is verified on every probe.");
    ::ui::VSpace(0.4f);

    for (const auto& net : state.vault.networks) {
        ImGui::PushID(net.chainId.c_str());
        ImGui::PushFont(fonts().uiSemi, kTextLg);
        ImGui::TextUnformatted(net.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        if (net.testnet) badge("TESTNET", col::Warn);
        if (!layout().phone()) {
            // The chain id is informational; phones cede the space to actions.
            ImGui::SameLine();
            ImGui::PushFont(fonts().mono, kMonoSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
            ImGui::TextUnformatted(middleEllipsis(net.chainId, 12, 8).c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::SameLine();
        float endX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(endX - ::ui::S(124.0f));
        if (neonButton("PROBE ALL", BtnKind::Subtle, {::ui::S(90.0f), 26}))
            controller.probeEndpoints(net.chainId);
        ImGui::SameLine(0, 4);
        if (iconButton("##rmnet", Icon::Trash, "Remove network", col::Slate, 13.0f))
            controller.removeNetwork(net.chainId);

        drawEndpointPool(state, controller, net, NodeType::Rpc,
                         "https://rpc.example.com");
        drawEndpointPool(state, controller, net, NodeType::Atomic,
                         "https://atomic.example.com (NFTs)");
        drawEndpointPool(state, controller, net, NodeType::Hyperion,
                         "https://hyperion.example.com (/v2 history)");
        drawEndpointPool(state, controller, net, NodeType::Light,
                         "https://lightapi.example.com (balances)");

        // Light API network slug.
        if (!net.light.empty()) {
            static std::map<std::string, std::array<char, 24>> slugBufs;
            auto& slugBuf = slugBufs[net.chainId];
            if (!ImGui::IsAnyItemActive())
                std::snprintf(slugBuf.data(), slugBuf.size(), "%s", net.lightSlug.c_str());
            ImGui::Dummy({::ui::S(8.0f), 0});
            ImGui::SameLine();
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
            ImGui::TextUnformatted("light API slug");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::SameLine(0, 6);
            ImGui::SetNextItemWidth(::ui::S(90.0f));
            ImGui::PushFont(fonts().mono, kMonoSm);
            ImGui::InputTextWithHint("##slug", "wax", slugBuf.data(), slugBuf.size());
            ImGui::PopFont();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                NetworkDef updated = net;
                updated.lightSlug = trim(slugBuf.data());
                controller.addNetwork(updated);
            }
        }

        // Price oracle: provider selector + its config. Display-only data;
        // switching providers prefills that provider's defaults.
        {
            ImGui::PushID("oracle");
            const OracleProvider provider =
                static_cast<OracleProvider>(net.oracle.provider);
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::CyanDim));
            ImGui::TextUnformatted("Price");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::SameLine(::ui::S(86.0f));
            int mode = net.oracle.provider;
            ImGui::SetNextItemWidth(::ui::S(150.0f));
            const char* providers[] = {"off", "Alcor DEX", "CoinGecko",
                                       "Delphi (on-chain)"};
            if (ImGui::Combo("##prov", &mode, providers, 4) &&
                mode != net.oracle.provider) {
                NetworkDef updated = net;
                updated.oracle =
                    oracleDefaults(net.chainId, static_cast<OracleProvider>(mode));
                controller.addNetwork(updated);
            }
            ::ui::HandOnHover();
            tooltip("USD price source for this chain's tokens. Display-only:\n"
                    "signing and whitelist decisions never read prices.\n"
                    "Alcor prices every listed token; CoinGecko and Delphi\n"
                    "price the core token only.");
            if (provider != OracleProvider::Off) {
                ImGui::SameLine(0, 6);
                if (neonButton("TEST", BtnKind::Subtle, {::ui::S(64.0f), 26}))
                    controller.testOracle(net, [c = &controller](std::string price,
                                                                 std::string error) {
                        if (error.empty())
                            c->toast(Toast::Success, "Oracle OK: " + price);
                        else
                            c->toast(Toast::Error, "Oracle: " + error);
                    });
            }

            const bool wantsUrl = provider == OracleProvider::Alcor ||
                                  provider == OracleProvider::CoinGecko;
            const bool wantsId = provider == OracleProvider::CoinGecko ||
                                 provider == OracleProvider::Delphi;
            if (wantsUrl || wantsId) {
                static std::map<std::string, std::array<char, 160>> urlBufs;
                static std::map<std::string, std::array<char, 32>> idBufs;
                auto& urlBuf = urlBufs[net.chainId];
                auto& idBuf = idBufs[net.chainId];
                if (!ImGui::IsAnyItemActive()) {
                    std::snprintf(urlBuf.data(), urlBuf.size(), "%s",
                                  net.oracle.url.c_str());
                    std::snprintf(idBuf.data(), idBuf.size(), "%s",
                                  net.oracle.coreId.c_str());
                }
                if (ImGui::BeginTable("##ocfg", 3, ImGuiTableFlags_SizingFixedFit |
                                                       ImGuiTableFlags_NoPadOuterX)) {
                    ImGui::TableSetupColumn("pad", ImGuiTableColumnFlags_WidthFixed,
                                            ::ui::S(78.0f));
                    ImGui::TableSetupColumn("main", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("aux", ImGuiTableColumnFlags_WidthFixed,
                                            wantsUrl && wantsId ? ::ui::S(120.0f) : 0.0f);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::PushFont(fonts().mono, kMonoSm);
                    if (wantsUrl)
                        ImGui::InputTextWithHint("##ourl", "https://oracle.example.com",
                                                 urlBuf.data(), urlBuf.size());
                    else
                        ImGui::InputTextWithHint("##oid", "delphi pair (e.g. waxpusd)",
                                                 idBuf.data(), idBuf.size());
                    ImGui::PopFont();
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        NetworkDef updated = net;
                        if (wantsUrl) {
                            std::string url = trim(urlBuf.data());
                            std::string why;
                            if (!url.empty() && !endpointAllowed(url, &why)) {
                                controller.toast(Toast::Error, why);
                            } else {
                                updated.oracle.url = url;
                                controller.addNetwork(updated);
                            }
                        } else {
                            updated.oracle.coreId = trim(idBuf.data());
                            controller.addNetwork(updated);
                        }
                    }
                    ImGui::TableNextColumn();
                    if (wantsUrl && wantsId) {
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::PushFont(fonts().mono, kMonoSm);
                        ImGui::InputTextWithHint("##ocoreid", "coingecko id",
                                                 idBuf.data(), idBuf.size());
                        ImGui::PopFont();
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            NetworkDef updated = net;
                            updated.oracle.coreId = trim(idBuf.data());
                            controller.addNetwork(updated);
                        }
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::PopID();
        }

        // Tracked tokens beyond the core symbol (registry fallback when no
        // light API is set). Drag to reorder; the order drives balance and
        // picker listings.
        for (size_t ti = 0; ti < net.tokens.size(); ++ti) {
            const TokenDef& token = net.tokens[ti];
            ImGui::PushID((token.contract + token.code).c_str());
            ImGui::Dummy({::ui::S(8.0f), 0});
            ImGui::SameLine();
            std::string tokenList = "##tok" + net.chainId;
            int droppedTok =
                dragGrip(tokenList.c_str(), static_cast<int>(ti), token.code.c_str());
            if (droppedTok >= 0 && droppedTok != static_cast<int>(ti) &&
                droppedTok < static_cast<int>(net.tokens.size())) {
                NetworkDef updated = net;
                TokenDef moved = updated.tokens[static_cast<size_t>(droppedTok)];
                updated.tokens.erase(updated.tokens.begin() + droppedTok);
                updated.tokens.insert(updated.tokens.begin() + static_cast<ptrdiff_t>(ti),
                                      moved);
                controller.addNetwork(updated);
            }
            ImGui::SameLine(0, 6);
            ImGui::AlignTextToFramePadding();
            monoText(token.code + "  (" + token.contract + ")", col::Steel, kMonoSm);
            ImGui::SameLine();
            if (iconButton("##rmtok", Icon::Trash, "Stop tracking", col::Slate, 12.0f))
                controller.removeToken(net.chainId, token);
            ImGui::PopID();
        }
        {
            static std::map<std::string, std::array<char, 32>> tokContractBufs, tokCodeBufs;
            auto& tokContract = tokContractBufs[net.chainId];
            auto& tokCode = tokCodeBufs[net.chainId];
            const bool phoneRow = layout().phone();
            ImGui::Dummy({::ui::S(8.0f), 0});
            ImGui::SameLine();
            ImGui::SetNextItemWidth(::ui::S(phoneRow ? 128.0f : 170.0f));
            ImGui::PushFont(fonts().mono, kMonoSm);
            ImGui::InputTextWithHint("##tokc", "token contract", tokContract.data(),
                                     tokContract.size());
            ImGui::SameLine(0, 6);
            ImGui::SetNextItemWidth(::ui::S(phoneRow ? 62.0f : 80.0f));
            ImGui::InputTextWithHint("##toks", "CODE", tokCode.data(), tokCode.size());
            ImGui::PopFont();
            ImGui::SameLine(0, 6);
            if (neonButton(phoneRow ? "TRACK" : "TRACK TOKEN", BtnKind::Subtle,
                           {::ui::S(phoneRow ? 70.0f : 110.0f), 28}) &&
                tokContract[0] && tokCode[0]) {
                controller.addToken(net.chainId,
                                    {toLower(trim(tokContract.data())),
                                     trim(tokCode.data())});
                tokContract[0] = 0;
                tokCode[0] = 0;
            }
        }
        ::ui::VSpace(1);
        ImGui::PopID();
    }

    // Add network from presets not yet present.
    ImGui::SetNextItemWidth(280);
    static int presetIdx = -1;
    auto presets = allPresets();
    std::vector<NetworkDef> addable;
    for (const auto& preset : presets) {
        bool present = false;
        for (const auto& existing : state.vault.networks)
            if (existing.chainId == preset.chainId) present = true;
        if (!present) addable.push_back(preset);
    }
    if (!addable.empty()) {
        if (ImGui::BeginCombo("##preset", presetIdx >= 0 &&
                                                  presetIdx < static_cast<int>(addable.size())
                                              ? addable[presetIdx].name.c_str()
                                              : "add a known network...")) {
            for (int i = 0; i < static_cast<int>(addable.size()); ++i)
                if (ImGui::Selectable(addable[i].name.c_str(), presetIdx == i)) presetIdx = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, 6);
        if (neonButton("ADD NETWORK", BtnKind::Ghost, {130, 34}) && presetIdx >= 0 &&
            presetIdx < static_cast<int>(addable.size())) {
            controller.addNetwork(addable[presetIdx]);
            presetIdx = -1;
        }
    }

    // Any-chain onboarding: probe an endpoint, verify its chain id, name it.
    vspace(8);
    sectionTitle("Add a custom chain");
    static char customUrl[160] = {};
    static char customName[48] = {};
    static char customSymbol[24] = "4,EOS";
    static std::string probedChainId, probeError;
    static bool probing = false;
    float avail = ImGui::GetContentRegionAvail().x;
    {
        FieldOpts opts;
        opts.mono = true;
        opts.placeholder = "https://api.mychain.example";
        opts.width = avail - 120;
        textField("##customurl", customUrl, sizeof customUrl, opts);
    }
    ImGui::SameLine(0, 6);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1);
    if (probing) {
        spinner(12.0f);
    } else if (neonButton("PROBE", BtnKind::Subtle, {100, 38}) && customUrl[0]) {
        std::string why;
        if (!endpointAllowed(trim(customUrl), &why)) {
            controller.toast(Toast::Error, why);
        } else {
            probing = true;
            probedChainId.clear();
            probeError.clear();
            controller.probeCustomChain(trim(customUrl), [](std::string chainId,
                                                            std::string error) {
                probing = false;
                probedChainId = std::move(chainId);
                probeError = std::move(error);
            });
        }
    }
    if (!probeError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextWrapped("%s", probeError.c_str());
        ImGui::PopStyleColor();
    }
    if (!probedChainId.empty()) {
        kvRow("Chain id", probedChainId, true, true);
        FieldOpts opts;
        opts.width = 200;
        opts.placeholder = "display name";
        textField("##customname", customName, sizeof customName, opts);
        ImGui::SameLine(0, 6);
        opts.mono = true;
        opts.width = 120;
        opts.placeholder = "4,SYM";
        textField("##customsym", customSymbol, sizeof customSymbol, opts);
        ImGui::SameLine(0, 6);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1);
        if (neonButton("SAVE CHAIN", BtnKind::Primary, {120, 38}) && customName[0]) {
            NetworkDef net;
            net.chainId = probedChainId;
            net.name = customName;
            net.rpc.nodes = {{trim(customUrl), "", 0, true}};
            net.coreSymbol = trim(customSymbol);
            controller.addNetwork(net);
            customUrl[0] = 0;
            customName[0] = 0;
            probedChainId.clear();
        }
    }
    endCard();
}

void drawLinkSessions(AppState& state, Controller& controller) {
    if (!beginCard("links")) {
        endCard();
        return;
    }
    sectionTitle("Dapp links (experimental)");
    subtext("Live anchor-link sessions: dapps push signing requests straight into this "
            "wallet over an encrypted channel. Connect by pasting a dapp's login "
            "request into Contracts > ESR > CONNECT AS LOGIN. Every pushed request "
            "still passes the guard and signing review.");
    if (state.vault.linkSessions.empty()) {
        subtext("No linked dapps.");
    }
    for (const auto& session : state.vault.linkSessions) {
        ImGui::PushID(session.id.c_str());
        statusDot(col::Success, true);
        ImGui::SameLine(0, 8);
        ImGui::PushFont(fonts().uiSemi, kText);
        ImGui::TextUnformatted(session.appName.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::PushFont(fonts().mono, kMonoSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
        ImGui::Text("%s@%s  -  linked %s ago%s", session.actor.c_str(),
                    session.permission.c_str(), formatAgo(session.createdAt).c_str(),
                    session.lastUsedAt
                        ? ("  -  used " + formatAgo(session.lastUsedAt) + " ago").c_str()
                        : "");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine();
        float endX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(endX - 30);
        if (iconButton("##unlink", Icon::Trash, "Unlink (the dapp can no longer push)",
                       col::Danger, 14.0f))
            controller.removeLinkSessionById(session.id);
        ImGui::PopID();
    }
    endCard();
}

void drawSecurity(AppState& state, Controller& controller) {
    if (!beginCard("security")) {
        endCard();
        return;
    }
    sectionTitle("Security policy");
    SecurityPrefs prefs = state.vault.security;
    bool changed = false;

    ImGui::PushFont(fonts().uiMedium, kText);
    ImGui::TextUnformatted("Auto-lock after inactivity");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(300);
    int minutes = prefs.autoLockMinutes;
    if (ImGui::SliderInt("##autolock", &minutes, 0, 120,
                         minutes == 0 ? "never" : "%d minutes")) {
        prefs.autoLockMinutes = minutes;
        changed = true;
    }
    vspace(6);

    changed |= toggle("Require password for every signature", &prefs.requirePasswordPerSign,
                      "Even whitelisted transactions re-ask for the vault password");
    vspace(4);
    changed |= toggle("Allow auto-sign rules", &prefs.allowAutoSign,
                      "Master switch. Auto-sign additionally requires a pinned rule, live "
                      "hash verification, and zero critical risk flags");
    vspace(4);
    changed |= toggle("Hold-to-sign on critical risk", &prefs.blockOnCriticalRisk,
                      "Permission changes, code deploys and near-full transfers demand a "
                      "long press instead of a click");
    vspace(4);
    changed |= toggle("Resource provider cosigning", &prefs.useResourceProvider,
                      "Lets a provider (Greymass Fuel and friends) cover CPU/NET for "
                      "your transactions; any fee it quotes appears as a prompt you can "
                      "decline");
    vspace(4);
    changed |= toggle("Lock when the app goes to background", &prefs.lockOnBackground,
                      "On phones and tablets the OS can suspend the app for days; this "
                      "seals the vault the moment it leaves the foreground");
    vspace(4);
    changed |= toggle("Autopilot standby (run schedules while locked)", &prefs.autopilotStandby,
                      "TRADE-OFF: locking then hides the UI but keeps the decrypted vault "
                      "in this process's memory so schedules keep signing - malware that "
                      "can read process memory could reach the keys during standby. Off = "
                      "locking always wipes keys from memory. Panic lock (Ctrl+Shift+L) "
                      "and quitting always wipe, either way");
    vspace(6);

    ImGui::PushFont(fonts().uiMedium, kText);
    ImGui::TextUnformatted("Clear clipboard after copying secrets");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(300);
    int clipboard = prefs.clipboardClearSec;
    if (ImGui::SliderInt("##clip", &clipboard, 0, 120,
                         clipboard == 0 ? "never" : "%d seconds")) {
        prefs.clipboardClearSec = clipboard;
        changed = true;
    }
    if (changed) controller.updateSecurity(prefs);
    vspace(10);

    sectionTitle("Vault password");
    static char currentPw[256] = {}, newPw[256] = {}, confirmPw[256] = {};
    static std::string pwError, pwOk;
    static bool busy = false;
    float third = (ImGui::GetContentRegionAvail().x - 20) / 3.0f;
    ImGui::BeginGroup();
    FieldOpts opts;
    opts.password = true;
    opts.width = third;
    opts.placeholder = "current";
    textField("##cur", currentPw, sizeof currentPw, opts);
    ImGui::EndGroup();
    ImGui::SameLine(0, 10);
    ImGui::BeginGroup();
    opts.placeholder = "new (8+ chars)";
    textField("##new", newPw, sizeof newPw, opts);
    ImGui::EndGroup();
    ImGui::SameLine(0, 10);
    ImGui::BeginGroup();
    opts.placeholder = "repeat new";
    textField("##rep", confirmPw, sizeof confirmPw, opts);
    ImGui::EndGroup();
    if (!pwError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextUnformatted(pwError.c_str());
        ImGui::PopStyleColor();
    }
    if (!pwOk.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Success));
        ImGui::TextUnformatted(pwOk.c_str());
        ImGui::PopStyleColor();
    }
    if (busy) {
        spinner(12.0f);
    } else if (neonButton("CHANGE PASSWORD", BtnKind::Ghost, {180, 36})) {
        pwError.clear();
        pwOk.clear();
        if (std::strcmp(newPw, confirmPw) != 0) {
            pwError = "new passwords do not match";
        } else if (std::strlen(newPw) < 8) {
            pwError = "new password must be at least 8 characters";
        } else {
            busy = true;
            controller.changePassword(currentPw, newPw, [](bool ok, std::string message) {
                busy = false;
                if (ok) {
                    pwOk = "password changed and vault re-encrypted";
                } else {
                    pwError = message;
                }
            });
            secureWipe(currentPw, sizeof currentPw);
            secureWipe(newPw, sizeof newPw);
            secureWipe(confirmPw, sizeof confirmPw);
        }
    }
    endCard();
}

void drawAppearance(AppState& state, Controller& controller) {
    (void)state;
    (void)controller;
    if (!beginCard("appearance")) {
        endCard();
        return;
    }
    sectionTitle("Appearance");
    Cosmetics& cosmetic = cosmetics();
    bool changed = false;
    ImGui::PushFont(fonts().uiMedium, kText);
    ImGui::TextUnformatted("Glow intensity");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(300);
    changed |= ImGui::SliderFloat("##glow", &cosmetic.glow, 0.0f, 1.0f, "%.2f");
    vspace(4);
    changed |= toggle("Reduce motion", &cosmetic.reduceMotion,
                      "Stops the drifting grid, scanlines and pulsing accents");
    if (changed) saveCosmetics();
    endCard();
}

void drawPortability(AppState& state, Controller& controller) {
    (void)state;
    if (!beginCard("portability")) {
        endCard();
        return;
    }
    sectionTitle("Vault file");
    subtext("The vault travels as one encrypted .tbx file. Export copies it sealed "
            "(the password stays with you); import installs a .tbx from another "
            "machine and keeps the current vault as a backup. You can also drop a "
            ".tbx file anywhere on this window.");
    vspace(4);

    static char exportPath[512] = {};
    if (!exportPath[0]) {
        auto def = (dataDir().parent_path() / "tacklebox-vault-export.tbx").string();
        std::snprintf(exportPath, sizeof exportPath, "%s", def.c_str());
    }
    float avail = ImGui::GetContentRegionAvail().x;
    {
        FieldOpts opts;
        opts.mono = true;
        opts.width = avail - 130;
        textField("Export to", exportPath, sizeof exportPath, opts);
    }
    ImGui::SameLine(0, 8);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 22);
    if (neonButton("EXPORT", BtnKind::Ghost, {110, 38}) && exportPath[0])
        controller.exportVault(trim(exportPath));

    static char importPath[512] = {};
    {
        FieldOpts opts;
        opts.mono = true;
        opts.placeholder = "path to a .tbx file";
        opts.width = avail - 130;
        textField("Import from", importPath, sizeof importPath, opts);
    }
    ImGui::SameLine(0, 8);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 22);
    if (neonButton("IMPORT", BtnKind::Danger, {110, 38}) && importPath[0])
        ImGui::OpenPopup("##confirmimport");
    if (ImGui::BeginPopup("##confirmimport")) {
        ImGui::TextWrapped("Replace the current vault with the imported file?\n"
                           "The wallet locks and the current vault is kept as a backup.");
        static float holdImport = 0.0f;
        if (holdButton("HOLD TO IMPORT", 1.2f, &holdImport, {200, 34})) {
            controller.importVault(trim(importPath));
            importPath[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    endCard();
}

void drawAbout(AppState& state, Controller& controller) {
    if (!beginCard("about")) {
        endCard();
        return;
    }
    sectionTitle("About");
    kvRow("Version", "TackleBox " TB_VERSION);
    kvRow("Engine", "dwarfkit (native Wharfkit port) + Dear ImGui");
    kvRow("Data directory", dataDir().string(), true, true);
    subtext("TackleBox talks only to the endpoints you configure and, when enabled, "
            "GitHub's release feed for update notices. No telemetry.");
    ::ui::VSpace(0.4f);

    // Updates: notify-only. The wallet never downloads or installs code.
    const auto& update = state.update;
    if (update.checking) {
        spinner(11.0f);
        ImGui::SameLine(0, 8);
        subtext("Checking GitHub releases...");
    } else {
        if (neonButton("CHECK FOR UPDATES", BtnKind::Subtle, {::ui::S(160.0f), 30}))
            controller.checkForUpdates(true);
        if (update.available) {
            ImGui::SameLine(0, 10);
            badgeFilled(("NEW: " + update.latestTag).c_str(), col::Success);
            ImGui::SameLine(0, 8);
            if (neonButton("VIEW RELEASE", BtnKind::Primary, {::ui::S(130.0f), 30}))
                SDL_OpenURL(update.releaseUrl.c_str());
            if (!update.notes.empty()) {
                ImGui::PushFont(fonts().ui, kTextSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
                ImGui::TextWrapped("%s", update.notes.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            subtext("Download from the release page and verify it yourself - the "
                    "wallet never installs updates on its own.");
        } else if (update.checkedAt && update.error.empty()) {
            ImGui::SameLine(0, 10);
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
            ImGui::Text("up to date  -  checked %s ago", formatAgo(update.checkedAt).c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        } else if (!update.error.empty()) {
            ImGui::SameLine(0, 10);
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(fonts().ui, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
            ImGui::TextUnformatted(update.error.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
    }
    endCard();
}

}  // namespace

// Startup registration + the switchboard for every preemptive API call, so
// idle network/CPU cost is entirely the user's choice.
void drawStartupBackground(AppState& state, Controller& controller) {
    if (!beginCard("startup")) {
        endCard();
        return;
    }
    sectionTitle("Startup & background activity");
    if (autostartSupported()) {
        bool open = autostartEnabled();
        if (toggle("Open TackleBox at login", &open,
                   "Registers this executable with the OS (Windows Run key / "
                   "launch agent / autostart entry). The vault still opens locked")) {
            std::string error;
            if (!setAutostart(open, &error))
                controller.toast(Toast::Error, "Autostart: " + error);
            else
                controller.toast(Toast::Success,
                                 open ? "TackleBox will open at login"
                                      : "Autostart removed");
        }
        vspace(6);
    }
    subtext("Each background fetch class can be switched off; manual refresh "
            "buttons always work regardless.");
    SecurityPrefs prefs = state.vault.security;
    bool changed = false;
    changed |= toggle("Auto-refresh account data & balances", &prefs.bgAccountRefresh,
                      "Refetches the active account and its tokens when the data "
                      "goes stale");
    vspace(4);
    changed |= toggle("Auto-refresh pinned queries", &prefs.bgPinnedRefresh,
                      "Runs each pinned dashboard query on its own timer");
    vspace(4);
    changed |= toggle("Auto-refresh token prices", &prefs.bgPriceRefresh,
                      "Refetches oracle prices once a minute while the dashboard "
                      "is open (the first fill still happens)");
    vspace(4);
    changed |= toggle("Check for updates after unlock", &prefs.bgUpdateCheck,
                      "One query to GitHub's release feed per session. Notify-only: "
                      "the wallet never downloads or installs code by itself");
    if (changed) controller.updateSecurity(prefs);
    endCard();
}

void drawSettings(AppState& state, Controller& controller) {
    heading("Settings");
    vspace(8);
    drawNetworks(state, controller);
    vspace(12);
    drawSecurity(state, controller);
    vspace(12);
    drawStartupBackground(state, controller);
    vspace(12);
    drawLinkSessions(state, controller);
    vspace(12);
    drawPortability(state, controller);
    vspace(12);
    drawAppearance(state, controller);
    vspace(12);
    drawAbout(state, controller);
}

}  // namespace tb::ui
