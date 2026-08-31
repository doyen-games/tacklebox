// The sole layout API (ImGui UI Blueprint section 8; see CLAUDE.md).
// Every screen derives its layout from these helpers plus ImGui metrics.
// New helpers land here as diffs; never inline the same layout math twice.
#pragma once
#include <cfloat>
#include "imgui.h"

namespace ui {

// Set once at boot from your DPI query. All px literals pass through S().
inline float gScale = 1.0f;
inline float S(float px) { return px * gScale; }

// Vertical space in text rows, never raw px.
inline void VSpace(float rows = 1.0f) {
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * rows));
}

// Align the next item of known width.
inline void CenterNext(float w) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - w) * 0.5f);
}
inline void RightNext(float w) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
}

inline void CenteredText(const char* txt) {
    CenterNext(ImGui::CalcTextSize(txt).x);
    ImGui::TextUnformatted(txt);
}

// Wrapped body text: 90% of avail, centered block.
inline void BodyText(const char* txt) {
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail * 0.05f);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + avail * 0.9f);
    ImGui::TextUnformatted(txt);
    ImGui::PopTextWrapPos();
}

inline ImVec2 ButtonSize(const char* label, float min_w_px = 120.0f) {
    float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 4.0f;
    float mw = S(min_w_px);
    return ImVec2(w < mw ? mw : w, ImGui::GetFrameHeight() * 1.4f);
}
inline bool PrimaryButton(const char* label) { return ImGui::Button(label, ButtonSize(label)); }
inline bool CenteredButton(const char* label) {
    ImVec2 sz = ButtonSize(label);
    CenterNext(sz.x);
    return ImGui::Button(label, sz);
}

// Equal-width button row. EndButtonRow only if Begin returned true (BeginTable rules).
inline bool BeginButtonRow(const char* id, int n) {
    return ImGui::BeginTable(id, n, ImGuiTableFlags_SizingStretchSame);
}
inline bool CellButton(const char* label) {
    ImGui::TableNextColumn();
    return ImGui::Button(label, ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 1.4f));
}
inline void EndButtonRow() { ImGui::EndTable(); }

// Scroll area sized in rows. Always call EndScrollRegion (BeginChild rules).
inline bool BeginScrollRegion(const char* id, float rows) {
    return ImGui::BeginChild(id,
        ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * rows),
        ImGuiChildFlags_Borders);
}
inline void EndScrollRegion() { ImGui::EndChild(); }

// Pointer cursor over the last item when it is clickable and hovered.
inline void HandOnHover() {
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

// Call before OpenPopup / Begin. frac_h 0 = auto height.
inline void NextWindowCentered(float frac_w = 0.5f, float frac_h = 0.0f) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * frac_w, vp->WorkSize.y * frac_h), ImGuiCond_Appearing);
}

} // namespace ui
