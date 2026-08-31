// Block explorer: chain overview with live head/LIB, block and transaction
// detail, and account inspection for any name on the chain.
#include <algorithm>
#include <cstring>

#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

// Actions inside get_block transactions arrive packed or expanded; summarize
// the expanded ones.
std::string summarizeTx(const json& tx) {
    if (!tx.is_object()) return "packed transaction";
    const json* trx = tx.contains("trx") ? &tx["trx"] : nullptr;
    if (!trx || !trx->is_object()) return "packed transaction";
    if (trx->contains("transaction") && (*trx)["transaction"].contains("actions")) {
        const json& actions = (*trx)["transaction"]["actions"];
        if (actions.is_array() && !actions.empty()) {
            std::string first = actions[0].value("account", "") +
                                "::" + actions[0].value("name", "");
            if (actions.size() > 1)
                first += " (+" + std::to_string(actions.size() - 1) + ")";
            return first;
        }
    }
    return "transaction";
}

std::string txIdOf(const json& tx) {
    if (tx.contains("trx")) {
        if (tx["trx"].is_object()) return tx["trx"].value("id", "");
        if (tx["trx"].is_string()) return tx["trx"].get<std::string>();
    }
    return {};
}

void drawSearchBar(AppState& state, Controller& controller) {
    static char query[128] = {};
    float avail = ImGui::GetContentRegionAvail().x;
    FieldOpts opts;
    opts.mono = true;
    opts.placeholder = "account, transaction id, or block number";
    opts.width = avail - 130.0f;
    bool entered = textField("##explsearch", query, sizeof query, opts);
    ImGui::SameLine(0, 8);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1);
    if ((neonButton("SEARCH", BtnKind::Primary, {110, 38}) || entered) && query[0]) {
        controller.exploreSearch(query);
        controller.noteActivity();
    }
    ImGui::SameLine(0, 8);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1);
    ExploreViewState& ex = state.explore;
    if (ex.mode != ExploreViewState::Mode::Overview &&
        neonButton("OVERVIEW", BtnKind::Subtle, {100, 38}))
        ex.mode = ExploreViewState::Mode::Overview;
}

void drawOverview(AppState& state, Controller& controller) {
    ExploreViewState& ex = state.explore;
    controller.exploreOverview(false);  // keep the strip fresh while visible

    if (!ex.overviewError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextWrapped("%s", ex.overviewError.c_str());
        ImGui::PopStyleColor();
    }
    if (ex.info.is_null()) {
        if (ex.loadingOverview) spinner(14.0f);
        return;
    }

    // Stat tiles (stacked on phones).
    bool narrow = layout().phone();
    float tileW = narrow ? 0.0f : (ImGui::GetContentRegionAvail().x - 24.0f) / 3.0f;
    auto tile = [&](const char* id, const char* label, const std::string& value,
                    const std::string& sub) {
        if (beginCard(id, tileW)) {
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::PushFont(fonts().mono, 26.0f);
            ImGui::TextUnformatted(value.c_str());
            ImGui::PopFont();
            if (!sub.empty()) {
                ImGui::PushFont(fonts().ui, kMonoSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                ImGui::TextUnformatted(sub.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
        }
        endCard();
    };

    uint32_t head = ex.info.value("head_block_num", 0u);
    uint32_t lib = ex.info.value("last_irreversible_block_num", 0u);
    tile("t1", "HEAD BLOCK", std::to_string(head),
         "producer " + ex.info.value("head_block_producer", std::string("-")));
    if (!narrow) ImGui::SameLine(0, 12);
    tile("t2", "IRREVERSIBLE", std::to_string(lib),
         head >= lib ? std::to_string(head - lib) + " blocks behind head" : "");
    if (!narrow) ImGui::SameLine(0, 12);
    std::string cpuLimit;
    if (ex.info.contains("block_cpu_limit"))
        cpuLimit = std::to_string(ex.info["block_cpu_limit"].get<int64_t>() / 1000) + " ms/block";
    tile("t3", "CHAIN CPU BUDGET", cpuLimit.empty() ? "-" : cpuLimit,
         "server " + ex.info.value("server_version_string", std::string("-")));

    vspace(12);
    if (beginCard("recentblocks")) {
        sectionTitle("Recent blocks");
        if (ex.recentBlocks.empty() && ex.loadingOverview) spinner(12.0f);
        float rowW = ImGui::GetContentRegionAvail().x;
        for (const auto& block : ex.recentBlocks) {
            uint32_t num = block.value("block_num", 0u);
            ImGui::PushID(static_cast<int>(num));
            if (ImGui::Selectable("##blk", false, 0, {0, 30}))
                controller.exploreBlock(std::to_string(num));
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(fonts().mono, kMono, {rmin.x + 4, rmin.y + 6}, col::Cyan,
                        std::to_string(num).c_str());
            dl->AddText(fonts().mono, kMonoSm, {rmin.x + rowW * 0.30f, rmin.y + 8},
                        col::Steel, block.value("producer", std::string()).c_str());
            std::string txs = std::to_string(block.value("tx_count", size_t(0))) + " tx";
            dl->AddText(fonts().mono, kMonoSm, {rmin.x + rowW * 0.62f, rmin.y + 8}, col::Ice,
                        txs.c_str());
            if (!narrow) {
                std::string ts = block.value("timestamp", std::string());
                if (auto t = ts.find('T'); t != std::string::npos) ts = ts.substr(t + 1);
                dl->AddText(fonts().mono, kMonoSm, {rmin.x + rowW * 0.76f, rmin.y + 8},
                            col::Slate, ts.c_str());
            }
            ImGui::PopID();
        }
    }
    endCard();
}

void drawAccountView(AppState& state, Controller& controller) {
    ExploreViewState& ex = state.explore;
    ImGui::PushFont(fonts().uiBold, 24.0f);
    ImGui::TextUnformatted(ex.accountName.c_str());
    ImGui::PopFont();
    if (ex.loadingAccount) {
        spinner(13.0f);
        return;
    }
    if (!ex.accountError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextWrapped("%s", ex.accountError.c_str());
        ImGui::PopStyleColor();
        return;
    }
    if (ex.account.is_null()) return;
    const json& account = ex.account;

    if (beginCard("acctsum")) {
        sectionTitle("Account");
        kvRow("Created", account.value("created", std::string("-")), true);
        if (account.contains("core_liquid_balance") &&
            account["core_liquid_balance"].is_string())
            kvRow("Liquid", account["core_liquid_balance"].get<std::string>(), true);
        int64_t ram = account.value("ram_quota", int64_t(0));
        int64_t ramUsed = account.value("ram_usage", int64_t(0));
        kvRow("RAM", std::to_string(ramUsed) + " / " + std::to_string(ram) + " bytes", true);
        bool isContract = false;
        if (account.contains("last_code_update") &&
            account.value("last_code_update", std::string()) != "1970-01-01T00:00:00.000")
            isContract = true;
        if (isContract) {
            ImGui::SameLine();
            badge("CONTRACT", col::Violet);
            if (neonButton("OPEN IN CONTRACTS", BtnKind::Ghost, {170, 32})) {
                controller.loadContract(ex.accountName);
                state.page = Page::Contracts;
            }
        }
    }
    endCard();
    vspace(10);

    if (beginCard("acctperms")) {
        sectionTitle("Permissions");
        if (account.contains("permissions") && account["permissions"].is_array()) {
            for (const auto& perm : account["permissions"]) {
                ImGui::PushFont(fonts().uiSemi, kText);
                ImGui::TextUnformatted(perm.value("perm_name", std::string("?")).c_str());
                ImGui::PopFont();
                ImGui::SameLine();
                ImGui::PushFont(fonts().mono, kMonoSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                ImGui::Text("parent: %s  threshold: %d",
                            perm.value("parent", std::string("-")).c_str(),
                            perm.contains("required_auth")
                                ? perm["required_auth"].value("threshold", 0)
                                : 0);
                ImGui::PopStyleColor();
                ImGui::PopFont();
                if (perm.contains("required_auth")) {
                    for (const auto& key : perm["required_auth"].value("keys", json::array())) {
                        ImGui::Indent(18);
                        monoText("+" + std::to_string(key.value("weight", 0)) + "  " +
                                     key.value("key", std::string()),
                                 col::Steel, kMonoSm);
                        ImGui::Unindent(18);
                    }
                    for (const auto& acct :
                         perm["required_auth"].value("accounts", json::array())) {
                        ImGui::Indent(18);
                        std::string who;
                        if (acct.contains("permission"))
                            who = acct["permission"].value("actor", std::string()) + "@" +
                                  acct["permission"].value("permission", std::string());
                        monoText("+" + std::to_string(acct.value("weight", 0)) + "  " + who,
                                 col::CyanDim, kMonoSm);
                        ImGui::Unindent(18);
                    }
                }
            }
        }
    }
    endCard();
    vspace(10);

    if (beginCard("acctactions")) {
        sectionTitle("Recent actions");
        if (!ex.actionsError.empty()) {
            subtext(ex.actionsError.c_str());
        } else if (ex.actions.contains("actions") && ex.actions["actions"].is_array()) {
            // Rows come from hyperion v2 (act/trx_id/@timestamp at the top,
            // newest first) or v1 history (action_trace wrapper, oldest
            // first); normalize both and show newest first.
            struct Row {
                std::string summary, txid, when;
            };
            std::vector<Row> rows;
            for (const json& wrap : ex.actions["actions"]) {
                const json* trace = wrap.contains("action_trace") ? &wrap["action_trace"]
                                                                  : &wrap;
                Row row;
                if (trace->contains("act"))
                    row.summary = (*trace)["act"].value("account", "") +
                                  "::" + (*trace)["act"].value("name", "");
                row.txid = trace->value("trx_id", "");
                row.when = wrap.value("block_time", trace->value("block_time", ""));
                if (row.when.empty()) row.when = wrap.value("timestamp", "");
                if (row.when.empty()) row.when = wrap.value("@timestamp", "");
                rows.push_back(std::move(row));
            }
            std::stable_sort(rows.begin(), rows.end(),
                             [](const Row& a, const Row& b) { return a.when > b.when; });
            if (rows.size() > 25) rows.resize(25);
            int shown = 0;
            for (const Row& r : rows) {
                const std::string& summary = r.summary;
                const std::string& txid = r.txid;
                std::string when = r.when;
                ++shown;
                ImGui::PushID(shown);
                if (ImGui::Selectable("##act", false, 0, {0, 24}) && !txid.empty())
                    controller.exploreTransaction(txid);
                ImVec2 rmin = ImGui::GetItemRectMin();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                float rowW = ImGui::GetItemRectMax().x - rmin.x;
                dl->AddText(fonts().mono, kMonoSm, {rmin.x + 4, rmin.y + 4}, col::Ice,
                            summary.c_str());
                if (auto t = when.find('T'); t != std::string::npos && layout().phone())
                    when = when.substr(t + 1);
                dl->AddText(fonts().mono, kMonoSm, {rmin.x + rowW * 0.64f, rmin.y + 4},
                            col::Slate, when.c_str());
                ImGui::PopID();
            }
            if (rows.empty()) subtext("No recorded actions.");
        } else if (ex.actions.is_null()) {
            subtext("History not loaded.");
        }
    }
    endCard();
}

void drawBlockView(AppState& state, Controller& controller) {
    ExploreViewState& ex = state.explore;
    if (ex.loadingBlock) {
        spinner(13.0f);
        return;
    }
    if (!ex.blockError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextWrapped("%s", ex.blockError.c_str());
        ImGui::PopStyleColor();
        return;
    }
    if (ex.block.is_null()) return;
    const json& block = ex.block;

    ImGui::PushFont(fonts().uiBold, 24.0f);
    ImGui::Text("Block %u", block.value("block_num", 0u));
    ImGui::PopFont();
    if (beginCard("blocksum")) {
        kvRow("Id", block.value("id", std::string("-")), true, true);
        kvRow("Producer", block.value("producer", std::string("-")), true);
        kvRow("Timestamp", block.value("timestamp", std::string("-")), true);
        kvRow("Previous", block.value("previous", std::string("-")), true, true);
        size_t txCount =
            block.contains("transactions") ? block["transactions"].size() : 0;
        kvRow("Transactions", std::to_string(txCount));
        // Neighbor navigation.
        uint32_t num = block.value("block_num", 0u);
        if (num > 1 && neonButton("< PREV", BtnKind::Subtle, {80, 30}))
            controller.exploreBlock(std::to_string(num - 1));
        ImGui::SameLine(0, 6);
        if (neonButton("NEXT >", BtnKind::Subtle, {80, 30}))
            controller.exploreBlock(std::to_string(num + 1));
    }
    endCard();
    vspace(10);

    if (block.contains("transactions") && !block["transactions"].empty()) {
        if (beginCard("blocktxs")) {
            sectionTitle("Transactions");
            int index = 0;
            for (const auto& tx : block["transactions"]) {
                std::string txid = txIdOf(tx);
                ImGui::PushID(index++);
                if (ImGui::Selectable("##tx", false, 0, {0, 26}) && !txid.empty())
                    controller.exploreTransaction(txid);
                ImVec2 rmin = ImGui::GetItemRectMin();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddText(fonts().mono, kMonoSm, {rmin.x + 4, rmin.y + 5}, col::Cyan,
                            txid.empty() ? "(packed)" : middleEllipsis(txid, 16, 8).c_str());
                dl->AddText(fonts().mono, kMonoSm, {rmin.x + 260, rmin.y + 5}, col::Ice,
                            summarizeTx(tx).c_str());
                std::string status = tx.value("status", std::string(""));
                dl->AddText(fonts().mono, kMonoSm, {rmin.x + 560, rmin.y + 5},
                            status == "executed" ? col::Success : col::Warn, status.c_str());
                ImGui::PopID();
            }
        }
        endCard();
    }
}

void drawTxView(AppState& state, Controller& controller) {
    (void)controller;
    ExploreViewState& ex = state.explore;
    ImGui::PushFont(fonts().uiBold, 22.0f);
    ImGui::TextUnformatted("Transaction");
    ImGui::PopFont();
    monoText(ex.txId, col::Cyan, kMono);
    ImGui::SameLine();
    if (iconButton("##cptx", Icon::Copy, "Copy id", col::Slate, 13.0f))
        ImGui::SetClipboardText(ex.txId.c_str());
    if (ex.loadingTx) {
        spinner(13.0f);
        return;
    }
    if (!ex.txError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Warn));
        ImGui::TextWrapped("%s", ex.txError.c_str());
        ImGui::PopStyleColor();
    }

    if (!ex.txStatus.is_null() && ex.txStatus.is_object()) {
        if (beginCard("txstatus")) {
            sectionTitle("Status");
            std::string st = ex.txStatus.value("state", std::string("UNKNOWN"));
            ImU32 c = st == "IRREVERSIBLE" ? col::Success
                      : st == "IN_BLOCK"   ? col::Cyan
                      : st == "FORKED_OUT" ? col::Danger
                                           : col::Steel;
            badgeFilled(st.c_str(), c);
            uint32_t blockNum = ex.txStatus.value("block_number", 0u);
            if (blockNum) {
                kvRow("Block", std::to_string(blockNum), true);
                if (neonButton("VIEW BLOCK", BtnKind::Subtle, {110, 30}))
                    controller.exploreBlock(std::to_string(blockNum));
            }
            if (ex.txStatus.contains("block_timestamp"))
                kvRow("Timestamp", ex.txStatus.value("block_timestamp", std::string()), true);
        }
        endCard();
        vspace(10);
    }

    const json* detail = nullptr;
    if (!ex.txDetail.is_null()) detail = &ex.txDetail;
    else if (!ex.txFromBlock.is_null()) detail = &ex.txFromBlock;
    if (detail) {
        if (beginCard("txdetail")) {
            sectionTitle("Decoded transaction");
            jsonTree(*detail, "txjson");
        }
        endCard();
    }
}

}  // namespace

void drawExplore(AppState& state, Controller& controller) {
    heading("Explore");
    subtext("The chain, inside the wallet: blocks, transactions and accounts straight "
            "from your configured endpoint. No third-party explorer needed.");
    vspace(8);
    drawSearchBar(state, controller);
    vspace(8);

    switch (state.explore.mode) {
        case ExploreViewState::Mode::Overview: drawOverview(state, controller); break;
        case ExploreViewState::Mode::Account: drawAccountView(state, controller); break;
        case ExploreViewState::Mode::Block: drawBlockView(state, controller); break;
        case ExploreViewState::Mode::Transaction: drawTxView(state, controller); break;
    }
}

}  // namespace tb::ui
