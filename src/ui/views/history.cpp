// The signing audit log: every approval, rejection and auto-sign the vault
// has ever produced, newest first.
#include <SDL3/SDL.h>

#include "core/paths.hpp"
#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/widgets.hpp"

namespace tb::ui {

void drawHistory(AppState& state, Controller& controller) {
    (void)controller;
    heading("History");
    subtext("The local signing log. Recorded at signature time inside the vault, so it "
            "survives restarts and cannot be edited from outside.");
    vspace(10);

    static int filter = 0;  // 0 all, 1 approved, 2 rejected
    const char* filters[] = {"ALL", "APPROVED", "REJECTED"};
    for (int i = 0; i < 3; ++i) {
        if (neonButton(filters[i], filter == i ? BtnKind::Primary : BtnKind::Subtle, {110, 30}))
            filter = i;
        ImGui::SameLine(0, 6);
    }
    // Export the full log (every entry, regardless of filter) as CSV.
    if (neonButton("EXPORT CSV", BtnKind::Ghost, {120, 30}) && !state.vault.audit.empty()) {
        struct SavePayload {
            Controller* controller;
            std::string csv;
        };
        auto* payload = new SavePayload{&controller, auditCsv(state.vault.audit)};
        static const SDL_DialogFileFilter kFilters[] = {{"CSV", "csv"}};
        SDL_ShowSaveFileDialog(
            [](void* userdata, const char* const* filelist, int) {
                auto* data = static_cast<SavePayload*>(userdata);
                std::string path = filelist && filelist[0] ? filelist[0] : std::string();
                Controller* c = data->controller;
                std::string csv = std::move(data->csv);
                delete data;
                if (path.empty()) return;  // canceled
                // The dialog callback may arrive off-main; marshal.
                c->runner().postMain([c, path, csv = std::move(csv)] {
                    if (atomicWrite(path, csv, /*keepBackup=*/false))
                        c->toast(Toast::Success, "Signing log exported to " + path);
                    else
                        c->toast(Toast::Error, "Could not write " + path);
                });
            },
            payload, nullptr, kFilters, 1, "tacklebox-signing-log.csv");
    }
    vspace(10);

    if (state.vault.audit.empty()) {
        emptyState(Icon::Clock, "Nothing in the log",
                   "Signatures and rejections will be recorded here.");
        return;
    }

    // Phones get a stacked card list instead of the wide table.
    if (layout().phone()) {
        int row = 0;
        for (const auto& entry : state.vault.audit) {
            if (filter == 1 && !entry.approved) continue;
            if (filter == 2 && entry.approved) continue;
            ImGui::PushID(row++);
            if (beginCard("h")) {
                monoText(entry.summary, col::Ice, kMono);
                ImGui::PushFont(fonts().mono, kMonoSm);
                ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                ImGui::Text("%s  -  %s", entry.signer.c_str(),
                            formatLocal(entry.time).c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                if (entry.approved)
                    badgeFilled("SIGNED", col::Success);
                else
                    badge("REJECTED", col::Danger);
                ImGui::SameLine(0, 6);
                badge(entry.verdict.c_str(), col::Steel);
                if (!entry.txId.empty()) {
                    ImGui::SameLine();
                    if (iconButton("##cptx", Icon::Copy, "Copy transaction id", col::Slate,
                                   13.0f))
                        ImGui::SetClipboardText(entry.txId.c_str());
                }
            }
            endCard();
            ImGui::PopID();
            vspace(6);
        }
        return;
    }

    if (beginCard("auditlog")) {
        if (ImGui::BeginTable("audit", 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Signer", ImGuiTableColumnFlags_WidthStretch, 0.22f);
            ImGui::TableSetupColumn("Transaction", ImGuiTableColumnFlags_WidthStretch, 0.42f);
            ImGui::TableSetupColumn("Verdict", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::PushFont(fonts().uiSemi, kTextSm);
            ImGui::TableHeadersRow();
            ImGui::PopFont();

            int row = 0;
            for (const auto& entry : state.vault.audit) {
                if (filter == 1 && !entry.approved) continue;
                if (filter == 2 && entry.approved) continue;
                ImGui::PushID(row++);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::PushFont(fonts().mono, kMonoSm);
                ImGui::TextUnformatted(formatLocal(entry.time).c_str());
                ImGui::PopFont();

                ImGui::TableNextColumn();
                ImGui::PushFont(fonts().mono, kMonoSm);
                ImGui::TextUnformatted(entry.signer.c_str());
                ImGui::PopFont();

                ImGui::TableNextColumn();
                ImGui::PushFont(fonts().mono, kMono);
                ImGui::TextUnformatted(entry.summary.c_str());
                ImGui::PopFont();
                if (!entry.txId.empty()) {
                    ImGui::PushFont(fonts().mono, kMonoSm);
                    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
                    ImGui::TextUnformatted(middleEllipsis(entry.txId, 14, 8).c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                    ImGui::SameLine();
                    if (iconButton("##cptx", Icon::Copy, "Copy transaction id", col::Slate,
                                   12.0f))
                        ImGui::SetClipboardText(entry.txId.c_str());
                }

                ImGui::TableNextColumn();
                {
                    ImU32 c = col::Steel;
                    if (entry.verdict == "trusted" || entry.verdict == "auto-signed")
                        c = col::Success;
                    else if (entry.verdict == "stale-pin") c = col::Warn;
                    else if (entry.verdict == "constraint-fail") c = col::Danger;
                    badge(entry.verdict.c_str(), c);
                }

                ImGui::TableNextColumn();
                if (entry.approved)
                    badgeFilled("SIGNED", col::Success);
                else
                    badge("REJECTED", col::Danger);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    endCard();
}

}  // namespace tb::ui
