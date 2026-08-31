// The TackleBox look: deep-void navy, cyan light lines, restrained glow.
// One palette, two faces - Rajdhani for structure, Share Tech Mono for data.
#pragma once

#include <imgui.h>

namespace tb::ui {

// --- palette ----------------------------------------------------------------
namespace col {
constexpr ImU32 rgba(unsigned rgb, unsigned a = 0xFF) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, a);
}

inline const ImU32 Void      = rgba(0x04070C);  // glClear / deepest layer
inline const ImU32 Bg        = rgba(0x070B14);
inline const ImU32 Panel     = rgba(0x0A121F);
inline const ImU32 PanelHi   = rgba(0x0E1828);
inline const ImU32 Inset     = rgba(0x060D18);  // input wells
inline const ImU32 Hairline  = rgba(0x14273B);
inline const ImU32 HairHi    = rgba(0x25567C);

inline const ImU32 Cyan      = rgba(0x00E0FF);
inline const ImU32 CyanDim   = rgba(0x0E7A96);
inline const ImU32 CyanFaint = rgba(0x00E0FF, 0x22);

inline const ImU32 Ice       = rgba(0xD9F2FF);  // primary text
inline const ImU32 Steel     = rgba(0x7A96AC);  // secondary text
inline const ImU32 Slate     = rgba(0x455C72);  // faint text

inline const ImU32 Success   = rgba(0x00FFA8);
inline const ImU32 Warn      = rgba(0xFFB020);
inline const ImU32 Danger    = rgba(0xFF3D5F);
inline const ImU32 Violet    = rgba(0xB44BFF);  // auto-sign accent

inline ImU32 alpha(ImU32 color, float a) {
    unsigned v = static_cast<unsigned>((color >> IM_COL32_A_SHIFT & 0xFF) * a);
    return (color & ~IM_COL32_A_MASK) | (v << IM_COL32_A_SHIFT);
}
inline ImVec4 vec(ImU32 color) { return ImGui::ColorConvertU32ToFloat4(color); }
}  // namespace col

// --- fonts ------------------------------------------------------------------
struct Fonts {
    ImFont* ui = nullptr;        // Rajdhani Regular
    ImFont* uiMedium = nullptr;  // Rajdhani Medium
    ImFont* uiSemi = nullptr;    // Rajdhani SemiBold
    ImFont* uiBold = nullptr;    // Rajdhani Bold
    ImFont* mono = nullptr;      // Share Tech Mono
};

Fonts& fonts();

// Base sizes; PushFont(font, size) scales freely from these.
constexpr float kText = 17.0f;
constexpr float kTextSm = 14.5f;
constexpr float kTextLg = 20.0f;
constexpr float kMono = 15.0f;
constexpr float kMonoSm = 13.0f;
constexpr float kH1 = 30.0f;
constexpr float kHero = 42.0f;

// Cosmetic preferences persisted in settings.json (outside the vault: they
// have no security weight).
struct Cosmetics {
    float glow = 1.0f;        // 0..1 glow intensity
    bool reduceMotion = false;
};
Cosmetics& cosmetics();
void loadCosmetics();
void saveCosmetics();

// Load embedded fonts into the atlas and apply the ImGui style.
void initTheme(float contentScale);

// The content scale captured at initTheme; the "scale" of the UI rules.
float dpiScale();

}  // namespace tb::ui
