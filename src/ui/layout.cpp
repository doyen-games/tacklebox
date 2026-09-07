#include "ui/layout.hpp"

#include "ui/theme.hpp"

namespace tb::ui {

LayoutConfig& layoutConfig() {
    static LayoutConfig config;
    return config;
}

namespace {
Layout g_layout;
ImVec2 g_pageMin, g_pageMax;
float g_pageScroll = 0.0f;
float g_scrollRequest = -1.0f;
int g_scrollFrames = 0;
}

void updateLayout(float safeTop, float safeBottom) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float w = vp->WorkSize.x;

    LayoutConfig& config = layoutConfig();
    if (config.override_ == 0)
        g_layout.factor = FormFactor::Phone;
    else if (config.override_ == 1)
        g_layout.factor = FormFactor::Tablet;
    else if (config.override_ == 2)
        g_layout.factor = FormFactor::Desktop;
    else
        g_layout.factor = w < 640.0f  ? FormFactor::Phone
                          : w < 1000.0f ? FormFactor::Tablet
                                        : FormFactor::Desktop;

#ifdef TB_MOBILE  // defined by the iOS/Android build configurations
    g_layout.touch = true;
#else
    g_layout.touch = config.forceTouch;
#endif
    g_layout.safeTop = safeTop;
    g_layout.safeBottom = safeBottom;

    // Frame-level style adjustments the whole tree inherits. The text size
    // preference multiplies every font size; layouts derive from text
    // metrics, so the whole tree reflows with it.
    ImGuiStyle& style = ImGui::GetStyle();
    const int textPct = config.textSizePct > 0 ? config.textSizePct : cosmetics().textSizePct;
    style.FontScaleMain =
        (g_layout.phone() ? 1.05f : 1.0f) * static_cast<float>(textPct) / 100.0f;
    style.TouchExtraPadding = g_layout.touch ? ImVec2(4, 4) : ImVec2(0, 0);
    style.ScrollbarSize = g_layout.touch ? 6.0f : 10.0f;
}

const Layout& layout() { return g_layout; }

void setPageRect(const ImVec2& min, const ImVec2& max, float scrollY) {
    g_pageMin = min;
    g_pageMax = max;
    g_pageScroll = scrollY;
}

float pageTop() { return g_pageMin.y; }

float pageBottom() { return g_pageMax.y; }

float pageScroll() { return g_pageScroll; }

void requestPageScroll(float y) {
    g_scrollRequest = y;
    // Content added this frame only sizes on the next one, and the scroll
    // range follows a frame later still; hold the request across both.
    g_scrollFrames = 3;
}

float takePageScrollRequest() {
    if (g_scrollFrames <= 0) return -1.0f;
    --g_scrollFrames;
    return g_scrollRequest;
}

}  // namespace tb::ui
