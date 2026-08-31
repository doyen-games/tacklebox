// NFT gallery: the active account's Atomic Assets, served by the network's
// configured AA API node, media rendered through the texture cache.
#include <cstring>

#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/texcache.hpp"
#include "ui/widgets.hpp"

namespace tb::ui {

namespace {

// IPFS hashes and bare paths become gateway URLs; full URLs pass through.
std::string mediaUrl(const std::string& raw) {
    std::string value = trim(raw);
    if (value.empty()) return {};
    if (startsWith(value, "https://")) return value;
    if (startsWith(value, "http://")) return {};  // never fetch plaintext
    if (startsWith(value, "ipfs://")) value = value.substr(7);
    while (startsWith(value, "/")) value = value.substr(1);
    if (startsWith(value, "ipfs/")) value = value.substr(5);
    return "https://ipfs.io/ipfs/" + value;
}

std::string assetImage(const json& asset) {
    // data.img is the AA convention; fall back to template immutable data.
    if (asset.contains("data") && asset["data"].is_object()) {
        if (asset["data"].contains("img") && asset["data"]["img"].is_string())
            return asset["data"]["img"].get<std::string>();
        if (asset["data"].contains("image") && asset["data"]["image"].is_string())
            return asset["data"]["image"].get<std::string>();
    }
    if (asset.contains("template") && asset["template"].is_object() &&
        asset["template"].contains("immutable_data") &&
        asset["template"]["immutable_data"].is_object() &&
        asset["template"]["immutable_data"].contains("img") &&
        asset["template"]["immutable_data"]["img"].is_string())
        return asset["template"]["immutable_data"]["img"].get<std::string>();
    return {};
}

void drawAssetCard(AppState& state, Controller& controller, const json& asset, float cardW) {
    std::string assetId = asset.value("asset_id", std::string("?"));
    ImGui::PushID(assetId.c_str());
    ImGui::BeginGroup();

    ImVec2 topLeft = ImGui::GetCursorScreenPos();
    float imgH = cardW;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(topLeft, {topLeft.x + cardW, topLeft.y + imgH}, col::rgba(0x060D18), 5.0f);

    std::string media = mediaUrl(assetImage(asset));
    bool drewImage = false;
    if (!media.empty()) {
        if (auto svc = controller.currentService()) {
            const TexEntry* tex = textures().get(media, *svc, controller.runner());
            if (tex->state == TexEntry::State::Ready && tex->glId) {
                // Cover-fit into the square without distortion.
                float aspect = static_cast<float>(tex->width) / static_cast<float>(tex->height);
                ImVec2 uv0{0, 0}, uv1{1, 1};
                if (aspect > 1.0f) {
                    float crop = (1.0f - 1.0f / aspect) * 0.5f;
                    uv0.x = crop;
                    uv1.x = 1.0f - crop;
                } else if (aspect < 1.0f) {
                    float crop = (1.0f - aspect) * 0.5f;
                    uv0.y = crop;
                    uv1.y = 1.0f - crop;
                }
                dl->AddImageRounded(static_cast<ImTextureID>(tex->glId), topLeft,
                                    {topLeft.x + cardW, topLeft.y + imgH}, uv0, uv1,
                                    IM_COL32_WHITE, 5.0f);
                drewImage = true;
            } else if (tex->state == TexEntry::State::Loading) {
                ImVec2 center{topLeft.x + cardW * 0.5f, topLeft.y + imgH * 0.5f};
                float t = static_cast<float>(ImGui::GetTime()) * 6.0f;
                dl->PathArcTo(center, 12.0f, t, t + 4.4f, 24);
                dl->PathStroke(col::CyanDim, 2.0f);
            }
        }
    }
    if (!drewImage) {
        // Placeholder glyph for missing/failed media.
        drawIcon(dl, Icon::Grid, {topLeft.x + cardW * 0.5f, topLeft.y + imgH * 0.5f}, 34.0f,
                 col::alpha(col::Slate, 0.7f), 1.6f);
    }
    dl->AddRect(topLeft, {topLeft.x + cardW, topLeft.y + imgH},
                ImGui::IsMouseHoveringRect(topLeft, {topLeft.x + cardW, topLeft.y + imgH})
                    ? col::alpha(col::Cyan, 0.6f)
                    : col::Hairline,
                5.0f, 1.0f);

    ImGui::Dummy({cardW, imgH});
    ImGui::PushFont(fonts().uiSemi, kText);
    std::string name = asset.value("name", std::string());
    if (name.empty() && asset.contains("data") && asset["data"].is_object())
        name = asset["data"].value("name", std::string("(unnamed)"));
    if (name.size() > 24) name = name.substr(0, 21) + "...";
    ImGui::TextUnformatted(name.c_str());
    ImGui::PopFont();
    ImGui::PushFont(fonts().mono, kMonoSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
    std::string collection;
    if (asset.contains("collection") && asset["collection"].is_object())
        collection = asset["collection"].value("collection_name", std::string());
    ImGui::Text("%s  #%s", collection.c_str(),
                asset.value("template_mint", std::string("?")).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::EndGroup();
    if (ImGui::IsItemClicked()) state.assets.selected = asset;
    ImGui::PopID();
}

}  // namespace

void drawAssets(AppState& state, Controller& controller) {
    const AccountRef* account = state.currentAccount();
    const NetworkDef* network = state.currentNetwork();
    heading("Assets");
    subtext("Atomic Assets NFTs owned by the active account, served by the network's "
            "Atomic API node (configurable in Settings).");
    vspace(8);

    if (!account) {
        emptyState(Icon::Grid, "No account selected", "Pick an account to see its assets.");
        return;
    }
    if (!network || network->atomic.empty()) {
        emptyState(Icon::Grid, "No Atomic API configured",
                   "Set an Atomic Assets endpoint for this network in Settings.");
        return;
    }

    controller.loadAssets(account->actor, false);
    AssetsViewState& av = state.assets;

    if (neonButton("REFRESH", BtnKind::Subtle, {100, 30})) controller.loadAssets(account->actor, true);
    ImGui::SameLine();
    if (av.loading) spinner(11.0f);
    vspace(6);

    if (!av.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextWrapped("%s", av.error.c_str());
        ImGui::PopStyleColor();
        return;
    }
    const json* data = nullptr;
    if (av.assets.contains("data") && av.assets["data"].is_array()) data = &av.assets["data"];
    if (!data || data->empty()) {
        if (!av.loading)
            emptyState(Icon::Grid, "No assets", "This account holds no Atomic Assets here.");
        return;
    }

    // Responsive grid.
    float avail = ImGui::GetContentRegionAvail().x;
    int columns = static_cast<int>(avail / 190.0f);
    if (columns < 2) columns = 2;
    float cardW = (avail - static_cast<float>(columns - 1) * 14.0f) / static_cast<float>(columns);
    int i = 0;
    for (const auto& asset : *data) {
        if (i % columns != 0) ImGui::SameLine(0, 14);
        drawAssetCard(state, controller, asset, cardW);
        ++i;
    }

    // Detail side panel as a modal (a full sheet on phones).
    if (!av.selected.is_null()) {
        ImGui::OpenPopup("##assetdetail");
        if (beginAdaptiveModal("##assetdetail", 560.0f)) {
            const json& asset = av.selected;
            std::string name = asset.value("name", std::string("(unnamed)"));
            heading(name.c_str(), 22.0f);
            kvRow("Asset id", asset.value("asset_id", std::string("?")), true, true);
            if (asset.contains("collection") && asset["collection"].is_object())
                kvRow("Collection",
                      asset["collection"].value("collection_name", std::string()), true);
            if (asset.contains("schema") && asset["schema"].is_object())
                kvRow("Schema", asset["schema"].value("schema_name", std::string()), true);
            kvRow("Mint", asset.value("template_mint", std::string("?")), true);
            kvRow("Owner", asset.value("owner", std::string()), true);
            if (asset.contains("data") && asset["data"].is_object()) {
                vspace(6);
                sectionTitle("Immutable data");
                jsonTree(asset["data"], "assetdata");
            }
            vspace(10);

            // Actions: transfer + burn, only for assets this account owns.
            const AccountRef* account = state.currentAccount();
            bool owned = account && asset.value("owner", std::string()) == account->actor;
            std::string assetId = asset.value("asset_id", std::string());
            if (owned && !assetId.empty()) {
                sectionTitle("Send");
                static char nftTo[16] = {};
                static char nftMemo[128] = {};
                float avail = ImGui::GetContentRegionAvail().x;
                FieldOpts opts;
                opts.mono = true;
                opts.width = (avail - 130) * 0.4f;
                ImGui::BeginGroup();
                opts.placeholder = "recipient";
                textField("##nftto", nftTo, sizeof nftTo, opts);
                ImGui::EndGroup();
                ImGui::SameLine(0, 8);
                ImGui::BeginGroup();
                opts.width = (avail - 130) * 0.6f;
                opts.placeholder = "memo";
                textField("##nftmemo", nftMemo, sizeof nftMemo, opts);
                ImGui::EndGroup();
                ImGui::SameLine(0, 8);
                if (neonButton("TRANSFER", BtnKind::Primary, {110, 38}) && nftTo[0]) {
                    controller.transferAsset(assetId, toLower(trim(nftTo)), nftMemo);
                    av.selected = json();
                    ImGui::CloseCurrentPopup();
                }
                static float burnHold = 0.0f;
                if (holdButton("HOLD TO BURN (IRREVERSIBLE)", 1.6f, &burnHold, {280, 34})) {
                    controller.burnAsset(assetId);
                    av.selected = json();
                    ImGui::CloseCurrentPopup();
                }
                vspace(6);
            }

            if (neonButton("CLOSE", BtnKind::Ghost, {110, 38})) {
                av.selected = json();
                ImGui::CloseCurrentPopup();
            }
            endAdaptiveModal();
        }
    }
}

}  // namespace tb::ui
